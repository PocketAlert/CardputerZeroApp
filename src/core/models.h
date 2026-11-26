#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace pa {

using json = nlohmann::json;

struct Action {
    std::string type;
    std::string label;
    std::string value;
};

struct Attachment {
    std::string tid;
    std::string file_name;
    std::string content_type;
    long long file_size = 0;
    std::string link;
};

struct Message {
    std::string tid;
    std::string title;
    std::string message;
    std::string application;
    std::string device;
    bool is_read = false;
    int priority = 0;
    bool was_scheduled = false;
    std::vector<Action> actions;
    std::optional<Attachment> attachment;
    std::string created_at;
};

struct MessagePage {
    std::vector<Message> data;
    int total = 0;
    bool has_more = false;
    int next_offset = 0;
};

struct MessageCreate {
    std::string title;
    std::string message;
    std::string application_id;
    std::string device_id;
    std::optional<int> level;
    std::vector<Action> actions;
    std::string send_at;
    std::string delay;
    std::string image;
};

struct MessageCreated {
    std::string tid;
    std::string title;
    bool scheduled = false;
    std::string status;
    std::string deliver_at;
    std::string tz;
};

struct ScheduledMessage {
    std::string tid;
    std::string title;
    std::string message;
    int priority = 0;
    std::string status;
    std::string deliver_at;
    std::string tz;
    std::string application;
    std::string device;
    std::vector<Action> actions;
    std::string created_at;
};

struct Usage {
    int used = 0;
    std::optional<int> limit;
    bool unlimited = false;
    int rejected = 0;
    std::string resets_at;
};

struct DayStat {
    std::string date;
    int count = 0;
};

struct Application {
    std::string tid;
    std::string name;
    std::string description;
    std::string color;
    std::optional<int> default_level;
    bool is_active = true;
    std::string created_at;
};

struct Device {
    std::string tid;
    std::string serial_number;
    std::string name;
    bool is_active = true;
    std::string created_at;
};

struct Webhook {
    std::string tid;
    std::string name;
    std::string url;
    std::string application_tid;
    std::string device_tid;
    std::string message;
    std::string actions;
    std::optional<int> default_level;
    bool is_active = true;
    std::string created_at;
};

struct WebhookRequest {
    long long id = 0;
    std::string method;
    std::string source_ip;
    std::string content_type;
    std::string body;
    long long size = 0;
    std::string status;
    std::string error_code;
    std::string rendered_message;
    std::string message_tid;
    std::optional<int> priority;
    long long duration_ms = 0;
    std::string created_at;
};

struct WebhookHistory {
    std::vector<WebhookRequest> requests;
    int total = 0;
    int page = 1;
    int per = 50;
    int retention_days = 0;
};

struct Form {
    std::string tid;
    std::string name;
    bool is_active = true;
    bool closed = false;
    int submissions_count = 0;
    std::string last_submission_at;
    std::string created_at;
};

struct ApiKey {
    std::string tid;
    std::string name;
    std::string token;
    bool is_active = true;
    std::string created_at;
};

struct Profile {
    long long id = 0;
    std::string username;
    std::string email;
    std::string timezone;
    bool email_verified = false;
    std::string subscription_status;
};

struct SigninResult {
    std::string token;
    bool two_factor_required = false;
    std::string challenge;
    std::string name;
    std::string email;
};

void from_json(const json& j, Action& v);
void to_json(json& j, const Action& v);
void from_json(const json& j, Attachment& v);
void from_json(const json& j, Message& v);
void from_json(const json& j, MessagePage& v);
void to_json(json& j, const MessageCreate& v);
void from_json(const json& j, MessageCreated& v);
void from_json(const json& j, ScheduledMessage& v);
void from_json(const json& j, Usage& v);
void from_json(const json& j, DayStat& v);
void from_json(const json& j, Application& v);
void from_json(const json& j, Device& v);
void from_json(const json& j, Webhook& v);
void from_json(const json& j, WebhookRequest& v);
void from_json(const json& j, WebhookHistory& v);
void from_json(const json& j, Form& v);
void from_json(const json& j, ApiKey& v);
void from_json(const json& j, Profile& v);
void from_json(const json& j, SigninResult& v);

void to_json(json& j, const Message& v);
void to_json(json& j, const Application& v);
void to_json(json& j, const Device& v);
void to_json(json& j, const Webhook& v);
void to_json(json& j, const ScheduledMessage& v);
void to_json(json& j, const Form& v);
void to_json(json& j, const ApiKey& v);

std::vector<Action> parse_actions_field(const json& value);

} // namespace pa
