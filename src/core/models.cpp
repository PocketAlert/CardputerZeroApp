#include "models.h"

namespace pa {
namespace {

// The API is loosely typed in places (nulls, numbers as strings), so every
// field read is tolerant instead of throwing on the first surprise.
std::string str(const json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) {
        return {};
    }
    if (it->is_string()) {
        return it->get<std::string>();
    }
    return it->dump();
}

long long num(const json& j, const char* key, long long fallback = 0) {
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) {
        return fallback;
    }
    if (it->is_number()) {
        return it->get<long long>();
    }
    if (it->is_boolean()) {
        return it->get<bool>() ? 1 : 0;
    }
    if (it->is_string()) {
        try {
            return std::stoll(it->get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

bool boolean(const json& j, const char* key, bool fallback = false) {
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) {
        return fallback;
    }
    if (it->is_boolean()) {
        return it->get<bool>();
    }
    if (it->is_number()) {
        return it->get<long long>() != 0;
    }
    if (it->is_string()) {
        const auto s = it->get<std::string>();
        return s == "true" || s == "1";
    }
    return fallback;
}

std::optional<int> opt_int(const json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) {
        return std::nullopt;
    }
    if (it->is_number()) {
        return it->get<int>();
    }
    if (it->is_string()) {
        try {
            return std::stoi(it->get<std::string>());
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

template <typename T>
std::vector<T> list(const json& j, const char* key) {
    std::vector<T> out;
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array()) {
        return out;
    }
    for (const auto& item : *it) {
        out.push_back(item.get<T>());
    }
    return out;
}

void put_opt(json& j, const char* key, const std::optional<int>& value) {
    if (value) {
        j[key] = *value;
    } else {
        j[key] = nullptr;
    }
}

} // namespace

std::vector<Action> parse_actions_field(const json& value) {
    std::vector<Action> out;
    if (value.is_array()) {
        for (const auto& item : value) {
            if (item.is_object()) {
                out.push_back(item.get<Action>());
            }
        }
    } else if (value.is_string()) {
        const auto parsed = json::parse(value.get<std::string>(), nullptr, false);
        if (!parsed.is_discarded()) {
            return parse_actions_field(parsed);
        }
    }
    return out;
}

void from_json(const json& j, Action& v) {
    v.type = str(j, "type");
    v.label = str(j, "label");
    v.value = str(j, "value");
}

void to_json(json& j, const Action& v) {
    j = json{{"type", v.type}, {"label", v.label}, {"value", v.value}};
}

void from_json(const json& j, Attachment& v) {
    v.tid = str(j, "tid");
    v.file_name = str(j, "file_name");
    v.content_type = str(j, "content_type");
    v.file_size = num(j, "file_size");
    v.link = str(j, "link");
}

void from_json(const json& j, Message& v) {
    v.tid = str(j, "tid");
    v.title = str(j, "title");
    v.message = str(j, "message");
    v.application = str(j, "application");
    v.device = str(j, "device");
    v.is_read = boolean(j, "is_read");
    v.priority = static_cast<int>(num(j, "priority"));
    v.was_scheduled = boolean(j, "was_scheduled");
    if (const auto it = j.find("actions"); it != j.end()) {
        v.actions = parse_actions_field(*it);
    }
    if (const auto it = j.find("attachment"); it != j.end() && it->is_object()) {
        v.attachment = it->get<Attachment>();
    }
    v.created_at = str(j, "created_at");
}

void to_json(json& j, const Message& v) {
    j = json{{"tid", v.tid},
             {"title", v.title},
             {"message", v.message},
             {"application", v.application},
             {"device", v.device},
             {"is_read", v.is_read},
             {"priority", v.priority},
             {"was_scheduled", v.was_scheduled},
             {"created_at", v.created_at}};
    if (!v.actions.empty()) {
        j["actions"] = v.actions;
    }
    if (v.attachment) {
        j["attachment"] = json{{"tid", v.attachment->tid},
                               {"file_name", v.attachment->file_name},
                               {"content_type", v.attachment->content_type},
                               {"file_size", v.attachment->file_size},
                               {"link", v.attachment->link}};
    }
}

void from_json(const json& j, MessagePage& v) {
    v.data = list<Message>(j, "data");
    if (const auto it = j.find("meta"); it != j.end() && it->is_object()) {
        v.total = static_cast<int>(num(*it, "total"));
        v.has_more = boolean(*it, "has_more");
        v.next_offset = static_cast<int>(num(*it, "next_offset"));
    }
}

void to_json(json& j, const MessageCreate& v) {
    j = json{{"title", v.title}, {"message", v.message}};
    if (!v.application_id.empty()) j["application_id"] = v.application_id;
    if (!v.device_id.empty()) j["device_id"] = v.device_id;
    if (v.level) j["level"] = *v.level;
    if (!v.actions.empty()) j["actions"] = v.actions;
    if (!v.send_at.empty()) j["send_at"] = v.send_at;
    if (!v.delay.empty()) j["delay"] = v.delay;
    if (!v.image.empty()) j["image"] = v.image;
}

void from_json(const json& j, MessageCreated& v) {
    v.tid = str(j, "tid");
    v.title = str(j, "title");
    v.scheduled = boolean(j, "scheduled");
    v.status = str(j, "status");
    v.deliver_at = str(j, "deliver_at");
    v.tz = str(j, "tz");
}

void from_json(const json& j, ScheduledMessage& v) {
    v.tid = str(j, "tid");
    v.title = str(j, "title");
    v.message = str(j, "message");
    v.priority = static_cast<int>(num(j, "priority"));
    v.status = str(j, "status");
    v.deliver_at = str(j, "deliver_at");
    v.tz = str(j, "tz");
    v.application = str(j, "application");
    v.device = str(j, "device");
    if (const auto it = j.find("actions"); it != j.end()) {
        v.actions = parse_actions_field(*it);
    }
    v.created_at = str(j, "created_at");
}

void to_json(json& j, const ScheduledMessage& v) {
    j = json{{"tid", v.tid},
             {"title", v.title},
             {"message", v.message},
             {"priority", v.priority},
             {"status", v.status},
             {"deliver_at", v.deliver_at},
             {"tz", v.tz},
             {"application", v.application},
             {"device", v.device},
             {"actions", v.actions},
             {"created_at", v.created_at}};
}

void from_json(const json& j, Usage& v) {
    v.used = static_cast<int>(num(j, "used"));
    v.limit = opt_int(j, "limit");
    v.unlimited = boolean(j, "unlimited");
    v.rejected = static_cast<int>(num(j, "rejected"));
    v.resets_at = str(j, "resets_at");
}

void from_json(const json& j, DayStat& v) {
    v.date = str(j, "date");
    v.count = static_cast<int>(num(j, "count"));
}

void from_json(const json& j, Application& v) {
    v.tid = str(j, "tid");
    v.name = str(j, "name");
    v.description = str(j, "description");
    v.color = str(j, "color");
    v.default_level = opt_int(j, "default_level");
    v.is_active = boolean(j, "is_active", true);
    v.created_at = str(j, "created_at");
}

void to_json(json& j, const Application& v) {
    j = json{{"tid", v.tid},
             {"name", v.name},
             {"description", v.description},
             {"color", v.color},
             {"is_active", v.is_active},
             {"created_at", v.created_at}};
    put_opt(j, "default_level", v.default_level);
}

void from_json(const json& j, Device& v) {
    v.tid = str(j, "tid");
    v.serial_number = str(j, "serial_number");
    v.name = str(j, "name");
    v.is_active = boolean(j, "is_active", true);
    v.created_at = str(j, "created_at");
}

void to_json(json& j, const Device& v) {
    j = json{{"tid", v.tid},
             {"serial_number", v.serial_number},
             {"name", v.name},
             {"is_active", v.is_active},
             {"created_at", v.created_at}};
}

void from_json(const json& j, Webhook& v) {
    v.tid = str(j, "tid");
    v.name = str(j, "name");
    v.url = str(j, "url");
    v.application_tid = str(j, "application_tid");
    v.device_tid = str(j, "device_tid");
    v.message = str(j, "message");
    v.actions = str(j, "actions");
    v.default_level = opt_int(j, "default_level");
    v.is_active = boolean(j, "is_active", true);
    v.created_at = str(j, "created_at");
}

void to_json(json& j, const Webhook& v) {
    j = json{{"tid", v.tid},
             {"name", v.name},
             {"url", v.url},
             {"application_tid", v.application_tid},
             {"device_tid", v.device_tid},
             {"message", v.message},
             {"actions", v.actions},
             {"is_active", v.is_active},
             {"created_at", v.created_at}};
    put_opt(j, "default_level", v.default_level);
}

void from_json(const json& j, WebhookRequest& v) {
    v.id = num(j, "id");
    v.method = str(j, "method");
    v.source_ip = str(j, "source_ip");
    v.content_type = str(j, "content_type");
    v.body = str(j, "body");
    v.size = num(j, "size");
    v.status = str(j, "status");
    v.error_code = str(j, "error_code");
    v.rendered_message = str(j, "rendered_message");
    v.message_tid = str(j, "message_tid");
    v.priority = opt_int(j, "priority");
    v.duration_ms = num(j, "duration_ms");
    v.created_at = str(j, "created_at");
}

void from_json(const json& j, WebhookHistory& v) {
    v.requests = list<WebhookRequest>(j, "requests");
    v.total = static_cast<int>(num(j, "total"));
    v.page = static_cast<int>(num(j, "page", 1));
    v.per = static_cast<int>(num(j, "per", 50));
    v.retention_days = static_cast<int>(num(j, "retention_days"));
}

void from_json(const json& j, Form& v) {
    v.tid = str(j, "tid");
    v.name = str(j, "name");
    v.is_active = boolean(j, "is_active", true);
    v.closed = boolean(j, "closed");
    v.submissions_count = static_cast<int>(num(j, "submissions_count"));
    v.last_submission_at = str(j, "last_submission_at");
    v.created_at = str(j, "created_at");
}

void to_json(json& j, const Form& v) {
    j = json{{"tid", v.tid},
             {"name", v.name},
             {"is_active", v.is_active},
             {"closed", v.closed},
             {"submissions_count", v.submissions_count},
             {"last_submission_at", v.last_submission_at},
             {"created_at", v.created_at}};
}

void from_json(const json& j, ApiKey& v) {
    v.tid = str(j, "tid");
    v.name = str(j, "name");
    v.token = str(j, "token");
    v.is_active = boolean(j, "is_active", true);
    v.created_at = str(j, "created_at");
}

void to_json(json& j, const ApiKey& v) {
    j = json{{"tid", v.tid},
             {"name", v.name},
             {"token", v.token},
             {"is_active", v.is_active},
             {"created_at", v.created_at}};
}

void from_json(const json& j, Profile& v) {
    v.id = num(j, "id");
    v.username = str(j, "username");
    v.email = str(j, "email");
    v.timezone = str(j, "timezone");
    v.email_verified = boolean(j, "email_verified");
    v.subscription_status = str(j, "subscription_status");
}

void from_json(const json& j, SigninResult& v) {
    v.token = str(j, "token");
    v.two_factor_required = boolean(j, "two_factor_required");
    v.challenge = str(j, "challenge");
    v.name = str(j, "name");
    v.email = str(j, "email");
}

} // namespace pa
