#include "api.h"

#include "parse.h"

#include <chrono>
#include <thread>

namespace pa {

std::string ApiError::text() const {
    if (status == 0) {
        return message.empty() ? "No connection" : "No connection: " + message;
    }
    if (status == 429 && limit > 0) {
        return "Daily limit reached (" + std::to_string(used) + "/" + std::to_string(limit) + ")";
    }
    std::string out = message;
    if (out.empty()) {
        switch (status) {
            case 401: out = "Unauthorized"; break;
            case 403: out = "Forbidden"; break;
            case 404: out = "Not found"; break;
            case 429: out = "Too many requests"; break;
            default: out = "HTTP " + std::to_string(status); break;
        }
    }
    if (!details.empty()) {
        out += ": " + details;
    }
    return out;
}

std::string path_escape(const std::string& segment) {
    return url_encode(segment);
}

Api::Api(std::shared_ptr<HttpTransport> transport, std::string base_url, Credentials credentials)
    : transport_(std::move(transport)), credentials_(std::move(credentials)) {
    set_base_url(std::move(base_url));
}

void Api::set_base_url(std::string base_url) {
    while (!base_url.empty() && base_url.back() == '/') {
        base_url.pop_back();
    }
    // Plain http would put the key and password on the wire in clear.
    base_url_ = is_valid_base_url(base_url) ? base_url : kDefaultBaseUrl;
}

Api::Raw Api::call(const std::string& method, const std::string& path, const json* body, bool authenticated) {
    HttpRequest request;
    request.method = method;
    request.url = base_url_ + path;
    if (method == "GET") request.timeout_seconds = 20;
    request.headers.emplace_back("Accept", "application/json");
    if (authenticated) {
        if (!credentials_.api_key.empty()) {
            request.headers.emplace_back("Token", credentials_.api_key);
        } else if (!credentials_.jwt.empty()) {
            request.headers.emplace_back("Authorization", "Bearer " + credentials_.jwt);
        }
    }
    if (body) {
        request.headers.emplace_back("Content-Type", "application/json");
        // Replace, not throw, on invalid UTF-8 typed on the device.
        request.body = body->dump(-1, ' ', false, json::error_handler_t::replace);
    }

    Raw raw;
    raw.response = transport_->send(request);
    // Reads are safe to repeat; a flaky Wi-Fi drop usually clears within a second.
    if (raw.response.status == 0 && method == "GET" && !http_cancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        raw.response = transport_->send(request);
    }
    if (raw.response.status == 0) {
        raw.error.status = 0;
        raw.error.message = raw.response.transport_error;
        return raw;
    }

    raw.body = json::parse(raw.response.body, nullptr, false);
    if (raw.response.status >= 400) {
        raw.error.status = raw.response.status;
        if (!raw.body.is_discarded() && raw.body.is_object()) {
            const auto read = [&](const char* key) {
                const auto it = raw.body.find(key);
                return it != raw.body.end() && it->is_string() ? it->get<std::string>() : std::string{};
            };
            raw.error.message = read("error");
            if (raw.error.message.empty()) {
                raw.error.message = read("message");
            }
            raw.error.details = read("details");
            raw.error.resets_at = read("resets_at");
            raw.error.upgrade_url = read("upgrade_url");
            if (raw.body.contains("limit") && raw.body["limit"].is_number()) {
                raw.error.limit = raw.body["limit"].get<int>();
            }
            if (raw.body.contains("used") && raw.body["used"].is_number()) {
                raw.error.used = raw.body["used"].get<int>();
            }
        } else if (raw.response.body.size() < 200) {
            raw.error.message = raw.response.body;
        }
        return raw;
    }
    raw.ok = true;
    return raw;
}

template <typename T>
Result<T> Api::decode(const std::string& method, const std::string& path, const json* body, bool authenticated) {
    Result<T> result;
    auto raw = call(method, path, body, authenticated);
    if (!raw.ok) {
        result.error = raw.error;
        return result;
    }
    if (raw.body.is_discarded()) {
        result.error.status = raw.response.status;
        result.error.message = "Unexpected response";
        return result;
    }
    try {
        // Some endpoints wrap payloads in {"data": ...}; lists and objects are both accepted.
        const json* payload = &raw.body;
        if constexpr (!std::is_same_v<T, MessagePage>) {
            if (raw.body.is_object() && raw.body.contains("data") &&
                (raw.body["data"].is_array() || raw.body["data"].is_object())) {
                payload = &raw.body["data"];
            }
        }
        if (payload->is_null()) {
            result.ok = true;
            return result;
        }
        result.value = payload->get<T>();
        result.ok = true;
    } catch (const std::exception& e) {
        result.error.status = raw.response.status;
        result.error.message = std::string("Bad response: ") + e.what();
    }
    return result;
}

Result<Empty> Api::command(const std::string& method, const std::string& path, const json* body) {
    Result<Empty> result;
    auto raw = call(method, path, body);
    result.ok = raw.ok;
    result.error = raw.error;
    return result;
}

Result<SigninResult> Api::signin(const std::string& email, const std::string& password) {
    const json body{{"email", email}, {"password", password}};
    return decode<SigninResult>("POST", "/v1/auth/signin", &body, false);
}

Result<SigninResult> Api::two_factor_login(const std::string& challenge, const std::string& code) {
    const json body{{"challenge", challenge}, {"code", code}};
    return decode<SigninResult>("POST", "/v1/auth/2fa/login", &body, false);
}

Result<Profile> Api::profile() {
    return decode<Profile>("GET", "/v1/auth/profile");
}

Result<MessagePage> Api::messages(const MessageQuery& query) {
    const auto path = "/v2/messages" + build_query({{"size", std::to_string(query.size)},
                                                    {"offset", std::to_string(query.offset)},
                                                    {"sort", query.sort},
                                                    {"application", query.application_tid},
                                                    {"tid", query.device_tid}});
    return decode<MessagePage>("GET", path);
}

Result<Message> Api::message(const std::string& tid) {
    return decode<Message>("GET", "/v1/messages/" + path_escape(tid));
}

Result<Empty> Api::set_read(const std::string& tid, bool read) {
    const json body{{"is_read", read}};
    return command("PUT", "/v1/messages/" + path_escape(tid), &body);
}

Result<Empty> Api::mark_all_read() {
    return command("PUT", "/v1/messages/read-all");
}

Result<Empty> Api::delete_message(const std::string& tid) {
    return command("DELETE", "/v1/messages/" + path_escape(tid));
}

Result<Empty> Api::delete_all_messages() {
    return command("DELETE", "/v1/messages/bulk");
}

Result<MessageCreated> Api::send(const MessageCreate& message) {
    const json body = message;
    return decode<MessageCreated>("POST", "/v1/messages", &body);
}

Result<std::vector<DayStat>> Api::stats() {
    return decode<std::vector<DayStat>>("GET", "/v1/messages/stat");
}

Result<Usage> Api::usage() {
    return decode<Usage>("GET", "/v1/messages/usage");
}

Result<std::vector<ScheduledMessage>> Api::scheduled(const std::string& status) {
    return decode<std::vector<ScheduledMessage>>("GET", "/v1/messages/scheduled" + build_query({{"status", status}}));
}

Result<Empty> Api::cancel_scheduled(const std::string& tid) {
    return command("DELETE", "/v1/messages/scheduled/" + path_escape(tid));
}

namespace {

json application_body(const ApplicationInput& input) {
    json body = json::object();
    if (input.name) body["name"] = *input.name;
    if (input.description) body["description"] = *input.description;
    if (input.color) body["color"] = *input.color;
    if (input.default_level) body["default_level"] = *input.default_level;
    return body;
}

json webhook_body(const WebhookInput& input) {
    json body = json::object();
    if (input.name) body["name"] = *input.name;
    if (input.message) body["message"] = *input.message;
    if (input.slug) body["url"] = *input.slug;
    if (input.application_id) body["application_id"] = *input.application_id;
    if (input.device_id) body["device_id"] = *input.device_id;
    if (input.default_level) body["default_level"] = *input.default_level;
    if (input.actions) body["actions"] = json(*input.actions).dump();
    if (input.is_active) body["is_active"] = *input.is_active;
    return body;
}

} // namespace

Result<std::vector<Application>> Api::applications() {
    return decode<std::vector<Application>>("GET", "/v1/applications");
}

Result<Application> Api::create_application(const ApplicationInput& input) {
    const auto body = application_body(input);
    return decode<Application>("POST", "/v1/applications", &body);
}

Result<Empty> Api::update_application(const std::string& tid, const ApplicationInput& input) {
    const auto body = application_body(input);
    return command("PUT", "/v1/applications/" + path_escape(tid), &body);
}

Result<Empty> Api::delete_application(const std::string& tid) {
    return command("DELETE", "/v1/applications/" + path_escape(tid));
}

Result<std::vector<Device>> Api::devices() {
    return decode<std::vector<Device>>("GET", "/v1/devices");
}

Result<Device> Api::device_by_serial(const std::string& serial) {
    return decode<Device>("GET", "/v1/devices/id/" + path_escape(serial));
}

Result<std::string> Api::register_device(const std::string& serial, const std::string& name, const std::string& type,
                                         const std::string& version) {
    const json body{{"serial_number", serial}, {"name", name}, {"type", type}, {"version", version}};
    Result<std::string> result;
    auto raw = call("POST", "/v1/devices", &body);
    if (!raw.ok) {
        result.error = raw.error;
        return result;
    }
    if (raw.body.is_object() && raw.body.contains("tid") && raw.body["tid"].is_string()) {
        result.value = raw.body["tid"].get<std::string>();
    }
    result.ok = true;
    return result;
}

Result<Empty> Api::rename_device(const std::string& tid, const std::string& name) {
    const json body{{"name", name}};
    return command("PUT", "/v1/devices/" + path_escape(tid), &body);
}

Result<Empty> Api::delete_device(const std::string& tid) {
    return command("DELETE", "/v1/devices/" + path_escape(tid));
}

Result<std::vector<Webhook>> Api::webhooks() {
    return decode<std::vector<Webhook>>("GET", "/v1/webhooks");
}

Result<Webhook> Api::create_webhook(const WebhookInput& input) {
    const auto body = webhook_body(input);
    return decode<Webhook>("POST", "/v1/webhooks", &body);
}

Result<Empty> Api::update_webhook(const std::string& tid, const WebhookInput& input) {
    const auto body = webhook_body(input);
    return command("PUT", "/v1/webhooks/" + path_escape(tid), &body);
}

Result<Empty> Api::delete_webhook(const std::string& tid) {
    return command("DELETE", "/v1/webhooks/" + path_escape(tid));
}

Result<WebhookHistory> Api::webhook_history(const std::string& tid, int page, int per) {
    return decode<WebhookHistory>("GET", "/v1/webhooks/" + path_escape(tid) + "/history" +
                                             build_query({{"page", std::to_string(page)},
                                                          {"per", std::to_string(per)}}));
}

Result<std::vector<Form>> Api::forms() {
    return decode<std::vector<Form>>("GET", "/v1/forms");
}

Result<Empty> Api::delete_form(const std::string& tid) {
    return command("DELETE", "/v1/forms/" + path_escape(tid));
}

Result<std::vector<ApiKey>> Api::api_keys() {
    return decode<std::vector<ApiKey>>("GET", "/v1/apikeys");
}

Result<ApiKey> Api::create_api_key(const std::string& name) {
    const json body{{"name", name}};
    return decode<ApiKey>("POST", "/v1/apikeys", &body);
}

Result<Empty> Api::delete_api_key(const std::string& tid) {
    return command("DELETE", "/v1/apikeys/" + path_escape(tid));
}

HttpResponse Api::run_external(HttpRequest request) {
    request.follow_redirects = true;
    request.max_body_bytes = 256U * 1024U;
    return transport_->send(request);
}

} // namespace pa
