#pragma once

#include "http.h"
#include "models.h"

#include <memory>
#include <string>
#include <vector>

namespace pa {

constexpr const char* kDefaultBaseUrl = "https://api.pocketalert.app";
constexpr const char* kWebhookBaseUrl = "https://p4a.me/wh/";
constexpr const char* kFormBaseUrl = "https://p4a.me/f/";
constexpr const char* kApiKeysPageUrl = "https://pocketalert.app/account/apikeys";

struct ApiError {
    long status = 0;
    std::string message;
    std::string details;
    int limit = 0;
    int used = 0;
    std::string resets_at;
    std::string upgrade_url;

    bool network() const { return status == 0; }
    std::string text() const;
};

template <typename T>
struct Result {
    T value{};
    bool ok = false;
    ApiError error;
};

struct Empty {};

struct Credentials {
    std::string api_key;
    std::string jwt;
};

struct MessageQuery {
    int size = 30;
    int offset = 0;
    std::string sort = "desc";
    std::string application_tid;
    std::string device_tid;
};

struct ApplicationInput {
    std::optional<std::string> name;
    std::optional<std::string> description;
    std::optional<std::string> color;
    std::optional<int> default_level;
};

struct WebhookInput {
    std::optional<std::string> name;
    std::optional<std::string> message;
    std::optional<std::string> slug;
    std::optional<std::string> application_id;
    std::optional<std::string> device_id;
    std::optional<int> default_level;
    std::optional<std::vector<Action>> actions;
    std::optional<bool> is_active;
};

class Api {
public:
    Api(std::shared_ptr<HttpTransport> transport, std::string base_url, Credentials credentials);

    void set_credentials(Credentials credentials) { credentials_ = std::move(credentials); }
    const Credentials& credentials() const { return credentials_; }
    void set_base_url(std::string base_url);
    const std::string& base_url() const { return base_url_; }

    Result<SigninResult> signin(const std::string& email, const std::string& password);
    Result<SigninResult> two_factor_login(const std::string& challenge, const std::string& code);
    Result<Profile> profile();

    Result<MessagePage> messages(const MessageQuery& query);
    Result<Message> message(const std::string& tid);
    Result<Empty> set_read(const std::string& tid, bool read);
    Result<Empty> mark_all_read();
    Result<Empty> delete_message(const std::string& tid);
    Result<Empty> delete_all_messages();
    Result<MessageCreated> send(const MessageCreate& message);
    Result<std::vector<DayStat>> stats();
    Result<Usage> usage();

    Result<std::vector<ScheduledMessage>> scheduled(const std::string& status);
    Result<Empty> cancel_scheduled(const std::string& tid);

    Result<std::vector<Application>> applications();
    Result<Application> create_application(const ApplicationInput& input);
    Result<Empty> update_application(const std::string& tid, const ApplicationInput& input);
    Result<Empty> delete_application(const std::string& tid);

    Result<std::vector<Device>> devices();
    Result<Device> device_by_serial(const std::string& serial);
    // Returns the new device's tid.
    Result<std::string> register_device(const std::string& serial, const std::string& name, const std::string& type,
                                        const std::string& version);
    Result<Empty> rename_device(const std::string& tid, const std::string& name);
    Result<Empty> delete_device(const std::string& tid);

    Result<std::vector<Webhook>> webhooks();
    Result<Webhook> create_webhook(const WebhookInput& input);
    Result<Empty> update_webhook(const std::string& tid, const WebhookInput& input);
    Result<Empty> delete_webhook(const std::string& tid);
    Result<WebhookHistory> webhook_history(const std::string& tid, int page, int per);

    Result<std::vector<Form>> forms();
    Result<Empty> delete_form(const std::string& tid);

    Result<std::vector<ApiKey>> api_keys();
    Result<ApiKey> create_api_key(const std::string& name);
    Result<Empty> delete_api_key(const std::string& tid);

    // Raw request to an arbitrary URL for message action buttons: no
    // credentials, redirects allowed, small response cap.
    HttpResponse run_external(HttpRequest request);

private:
    struct Raw {
        HttpResponse response;
        ApiError error;
        bool ok = false;
        json body;
    };

    Raw call(const std::string& method, const std::string& path, const json* body = nullptr, bool authenticated = true);

    template <typename T>
    Result<T> decode(const std::string& method, const std::string& path, const json* body = nullptr,
                     bool authenticated = true);
    Result<Empty> command(const std::string& method, const std::string& path, const json* body = nullptr);

    std::shared_ptr<HttpTransport> transport_;
    std::string base_url_;
    Credentials credentials_;
};

std::string path_escape(const std::string& segment);

} // namespace pa
