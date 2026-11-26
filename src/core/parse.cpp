#include "parse.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <cmath>
#include <regex>
#include <set>
#include <sstream>

namespace pa {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string to_lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string level_name(int level) {
    switch (level) {
        case -2: return "silent";
        case -1: return "low";
        case 0: return "default";
        case 1: return "high";
        case 2: return "critical";
        default: return std::to_string(level);
    }
}

std::optional<int> parse_level(const std::string& text) {
    static const std::map<std::string, int> aliases = {
        {"silent", -2}, {"min", -2},    {"low", -1},  {"default", 0}, {"normal", 0},
        {"high", 1},    {"critical", 2}, {"max", 2},  {"urgent", 2},
    };
    const auto key = to_lower(trim(text));
    if (const auto it = aliases.find(key); it != aliases.end()) {
        return it->second;
    }
    try {
        size_t used = 0;
        const int value = std::stoi(key, &used);
        if (used == key.size() && value >= kMinLevel && value <= kMaxLevel) {
            return value;
        }
    } catch (...) {
    }
    return std::nullopt;
}

bool is_http_url(const std::string& text) {
    static const std::regex pattern(R"(^https?://[^\s/?#]+[^\s]*$)", std::regex::icase);
    return std::regex_match(text, pattern);
}

bool is_valid_base_url(const std::string& text) {
    static const std::regex https(R"(^https://[A-Za-z0-9.-]+(:[0-9]{1,5})?(/[^\s]*)?$)", std::regex::icase);
    static const std::regex local(R"(^http://(localhost|127\.0\.0\.1|\[::1\])(:[0-9]{1,5})?(/[^\s]*)?$)",
                                  std::regex::icase);
    return std::regex_match(text, https) || std::regex_match(text, local);
}

std::optional<int> parse_int(const std::string& text) {
    int value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end || text.empty()) {
        return std::nullopt;
    }
    return value;
}

namespace {

bool is_header_token(const std::string& name) {
    if (name.empty()) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || std::strchr("!#$%&'*+-.^_`|~", c) != nullptr;
    });
}

bool is_header_value(const std::string& value) {
    return value.find_first_of("\r\n") == std::string::npos && value.find('\0') == std::string::npos;
}

bool is_allowed_method(const std::string& method) {
    static const std::set<std::string> allowed = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"};
    return allowed.count(method) != 0;
}

std::optional<std::string> http_action_value(const std::string& value, bool loose, ParseError& error) {
    if (!value.empty() && value.front() == '{') {
        const auto parsed = json::parse(value, nullptr, false);
        const bool has_url = !parsed.is_discarded() && parsed.is_object() && parsed.contains("url") &&
                             parsed["url"].is_string();
        if (!has_url || (!loose && !is_http_url(parsed["url"].get<std::string>()))) {
            error.message = "JSON value needs a valid http(s) \"url\"";
            return std::nullopt;
        }
        return value;
    }

    std::string method = "POST";
    std::string target = value;
    if (const auto space = value.find(' '); space != std::string::npos) {
        auto candidate = value.substr(0, space);
        std::transform(candidate.begin(), candidate.end(), candidate.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        if (candidate == "GET" || candidate == "POST" || candidate == "PUT" || candidate == "DELETE") {
            method = candidate;
            target = trim(value.substr(space + 1));
        }
    }
    if (!loose && !is_http_url(target)) {
        error.message = "value must be [METHOD ]URL or a JSON object";
        return std::nullopt;
    }
    return json{{"method", method}, {"url", target}}.dump();
}

} // namespace

std::optional<Action> parse_action(const std::string& spec, bool template_mode, ParseError& error) {
    const auto first = spec.find(':');
    const auto second = first == std::string::npos ? std::string::npos : spec.find(':', first + 1);
    if (second == std::string::npos) {
        error.message = "expected type:label:value";
        return std::nullopt;
    }
    Action action;
    action.type = to_lower(trim(spec.substr(0, first)));
    action.label = trim(spec.substr(first + 1, second - first - 1));
    action.value = trim(spec.substr(second + 1));
    if (action.label.empty() || action.value.empty()) {
        error.message = "expected type:label:value";
        return std::nullopt;
    }
    const bool loose = template_mode && action.value.find('%') != std::string::npos;

    if (action.type == "view") {
        if (!loose && !is_http_url(action.value)) {
            error.message = "view: value must be an http(s) URL";
            return std::nullopt;
        }
    } else if (action.type == "copy") {
    } else if (action.type == "http") {
        auto value = http_action_value(action.value, loose, error);
        if (!value) {
            error.message = "http: " + error.message;
            return std::nullopt;
        }
        action.value = *value;
    } else {
        error.message = "type must be view, http or copy";
        return std::nullopt;
    }
    return action;
}

std::string format_action_spec(const Action& action) {
    std::string value = action.value;
    if (action.type == "http") {
        if (const auto config = parse_http_action(action.value); config && config->headers.empty() &&
                                                                  config->body.empty()) {
            value = config->method + " " + config->url;
        }
    }
    return action.type + ":" + action.label + ":" + value;
}

std::optional<std::string> normalize_delay(const std::string& text, ParseError& error) {
    const auto input = to_lower(trim(text));
    if (input.empty()) {
        error.message = "delay is empty";
        return std::nullopt;
    }
    if (std::all_of(input.begin(), input.end(), [](unsigned char c) { return std::isdigit(c); })) {
        if (input.size() > 9) {
            error.message = "delay is too long";
            return std::nullopt;
        }
        return input;
    }

    static const std::regex unit_pattern(R"((\d+(?:\.\d+)?)(d|h|ms|m|s))");
    double total_seconds = 0;
    size_t consumed = 0;
    for (auto it = std::sregex_iterator(input.begin(), input.end(), unit_pattern); it != std::sregex_iterator();
         ++it) {
        const auto& match = *it;
        if (static_cast<size_t>(match.position()) != consumed) {
            break;
        }
        if (match[1].length() > 9) {
            error.message = "delay is too long";
            return std::nullopt;
        }
        const double amount = std::stod(match[1].str());
        const auto unit = match[2].str();
        if (unit == "d") total_seconds += amount * 86400;
        else if (unit == "h") total_seconds += amount * 3600;
        else if (unit == "m") total_seconds += amount * 60;
        else if (unit == "s") total_seconds += amount;
        else if (unit == "ms") total_seconds += amount / 1000;
        consumed += static_cast<size_t>(match.length());
    }
    if (consumed != input.size()) {
        error.message = "use 30m, 2h, 1d, 1d12h or seconds";
        return std::nullopt;
    }
    if (total_seconds <= 0) {
        error.message = "delay must be positive";
        return std::nullopt;
    }
    constexpr double kMaxDelaySeconds = 10.0 * 365 * 86400;
    if (total_seconds > kMaxDelaySeconds) {
        error.message = "delay is too long";
        return std::nullopt;
    }

    // The server parses Go durations, which have no day unit.
    long long seconds = static_cast<long long>(std::llround(total_seconds));
    const long long hours = seconds / 3600;
    seconds %= 3600;
    const long long minutes = seconds / 60;
    seconds %= 60;
    std::ostringstream out;
    if (hours) out << hours << "h";
    if (minutes || hours) out << minutes << "m";
    out << seconds << "s";
    return out.str();
}

std::optional<HttpActionConfig> parse_http_action(const std::string& value) {
    HttpActionConfig config;
    const auto text = trim(value);
    if (!text.empty() && text.front() == '{') {
        const auto parsed = json::parse(text, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("url") || !parsed["url"].is_string()) {
            return std::nullopt;
        }
        config.url = parsed["url"].get<std::string>();
        if (parsed.contains("method") && parsed["method"].is_string()) {
            config.method = parsed["method"].get<std::string>();
            std::transform(config.method.begin(), config.method.end(), config.method.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        }
        if (parsed.contains("headers") && parsed["headers"].is_object()) {
            for (const auto& [name, header_value] : parsed["headers"].items()) {
                const auto text_value = header_value.is_string()
                                            ? header_value.get<std::string>()
                                            : header_value.dump(-1, ' ', false, json::error_handler_t::replace);
                if (!is_header_token(name) || !is_header_value(text_value)) {
                    return std::nullopt;
                }
                config.headers[name] = text_value;
            }
        }
        if (parsed.contains("body") && !parsed["body"].is_null()) {
            config.body = parsed["body"].is_string()
                              ? parsed["body"].get<std::string>()
                              : parsed["body"].dump(-1, ' ', false, json::error_handler_t::replace);
        }
    } else {
        config.url = text;
    }
    if (!is_http_url(config.url) || !is_allowed_method(config.method)) {
        return std::nullopt;
    }
    return config;
}

std::vector<size_t> filter_messages(const std::vector<Message>& messages, bool unread_only, const std::string& needle) {
    const auto folded = to_lower(needle);
    const auto matches = [&](const std::string& text) { return to_lower(text).find(folded) != std::string::npos; };
    std::vector<size_t> out;
    out.reserve(messages.size());
    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& m = messages[i];
        if (unread_only && m.is_read) continue;
        if (!folded.empty() && !matches(m.title) && !matches(m.message) && !matches(m.application)) continue;
        out.push_back(i);
    }
    return out;
}

} // namespace pa
