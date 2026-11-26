#include "api.h"
#include "config.h"
#include "demo_backend.h"
#include "i18n.h"
#include "notify.h"
#include "parse.h"
#include "worker.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {

int g_failed = 0;
int g_checks = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_checks;                                                                 \
        if (!(cond)) {                                                              \
            ++g_failed;                                                             \
            std::fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        ++g_checks;                                                                                 \
        const auto va = (a);                                                                        \
        const auto vb = (b);                                                                        \
        if (!(va == vb)) {                                                                          \
            ++g_failed;                                                                             \
            std::fprintf(stderr, "  FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b);           \
        }                                                                                           \
    } while (0)

struct Test {
    const char* name;
    std::function<void()> body;
};

std::vector<Test>& tests() {
    static std::vector<Test> all;
    return all;
}

struct Register {
    Register(const char* name, std::function<void()> body) { tests().push_back({name, std::move(body)}); }
};

#define TEST(name)                                     \
    void name();                                       \
    const Register register_##name(#name, name);       \
    void name()

// Records requests and replays canned responses, to pin down the wire format.
class RecordingTransport final : public pa::HttpTransport {
public:
    std::vector<pa::HttpRequest> requests;
    pa::HttpResponse next{200, "{}", {}, ""};

    pa::HttpResponse send(const pa::HttpRequest& request) override {
        requests.push_back(request);
        return next;
    }
};

std::string header(const pa::HttpRequest& request, const std::string& name) {
    for (const auto& [key, value] : request.headers) {
        if (key == name) return value;
    }
    return {};
}

// A private config dir for one test; removed again when the test ends.
struct TempConfigDir {
    std::filesystem::path dir;
    explicit TempConfigDir(const std::string& name)
        : dir(std::filesystem::temp_directory_path() / ("pa-test-" + name + "-" + std::to_string(::getpid()))) {
        std::filesystem::remove_all(dir);
        setenv("PA_CARDPUTER_CONFIG_DIR", dir.c_str(), 1);
    }
    ~TempConfigDir() {
        std::filesystem::remove_all(dir);
        unsetenv("PA_CARDPUTER_CONFIG_DIR");
    }
};

pa::Api demo_api() {
    return pa::Api(std::make_shared<pa::DemoTransport>(0, false), "https://api.example", pa::Credentials{"demo", ""});
}

// ---------------------------------------------------------------- parse

TEST(levels_accept_names_aliases_and_numbers) {
    CHECK_EQ(*pa::parse_level("critical"), 2);
    CHECK_EQ(*pa::parse_level(" Urgent "), 2);
    CHECK_EQ(*pa::parse_level("min"), -2);
    CHECK_EQ(*pa::parse_level("normal"), 0);
    CHECK_EQ(*pa::parse_level("-1"), -1);
    CHECK(!pa::parse_level("3"));
    CHECK(!pa::parse_level("loud"));
    CHECK_EQ(pa::level_name(1), std::string("high"));
}

TEST(actions_follow_cli_grammar) {
    pa::ParseError error;
    auto view = pa::parse_action("view:Open:https://status.example.com", false, error);
    CHECK(view && view->type == "view" && view->label == "Open");
    CHECK(!pa::parse_action("view:Open:not-a-url", false, error));

    auto post = pa::parse_action("http:Restart:https://ci.example.com/restart", false, error);
    CHECK(post);
    const auto config = pa::parse_http_action(post->value);
    CHECK(config && config->method == "POST" && config->url == "https://ci.example.com/restart");

    auto get = pa::parse_action("http:Ack:GET https://example.com/ack", false, error);
    CHECK(get && pa::parse_http_action(get->value)->method == "GET");

    auto copy = pa::parse_action("copy:Code:12:34", false, error);
    CHECK(copy && copy->value == "12:34");

    CHECK(!pa::parse_action("ping:x:y", false, error));
    CHECK(!pa::parse_action("view:Open", false, error));
    CHECK(pa::parse_action("view:Panel:%ruleUrl%", true, error));
    CHECK_EQ(pa::format_action_spec(*get), std::string("http:Ack:GET https://example.com/ack"));
}

TEST(http_action_defaults_to_get_like_the_mobile_app) {
    const auto bare = pa::parse_http_action("https://example.com/hook");
    CHECK(bare && bare->method == "GET");
    const auto json = pa::parse_http_action(R"({"url":"https://x.example/a","method":"put","headers":{"X-A":"1"},"body":{"k":2}})");
    CHECK(json && json->method == "PUT" && json->headers.at("X-A") == "1" && json->body == R"({"k":2})");
    CHECK(!pa::parse_http_action(R"({"method":"POST"})"));
}

TEST(delays_convert_days_to_go_durations) {
    pa::ParseError error;
    CHECK_EQ(*pa::normalize_delay("1d", error), std::string("24h0m0s"));
    CHECK_EQ(*pa::normalize_delay("1d12h", error), std::string("36h0m0s"));
    CHECK_EQ(*pa::normalize_delay("30m", error), std::string("30m0s"));
    CHECK_EQ(*pa::normalize_delay("90", error), std::string("90"));
    CHECK_EQ(*pa::normalize_delay("45s", error), std::string("45s"));
    CHECK(!pa::normalize_delay("soon", error));
    CHECK(!pa::normalize_delay("0m", error));
}

// ---------------------------------------------------------------- models

TEST(models_tolerate_nulls_and_string_numbers) {
    const auto j = pa::json::parse(R"({
        "tid":"m1","title":"T","message":"B","application":null,"is_read":"true",
        "priority":"2","actions":"[{\"type\":\"view\",\"label\":\"L\",\"value\":\"https://a.b\"}]",
        "attachment":{"tid":"a1","file_name":"x.jpg","file_size":"2048","link":"https://cdn/x.jpg"}
    })");
    const auto m = j.get<pa::Message>();
    CHECK_EQ(m.priority, 2);
    CHECK(m.is_read);
    CHECK(m.application.empty());
    CHECK_EQ(m.actions.size(), static_cast<size_t>(1));
    CHECK(m.attachment && m.attachment->file_size == 2048);

    const auto usage = pa::json::parse(R"({"used":5,"limit":null,"unlimited":true})").get<pa::Usage>();
    CHECK(!usage.limit && usage.unlimited);
}

// ---------------------------------------------------------------- api wire format

TEST(api_key_goes_in_token_header) {
    auto transport = std::make_shared<RecordingTransport>();
    transport->next.body = R"({"data":[],"meta":{"total":0,"has_more":false,"next_offset":0}})";
    pa::Api api(transport, "https://api.example/", pa::Credentials{"secret", ""});
    pa::MessageQuery query;
    query.size = 20;
    query.application_tid = "app 1";
    const auto page = api.messages(query);
    CHECK(page.ok);
    CHECK_EQ(transport->requests.size(), static_cast<size_t>(1));
    const auto& r = transport->requests[0];
    CHECK_EQ(r.url, std::string("https://api.example/v2/messages?size=20&offset=0&sort=desc&application=app%201"));
    CHECK_EQ(header(r, "Token"), std::string("secret"));
    CHECK(header(r, "Authorization").empty());
}

TEST(jwt_goes_in_bearer_header) {
    auto transport = std::make_shared<RecordingTransport>();
    transport->next.body = R"({"tid":"k","name":"Cardputer Zero","token":"pa_x"})";
    pa::Api api(transport, "", pa::Credentials{"", "jwt-1"});
    const auto key = api.create_api_key("Cardputer Zero");
    CHECK(key.ok && key.value.token == "pa_x");
    CHECK_EQ(header(transport->requests[0], "Authorization"), std::string("Bearer jwt-1"));
    CHECK_EQ(transport->requests[0].url, std::string("https://api.pocketalert.app/v1/apikeys"));
}

TEST(send_serializes_only_set_fields) {
    auto transport = std::make_shared<RecordingTransport>();
    transport->next.status = 201;
    transport->next.body = R"({"tid":"s1","scheduled":true,"status":"pending","deliver_at":"01.12.2026 09:00:00"})";
    pa::Api api(transport, "", pa::Credentials{"k", ""});
    pa::MessageCreate m;
    m.title = "Hi";
    m.message = "There";
    m.level = 1;
    m.delay = "30m0s";
    m.actions.push_back({"view", "Open", "https://a.example"});
    const auto created = api.send(m);
    CHECK(created.ok && created.value.scheduled);
    const auto body = pa::json::parse(transport->requests[0].body);
    CHECK_EQ(body["level"].get<int>(), 1);
    CHECK_EQ(body["delay"].get<std::string>(), std::string("30m0s"));
    CHECK(!body.contains("application_id"));
    CHECK(!body.contains("send_at"));
    CHECK_EQ(body["actions"][0]["label"].get<std::string>(), std::string("Open"));
}

TEST(webhook_actions_are_sent_as_a_json_string) {
    auto transport = std::make_shared<RecordingTransport>();
    pa::Api api(transport, "", pa::Credentials{"k", ""});
    pa::WebhookInput input;
    input.actions = std::vector<pa::Action>{{"view", "Panel", "%url%"}};
    input.is_active = false;
    CHECK(api.update_webhook("w1", input).ok);
    const auto body = pa::json::parse(transport->requests[0].body);
    CHECK(body["actions"].is_string());
    CHECK_EQ(body["is_active"].get<bool>(), false);
    CHECK(!body.contains("name"));
}

TEST(errors_keep_limit_details) {
    auto transport = std::make_shared<RecordingTransport>();
    transport->next.status = 429;
    transport->next.body = R"({"error":"limit","limit":100,"used":100,"resets_at":"2026-11-26T00:00:00Z"})";
    pa::Api api(transport, "", pa::Credentials{"k", ""});
    const auto r = api.send(pa::MessageCreate{"a", "b"});
    CHECK(!r.ok);
    CHECK_EQ(r.error.limit, 100);
    CHECK_EQ(r.error.text(), std::string("Daily limit reached (100/100)"));

    transport->next.status = 0;
    transport->next.transport_error = "Could not resolve host";
    const auto offline = api.profile();
    CHECK(offline.error.network());
}

// ---------------------------------------------------------------- demo backend

TEST(demo_backend_supports_the_full_message_flow) {
    auto api = demo_api();
    pa::MessageQuery query;
    query.size = 10;
    auto first = api.messages(query);
    CHECK(first.ok && first.value.data.size() == 10 && first.value.has_more);
    query.offset = first.value.next_offset;
    auto second = api.messages(query);
    CHECK(second.ok && second.value.data.front().tid != first.value.data.front().tid);

    const auto tid = first.value.data.front().tid;
    CHECK(api.set_read(tid, false).ok);
    CHECK(api.message(tid).ok);
    query.offset = 0;
    CHECK(api.messages(query).value.data.front().is_read);

    pa::MessageCreate m{"From test", "Body"};
    CHECK(api.send(m).ok);
    CHECK_EQ(api.messages(query).value.data.front().title, std::string("From test"));
    CHECK(api.delete_message(api.messages(query).value.data.front().tid).ok);

    m.delay = "30m0s";
    const auto scheduled = api.send(m);
    CHECK(scheduled.ok && scheduled.value.scheduled);
    CHECK(api.cancel_scheduled(scheduled.value.tid).ok);
    CHECK(!api.cancel_scheduled(scheduled.value.tid).ok);

    CHECK(api.mark_all_read().ok);
    CHECK(api.delete_all_messages().ok);
    CHECK(api.messages(query).value.data.empty());
}

TEST(demo_backend_resources) {
    auto api = demo_api();
    const auto apps = api.applications();
    CHECK(apps.ok && apps.value.size() == 4);
    pa::MessageQuery query;
    query.application_tid = apps.value[0].tid;
    for (const auto& m : api.messages(query).value.data) CHECK_EQ(m.application, apps.value[0].name);

    pa::ApplicationInput input;
    input.name = "Test app";
    const auto created = api.create_application(input);
    CHECK(created.ok && created.value.color == "#3B82F6");
    CHECK(api.delete_application(created.value.tid).ok);

    const auto hooks = api.webhooks();
    CHECK(hooks.ok && !hooks.value.empty());
    CHECK(api.webhook_history(hooks.value[0].tid, 1, 50).value.requests.size() == 6);
    const auto key = api.create_api_key("Laptop");
    CHECK(key.ok && !key.value.token.empty());
    CHECK(api.api_keys().value.back().token.empty());
    CHECK(api.usage().value.limit.has_value());
    CHECK_EQ(api.stats().value.size(), static_cast<size_t>(7));
    CHECK(api.profile().value.email == "demo@pocketalert.app");
}

// ---------------------------------------------------------------- notifications

pa::Message message(const std::string& tid, bool read, int priority = 0) {
    pa::Message m;
    m.tid = tid;
    m.is_read = read;
    m.priority = priority;
    return m;
}

TEST(first_poll_records_without_alerting) {
    pa::NotifyState state;
    const auto d = pa::detect_new({message("a", false), message("b", true)}, state);
    CHECK(d.fresh.empty());
    CHECK_EQ(d.unread, 1);
    CHECK_EQ(state.seen_tids.size(), static_cast<size_t>(2));
}

TEST(later_polls_alert_once_per_unread_message) {
    pa::NotifyState state;
    pa::detect_new({message("a", false)}, state);
    auto d = pa::detect_new({message("c", false, 2), message("x", true), message("a", false)}, state);
    CHECK_EQ(d.fresh.size(), static_cast<size_t>(1));
    CHECK_EQ(d.fresh[0].tid, std::string("c"));
    CHECK_EQ(pa::loudest_level(d.fresh), 2);
    d = pa::detect_new({message("c", false, 2), message("a", false)}, state);
    CHECK(d.fresh.empty());
}

TEST(seen_history_is_bounded) {
    pa::NotifyState state;
    std::vector<pa::Message> batch;
    for (int i = 0; i < 400; ++i) batch.push_back(message("t" + std::to_string(i), true));
    pa::detect_new(batch, state);
    CHECK_EQ(state.seen_tids.size(), pa::kSeenHistory);
}

// ---------------------------------------------------------------- config

TEST(config_round_trip_is_private) {
    const auto dir = std::filesystem::temp_directory_path() / ("pa-test-" + std::to_string(::getpid()));
    setenv("PA_CARDPUTER_CONFIG_DIR", dir.c_str(), 1);
    pa::Config config;
    config.api_key = "k-123";
    config.poll_seconds = 30;
    config.theme = "lcd";
    config.device_tid = "dev-1";
    CHECK(pa::save_config(config));
    pa::Config loaded;
    CHECK(pa::load_config(loaded));
    CHECK_EQ(loaded.api_key, std::string("k-123"));
    CHECK_EQ(loaded.device_tid, std::string("dev-1"));
    CHECK_EQ(loaded.poll_seconds, 30);
    CHECK_EQ(loaded.theme, std::string("lcd"));
    const auto perms = std::filesystem::status(pa::config_path()).permissions();
    CHECK((perms & std::filesystem::perms::group_read) == std::filesystem::perms::none);

    pa::NotifyState state;
    state.seen_tids = {"a", "b"};
    state.unread = 3;
    CHECK(pa::save_state(state));
    pa::NotifyState back;
    CHECK(pa::load_state(back) && back.unread == 3 && back.seen_tids.size() == 2);
    std::filesystem::remove_all(dir);
    unsetenv("PA_CARDPUTER_CONFIG_DIR");
}

TEST(hardware_id_is_hashed_and_stable) {
    const auto id = pa::hardware_id();
    if (!id.empty()) {
        CHECK_EQ(id.size(), static_cast<size_t>(20));
        CHECK_EQ(id.rfind("cp0-", 0), static_cast<size_t>(0));
        CHECK_EQ(id, pa::hardware_id());
    }
}


// ---------------------------------------------------------------- input validation

TEST(http_actions_reject_request_splitting) {
    CHECK(!pa::parse_http_action(R"({"url":"https://x.example","method":"TRACE"})"));
    CHECK(!pa::parse_http_action(R"({"url":"https://x.example","method":"GET\r\nX: y"})"));
    CHECK(!pa::parse_http_action(R"({"url":"https://x.example","headers":{"X-A":"1\r\nHost: evil"}})"));
    CHECK(!pa::parse_http_action(R"({"url":"https://x.example","headers":{"Bad Name":"1"}})"));
    CHECK(!pa::parse_http_action(R"({"url":"https://x.example","headers":{"X:Y":"1"}})"));
    CHECK(!pa::parse_http_action("file:///etc/passwd"));
    CHECK(!pa::parse_http_action(R"({"url":"ftp://x.example/a"})"));
    const auto patch = pa::parse_http_action(R"({"url":"https://x.example","method":"patch","headers":{"X-Id":7}})");
    CHECK(patch && patch->method == "PATCH" && patch->headers.at("X-Id") == "7");
}

TEST(base_url_requires_https_except_localhost) {
    CHECK(pa::is_valid_base_url("https://api.pocketalert.app"));
    CHECK(pa::is_valid_base_url("https://pa.example.com:8443/api"));
    CHECK(pa::is_valid_base_url("http://localhost:8080"));
    CHECK(pa::is_valid_base_url("http://127.0.0.1"));
    CHECK(!pa::is_valid_base_url("http://api.pocketalert.app"));
    CHECK(!pa::is_valid_base_url("http://localhost.evil.com"));
    CHECK(!pa::is_valid_base_url("ftp://api.example"));
    CHECK(!pa::is_valid_base_url("https://"));
    CHECK(!pa::is_valid_base_url("https://a b"));

    auto transport = std::make_shared<RecordingTransport>();
    pa::Api api(transport, "http://api.example", pa::Credentials{"k", ""});
    CHECK_EQ(api.base_url(), std::string(pa::kDefaultBaseUrl));
}

TEST(parse_int_is_strict) {
    CHECK_EQ(*pa::parse_int("15"), 15);
    CHECK_EQ(*pa::parse_int("-2"), -2);
    CHECK(!pa::parse_int(""));
    CHECK(!pa::parse_int("1x"));
    CHECK(!pa::parse_int(" 1"));
    CHECK(!pa::parse_int("99999999999999999999"));
}

TEST(delays_have_bounds) {
    pa::ParseError error;
    CHECK(!pa::normalize_delay("9999999999", error));
    CHECK(!pa::normalize_delay("99999999999999999999d", error));
    CHECK(!pa::normalize_delay("11000d", error));
    CHECK_EQ(*pa::normalize_delay("365d", error), std::string("8760h0m0s"));
    CHECK_EQ(*pa::normalize_delay("1.5h", error), std::string("1h30m0s"));
}

TEST(inbox_filter_matches_title_text_and_app) {
    std::vector<pa::Message> list(4);
    list[0].title = "Disk FULL";
    list[1].message = "backup done";
    list[1].is_read = true;
    list[2].application = "Billing";
    list[3].title = "other";
    CHECK_EQ(pa::filter_messages(list, false, "").size(), static_cast<size_t>(4));
    CHECK(pa::filter_messages(list, false, "full") == std::vector<size_t>{0});
    CHECK(pa::filter_messages(list, false, "BACKUP") == std::vector<size_t>{1});
    CHECK(pa::filter_messages(list, false, "bill") == std::vector<size_t>{2});
    CHECK(pa::filter_messages(list, true, "backup").empty());
    CHECK((pa::filter_messages(list, true, "") == std::vector<size_t>{0, 2, 3}));
}

// ---------------------------------------------------------------- wire details

TEST(query_and_path_encoding) {
    CHECK_EQ(pa::url_encode("a b/c?d&e=é"), std::string("a%20b%2Fc%3Fd%26e%3D%C3%A9"));
    CHECK_EQ(pa::build_query({{"a", "1"}, {"skip", ""}, {"b", "x y"}}), std::string("?a=1&b=x%20y"));
    CHECK(pa::build_query({{"a", ""}}).empty());
}

TEST(error_text_covers_each_case) {
    pa::ApiError e;
    CHECK_EQ(e.text(), std::string("No connection"));
    e.message = "timeout";
    CHECK_EQ(e.text(), std::string("No connection: timeout"));
    e = {};
    e.status = 404;
    CHECK_EQ(e.text(), std::string("Not found"));
    e.status = 500;
    CHECK_EQ(e.text(), std::string("HTTP 500"));
    e.message = "Bad";
    e.details = "field x";
    CHECK_EQ(e.text(), std::string("Bad: field x"));
    e = {};
    e.status = 429;
    CHECK_EQ(e.text(), std::string("Too many requests"));
}

TEST(malformed_responses_fail_cleanly) {
    auto transport = std::make_shared<RecordingTransport>();
    pa::Api api(transport, "", pa::Credentials{"k", ""});
    transport->next.body = R"({"data":"oops"})";
    CHECK(!api.applications().ok);
    transport->next.body = "not json \xff";
    CHECK(!api.profile().ok);
    transport->next.body = R"({"data":[{"tid":"m","title":"x"}],"meta":{"total":"7","has_more":true,"next_offset":25}})";
    const auto page = api.messages({});
    CHECK(page.ok && page.value.total == 7 && page.value.has_more && page.value.next_offset == 25);
}

TEST(invalid_utf8_in_user_text_is_sent_not_thrown) {
    auto transport = std::make_shared<RecordingTransport>();
    pa::Api api(transport, "", pa::Credentials{"k", ""});
    pa::MessageCreate m{"bad \xc3", "text \xff"};
    api.send(m);
    CHECK_EQ(transport->requests.size(), static_cast<size_t>(1));
    CHECK(!pa::json::parse(transport->requests[0].body, nullptr, false).is_discarded());
}

TEST(api_calls_never_follow_redirects_but_actions_may) {
    auto transport = std::make_shared<RecordingTransport>();
    pa::Api api(transport, "", pa::Credentials{"k", ""});
    api.profile();
    CHECK(!transport->requests.back().follow_redirects);
    pa::HttpRequest external;
    external.url = "https://hooks.example/a";
    api.run_external(external);
    CHECK(transport->requests.back().follow_redirects);
    CHECK(transport->requests.back().max_body_bytes <= 256U * 1024U);
    CHECK(header(transport->requests.back(), "Token").empty());
}

TEST(curl_refuses_non_http_schemes) {
    pa::CurlTransport curl("pa-test");
    pa::HttpRequest request;
    request.url = "file:///etc/hosts";
    const auto response = curl.send(request);
    CHECK_EQ(response.status, 0L);
    CHECK(!response.transport_error.empty());
    CHECK(response.body.empty());
}

TEST(demo_backend_answers_bad_bodies_with_400) {
    auto transport = std::make_shared<pa::DemoTransport>(0, false);
    pa::HttpRequest request;
    request.method = "POST";
    request.url = "https://api.example/v1/messages";
    request.body = R"({"title":5,"message":[]})";
    CHECK_EQ(transport->send(request).status, 400L);
    request.body = R"([1])";
    CHECK_EQ(transport->send(request).status, 400L);
}

// ---------------------------------------------------------------- files and locks

TEST(state_is_compared_before_writing) {
    pa::NotifyState state;
    pa::detect_new({message("a", false)}, state);
    const auto before = state;
    pa::detect_new({message("a", false)}, state);
    CHECK(state == before);
    pa::detect_new({message("b", false), message("a", false)}, state);
    CHECK(state != before);
}

TEST(broken_state_file_is_ignored) {
    TempConfigDir temp("state");
    std::filesystem::create_directories(temp.dir);
    std::ofstream(pa::state_path()) << "{ not json";
    pa::NotifyState state;
    state.unread = 9;
    CHECK(!pa::load_state(state));
    CHECK_EQ(state.unread, 9);
    std::ofstream(pa::state_path()) << R"({"seen_tids":"wrong type","unread":2})";
    CHECK(pa::load_state(state));
    CHECK(state.seen_tids.empty());
    CHECK_EQ(state.unread, 2);
}

TEST(atomic_write_replaces_links_and_keeps_mode) {
    TempConfigDir temp("atomic");
    std::filesystem::create_directories(temp.dir);
    const auto victim = temp.dir / "victim";
    std::ofstream(victim) << "keep";
    const auto path = (temp.dir / "config.json").string();
    std::filesystem::create_symlink(victim, path);
    const auto old_umask = ::umask(0);
    CHECK(pa::write_file_atomic(path, "new", 0600));
    ::umask(old_umask);
    std::string kept;
    std::getline(std::ifstream(victim), kept);
    CHECK_EQ(kept, std::string("keep"));
    CHECK(!std::filesystem::is_symlink(path));
    struct stat info{};
    CHECK(::stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
    size_t leftovers = 0;
    for (const auto& entry : std::filesystem::directory_iterator(temp.dir)) {
        if (entry.path().filename().string().find(".tmp.") != std::string::npos) ++leftovers;
    }
    CHECK_EQ(leftovers, static_cast<size_t>(0));
}

TEST(config_ignores_legacy_server_and_clamps_screen_timeouts) {
    TempConfigDir temp("cfg");
    std::filesystem::create_directories(temp.dir);
    std::ofstream(pa::config_path())
        << R"({"api_key":"k","base_url":"http://api.example","screen_dim_seconds":-5,"screen_off_seconds":99999})";
    pa::Config config;
    CHECK(pa::load_config(config));
    CHECK_EQ(config.api_key, std::string("k"));
    CHECK_EQ(config.screen_dim_seconds, 0);
    CHECK_EQ(config.screen_off_seconds, 3600);
}

TEST(ui_lock_is_held_only_while_alive) {
    TempConfigDir temp("lock");
    CHECK(!pa::UiLock::held_by_live_process());
    {
        pa::UiLock lock;
        CHECK(pa::UiLock::held_by_live_process());
    }
    CHECK(!pa::UiLock::held_by_live_process());
}

// ---------------------------------------------------------------- worker

bool wait_readable(int fd, int timeout_ms) {
    pollfd p{fd, POLLIN, 0};
    return ::poll(&p, 1, timeout_ms) > 0;
}

TEST(worker_runs_jobs_in_order_and_wakes_the_loop) {
    std::vector<int> order;
    {
        ui::Worker worker;
        auto post = worker.ui_poster();
        for (int i = 0; i < 5; ++i) {
            worker.post([post, i, &order] { post([i, &order] { order.push_back(i); }); });
        }
        worker.post([] { throw std::runtime_error("boom"); });
        worker.post([post, &order] { post([&order] { order.push_back(99); }); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (order.size() < 6 && std::chrono::steady_clock::now() < deadline) {
            if (wait_readable(worker.wake_fd(), 100)) worker.drain_ui();
        }
    }
    CHECK((order == std::vector<int>{0, 1, 2, 3, 4, 99}));
}

TEST(worker_poster_outlives_the_worker) {
    ui::Worker::UiPoster post;
    {
        ui::Worker worker;
        post = worker.ui_poster();
    }
    bool ran = false;
    post([&ran] { ran = true; });
    CHECK(!ran);
}

// ---------------------------------------------------------------- i18n

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

TEST(i18n_falls_back_to_english_then_key) {
    TempConfigDir temp("i18n");
    write_text(temp.dir / "en" / "inbox.json", R"({"title":"Inbox","keys":{"new":"New"},"unread":"{n} unread"})");
    write_text(temp.dir / "de" / "inbox.json", R"({"title":"Posteingang","keys":{"new":""}})");
    CHECK(pa::i18n::load(temp.dir, "de"));
    CHECK_EQ(pa::i18n::current(), std::string("de"));
    CHECK_EQ(std::string(pa::i18n::tr("inbox.title")), std::string("Posteingang"));
    CHECK_EQ(std::string(pa::i18n::tr("inbox.keys.new")), std::string("New"));
    CHECK_EQ(pa::i18n::tr("inbox.unread", {{"n", "5"}}), std::string("5 unread"));
    CHECK_EQ(std::string(pa::i18n::tr("inbox.nope")), std::string("inbox.nope"));
    CHECK(pa::i18n::load(temp.dir, "xx"));
    CHECK_EQ(pa::i18n::current(), std::string("en"));
    CHECK(!pa::i18n::load(temp.dir / "missing", "en"));
}

TEST(i18n_format_leaves_unknown_and_unclosed_braces) {
    CHECK_EQ(pa::i18n::format("{a} and {b} {c", {{"a", "1"}}), std::string("1 and {b} {c"));
    CHECK_EQ(pa::i18n::format("{n}{n}", {{"n", "x"}}), std::string("xx"));
}

TEST(i18n_language_comes_from_locale_env) {
    CHECK_EQ(pa::i18n::from_locale_env("de_DE.UTF-8"), std::string("de"));
    CHECK_EQ(pa::i18n::from_locale_env("pt_BR"), std::string("pt"));
    CHECK_EQ(pa::i18n::from_locale_env("zh_CN.UTF-8"), std::string("zh"));
    CHECK_EQ(pa::i18n::from_locale_env("nl_NL.UTF-8"), std::string("en"));
    CHECK_EQ(pa::i18n::resolve("ja"), std::string("ja"));
    CHECK_EQ(pa::i18n::resolve("xx"), std::string("en"));
}

std::set<std::string> placeholders(const std::string& text) {
    std::set<std::string> names;
    for (size_t open = text.find('{'); open != std::string::npos; open = text.find('{', open + 1)) {
        const auto close = text.find('}', open);
        if (close == std::string::npos) break;
        names.insert(text.substr(open + 1, close - open - 1));
    }
    return names;
}

void collect_strings(const nlohmann::json& node, const std::string& prefix, std::map<std::string, std::string>& out) {
    if (node.is_string()) {
        out[prefix] = node.get<std::string>();
        return;
    }
    for (const auto& [key, value] : node.items()) collect_strings(value, prefix + "." + key, out);
}

std::map<std::string, std::string> dictionary(const std::filesystem::path& dir) {
    std::map<std::string, std::string> out;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        std::ifstream in(entry.path());
        const auto parsed = nlohmann::json::parse(in, nullptr, false);
        if (parsed.is_discarded()) {
            out["<invalid json> " + entry.path().string()] = "";
            continue;
        }
        collect_strings(parsed, entry.path().stem().string(), out);
    }
    return out;
}

TEST(shipped_translations_match_english_keys_and_placeholders) {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "i18n";
    const auto english = dictionary(root / "en");
    CHECK(!english.empty());
    for (const auto& language : pa::i18n::languages()) {
        if (std::string(language.code) == "en" || !std::filesystem::exists(root / language.code)) continue;
        for (const auto& [key, text] : dictionary(root / language.code)) {
            const auto source = english.find(key);
            if (source == english.end()) {
                std::printf("  %s: unknown key %s\n", language.code, key.c_str());
                CHECK(false);
                continue;
            }
            if (placeholders(source->second) != placeholders(text)) {
                std::printf("  %s: placeholders differ in %s\n", language.code, key.c_str());
                CHECK(false);
            }
        }
    }
}

} // namespace

int main() {
    for (const auto& test : tests()) {
        const int before = g_failed;
        test.body();
        std::printf("%s %s\n", g_failed == before ? "ok  " : "FAIL", test.name);
    }
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
