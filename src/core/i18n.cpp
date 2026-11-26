#include "i18n.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace pa::i18n {
namespace {

using Table = std::unordered_map<std::string, std::string>;

struct State {
    std::string code = "en";
    Table english;
    Table active;
    // Keys missing everywhere are echoed back; they live here so tr() can return a pointer.
    Table missing;
};

State& state() {
    static State s;
    return s;
}

void flatten(const nlohmann::json& node, const std::string& prefix, Table& out) {
    if (node.is_string()) {
        out[prefix] = node.get<std::string>();
        return;
    }
    if (!node.is_object()) return;
    for (const auto& [key, value] : node.items()) {
        flatten(value, prefix.empty() ? key : prefix + "." + key, out);
    }
}

Table load_dir(const std::filesystem::path& dir) {
    Table table;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream in(entry.path());
        const auto parsed = nlohmann::json::parse(in, nullptr, false);
        if (parsed.is_discarded()) continue;
        flatten(parsed, entry.path().stem().string(), table);
    }
    return table;
}

} // namespace

const std::vector<Language>& languages() {
    static const std::vector<Language> list = {
        {"en", "English"},   {"de", "Deutsch"},          {"es", "Español"}, {"fr", "Français"},
        {"id", "Indonesia"}, {"it", "Italiano"},         {"ja", "日本語"},  {"ko", "한국어"},
        {"pt", "Português"}, {"ru", "Русский"},          {"zh", "简体中文"},
    };
    return list;
}

bool is_supported(std::string_view code) {
    for (const auto& language : languages()) {
        if (code == language.code) return true;
    }
    return false;
}

std::string from_locale_env(std::string_view value) {
    const auto end = value.find_first_of("_.@-");
    std::string code(value.substr(0, end));
    for (auto& c : code) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return is_supported(code) ? code : "en";
}

std::string resolve(std::string_view setting) {
    if (!setting.empty()) return is_supported(setting) ? std::string(setting) : "en";
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(name);
        // "C" and "POSIX" mean no preference rather than English specifically, so keep looking.
        if (value && value[0] && std::string_view(value) != "C" && std::string_view(value) != "POSIX") {
            return from_locale_env(value);
        }
    }
    return "en";
}

bool load(const std::filesystem::path& root, const std::string& code) {
    auto& s = state();
    s.english = load_dir(root / "en");
    s.code = is_supported(code) ? code : "en";
    s.active = s.code == "en" ? Table{} : load_dir(root / s.code);
    s.missing.clear();
    return !s.english.empty();
}

const std::string& current() {
    return state().code;
}

const char* tr(std::string_view key) {
    auto& s = state();
    const std::string k(key);
    if (auto it = s.active.find(k); it != s.active.end() && !it->second.empty()) return it->second.c_str();
    if (auto it = s.english.find(k); it != s.english.end()) return it->second.c_str();
    return s.missing.try_emplace(k, k).first->second.c_str();
}

std::string format(std::string_view text, Args args) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '{') {
            const auto close = text.find('}', i);
            if (close != std::string_view::npos) {
                const auto name = text.substr(i + 1, close - i - 1);
                bool replaced = false;
                for (const auto& [arg, value] : args) {
                    if (arg == name) {
                        out += value;
                        replaced = true;
                        break;
                    }
                }
                if (replaced) {
                    i = close + 1;
                    continue;
                }
            }
        }
        out += text[i++];
    }
    return out;
}

std::string tr(std::string_view key, Args args) {
    return format(tr(key), args);
}

} // namespace pa::i18n
