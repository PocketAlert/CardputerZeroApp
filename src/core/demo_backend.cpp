#include "demo_backend.h"

#include "models.h"
#include "parse.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <map>
#include <mutex>
#include <random>
#include <thread>

namespace pa {
namespace {

using Clock = std::chrono::system_clock;

std::string format_time(Clock::time_point when) {
    const std::time_t t = Clock::to_time_t(when);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M:%S", &tm);
    return buffer;
}

std::string format_utc(Clock::time_point when) {
    const std::time_t t = Clock::to_time_t(when);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string format_date(Clock::time_point when) {
    const std::time_t t = Clock::to_time_t(when);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm);
    return buffer;
}

std::string url_decode(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            out.push_back(static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else if (text[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

HttpResponse reply(long status, const json& body) {
    HttpResponse response;
    response.status = status;
    response.body = body.dump();
    return response;
}

HttpResponse success() {
    return reply(200, json{{"data", "success"}});
}

HttpResponse not_found() {
    return reply(404, json{{"error", "Not found"}});
}

struct Template {
    int priority;
    const char* app;
    const char* title;
    const char* body;
    std::vector<Action> actions;
    const char* attachment;
};

const std::vector<Template>& seed_messages() {
    static const std::vector<Template> seed = {
        {2, "Production", "API p95 latency 2.4s",
         "api-eu-1: p95 latency above 2s for 5 minutes.\nError rate 3.1%, upstream postgres-primary is slow.\n"
         "Runbook: check connection pool saturation first, then long-running queries.",
         {{"view", "Grafana", "https://grafana.example.com/d/api-latency"},
          {"http", "Restart", R"({"method":"POST","url":"https://deploy.example.com/restart/api-eu-1"})"},
          {"copy", "Host", "api-eu-1.internal"}},
         nullptr},
        {1, "CI / CD", "Deploy failed: web@a1c9f2e",
         "Job build-and-push exited with code 1 after 3m12s.\n\nnpm ERR! Missing script: \"build\"\nnpm ERR! A complete log "
         "of this run can be found in /root/.npm/_logs.",
         {{"view", "Open run", "https://github.com/example/web/actions/runs/1042"},
          {"http", "Retry", R"({"method":"POST","url":"https://ci.example.com/retry/1042"})"}},
         nullptr},
        {0, "Home Lab", "Doorbell: motion detected", "Front door camera, 3 frames. Person detected with 92% confidence.",
         {{"view", "Live view", "https://nvr.example.com/cam/front"}},
         "https://cdn.example.com/u/demo/front-door.jpg"},
        {0, "Home Lab", "Server rebooted",
         "pve-01 came back 2 minutes after a power cut. The UPS carried the load for 6 minutes and every VM started again.",
         {}, nullptr},
        {0, "Payments", "New subscription: Pro yearly", "Order #10482, $96.00. Trial converted after 14 days.",
         {{"view", "Dashboard", "https://app.lemonsqueezy.com/orders/10482"}}, nullptr},
        {1, "Production", "SSL certificate expires in 7 days",
         "api.example.com, issued by Let's Encrypt R11. Auto-renewal failed twice: DNS challenge timeout.",
         {{"copy", "Domain", "api.example.com"}}, nullptr},
        {-1, "Home Lab", "Disk 81% full", "/dev/sda1 on pve-01: 162 GB of 200 GB used. Largest: /var/lib/vz/dump.", {},
         nullptr},
        {-2, "CI / CD", "Nightly report", "412 tests passed, 0 failed. Coverage 87.4% (+0.3%). Duration 6m48s.", {},
         nullptr},
        {0, "Production", "Queue drained", "email-worker processed 18,240 jobs in 4m. Backlog is back to zero.", {},
         nullptr},
        {1, "Payments", "Payment failed", "Card declined for customer cus_8f2k. Retry scheduled in 3 days.",
         {{"view", "Customer", "https://dashboard.stripe.com/customers/cus_8f2k"}}, nullptr},
    };
    return seed;
}

const std::vector<Template>& incoming_messages() {
    static const std::vector<Template> incoming = {
        {1, "Production", "CPU 95% on worker-3", "worker-3 at 95% CPU for 10 minutes. Load average 7.8 on 4 cores.",
         {{"view", "Grafana", "https://grafana.example.com/d/workers"},
          {"http", "Scale up", R"({"method":"POST","url":"https://deploy.example.com/scale/worker"})"}},
         nullptr},
        {0, "Payments", "Payment received: $12.00", "Monthly Pro plan renewed for team acme.", {}, nullptr},
        {2, "Production", "Uptime check failed", "https://status.example.com returned 502 from 3 regions.",
         {{"view", "Status", "https://status.example.com"}, {"copy", "Incident", "INC-2291"}}, nullptr},
        {-1, "CI / CD", "PR #412 merged", "feat: offline queue for outgoing alerts. 14 files changed.", {}, nullptr},
    };
    return incoming;
}

} // namespace

struct DemoTransport::State {
    std::mutex mutex;
    std::mt19937 rng{std::random_device{}()};
    Clock::time_point started = Clock::now();
    Clock::time_point last_incoming = Clock::now();
    int incoming_every = 40;
    bool latency = true;
    size_t incoming_index = 0;
    int sent_today = 137;

    std::vector<Message> messages;
    std::vector<ScheduledMessage> scheduled;
    std::vector<Application> applications;
    std::vector<Device> devices;
    std::vector<Webhook> webhooks;
    std::map<std::string, std::vector<json>> webhook_history;
    std::vector<Form> forms;
    std::vector<ApiKey> api_keys;

    std::string tid() {
        static const char* alphabet = "abcdefghijkmnopqrstuvwxyz23456789";
        std::uniform_int_distribution<int> pick(0, 32);
        std::string out;
        for (int i = 0; i < 12; ++i) out.push_back(alphabet[pick(rng)]);
        return out;
    }

    Message make_message(const Template& t, Clock::time_point when, bool read) {
        Message m;
        m.tid = tid();
        m.title = t.title;
        m.message = t.body;
        m.application = t.app;
        m.device = "All devices";
        m.is_read = read;
        m.priority = t.priority;
        m.actions = t.actions;
        m.created_at = format_time(when);
        if (t.attachment) {
            Attachment a;
            a.tid = tid();
            a.file_name = "front-door.jpg";
            a.content_type = "image/jpeg";
            a.file_size = 184320;
            a.link = t.attachment;
            m.attachment = a;
        }
        return m;
    }

    void seed() {
        const auto now = Clock::now();
        applications = {
            {tid(), "Production", "API, workers and queues", "#E5484D", 1, true, format_time(now - std::chrono::hours(24 * 90))},
            {tid(), "CI / CD", "GitHub Actions pipelines", "#3F5CAD", std::nullopt, true, format_time(now - std::chrono::hours(24 * 60))},
            {tid(), "Home Lab", "Proxmox, NAS, cameras", "#16A34A", std::nullopt, true, format_time(now - std::chrono::hours(24 * 30))},
            {tid(), "Payments", "Stripe and Lemon Squeezy", "#F59E0B", 0, true, format_time(now - std::chrono::hours(24 * 12))},
        };
        devices = {
            {tid(), "cp0-7f3a91", "Cardputer Zero", true, format_time(now - std::chrono::hours(2))},
            {tid(), "A1B2C3D4E5", "iPhone 15", true, format_time(now - std::chrono::hours(24 * 120))},
            {tid(), "F6E5D4C3B2", "Pixel 8", true, format_time(now - std::chrono::hours(24 * 40))},
        };

        const auto& seed = seed_messages();
        int minutes = 2;
        for (size_t round = 0; round < 3; ++round) {
            for (size_t i = 0; i < seed.size(); ++i) {
                const bool read = round > 0 || i >= 5;
                messages.push_back(make_message(seed[i], now - std::chrono::minutes(minutes), read));
                minutes += static_cast<int>(7 + i * 5 + round * 40);
            }
        }

        const auto tomorrow = now + std::chrono::hours(20);
        scheduled = {
            {tid(), "Standup in 15 minutes", "Daily sync, room 3 / Meet link in calendar.", 0, "pending",
             format_time(tomorrow), "Europe/Berlin", "CI / CD", "All devices", {}, format_time(now - std::chrono::hours(3))},
            {tid(), "Renew domain example.com", "Expires in 14 days, auto-renew is off.", 1, "pending",
             format_time(now + std::chrono::hours(24 * 6)), "Europe/Berlin", "Production", "iPhone 15", {},
             format_time(now - std::chrono::hours(26))},
            {tid(), "Backup verification", "Restore test for nas-01 snapshots.", 0, "sent",
             format_time(now - std::chrono::hours(5)), "Europe/Berlin", "Home Lab", "All devices", {},
             format_time(now - std::chrono::hours(30))},
            {tid(), "Promo email batch", "Black Friday campaign, segment B.", -1, "canceled",
             format_time(now - std::chrono::hours(48)), "Europe/Berlin", "Payments", "All devices", {},
             format_time(now - std::chrono::hours(72))},
        };

        webhooks = {
            {tid(), "Grafana alerts", "gr4f-k2x9", applications[0].tid, "", "%title%: %message%",
             R"([{"type":"view","label":"Panel","value":"%ruleUrl%"}])", 1, true, format_time(now - std::chrono::hours(24 * 50))},
            {tid(), "GitHub", "gh-7mxq", applications[1].tid, "", "*", "", std::nullopt, true,
             format_time(now - std::chrono::hours(24 * 20))},
            {tid(), "Old Zapier hook", "zap-old", "", "", "%message%", "", -1, false,
             format_time(now - std::chrono::hours(24 * 200))},
        };
        for (const auto& hook : webhooks) {
            auto& history = webhook_history[hook.tid];
            for (int i = 0; i < 6; ++i) {
                const bool failed = i == 2;
                history.push_back(json{{"id", 9000 - i},
                                       {"method", "POST"},
                                       {"source_ip", "34.120.54." + std::to_string(10 + i)},
                                       {"content_type", "application/json"},
                                       {"body", R"({"title":"High memory","message":"node-2 at 91%"})"},
                                       {"size", 52},
                                       {"status", failed ? "error" : "success"},
                                       {"error_code", failed ? "template_render" : ""},
                                       {"rendered_message", failed ? "" : "High memory: node-2 at 91%"},
                                       {"message_tid", failed ? "" : tid()},
                                       {"priority", 1},
                                       {"duration_ms", 40 + i * 7},
                                       {"created_at", format_time(now - std::chrono::minutes(30 * (i + 1)))}});
            }
        }

        forms = {
            {tid(), "Contact form", true, false, 12, format_time(now - std::chrono::hours(4)),
             format_time(now - std::chrono::hours(24 * 15))},
            {tid(), "Bug report", true, true, 3, format_time(now - std::chrono::hours(24 * 6)),
             format_time(now - std::chrono::hours(24 * 25))},
        };
        api_keys = {
            {tid(), "Cardputer Zero", "", true, format_time(now - std::chrono::hours(2))},
            {tid(), "CLI laptop", "", true, format_time(now - std::chrono::hours(24 * 33))},
            {tid(), "GitHub Actions", "", true, format_time(now - std::chrono::hours(24 * 70))},
        };
    }

    void deliver_incoming() {
        const auto now = Clock::now();
        if (incoming_every <= 0 || now - last_incoming < std::chrono::seconds(incoming_every)) {
            return;
        }
        last_incoming = now;
        const auto& pool = incoming_messages();
        messages.insert(messages.begin(), make_message(pool[incoming_index++ % pool.size()], now, false));
        ++sent_today;
    }

    std::string application_name(const std::string& tid_or_name) {
        for (const auto& app : applications) {
            if (app.tid == tid_or_name || app.name == tid_or_name) return app.name;
        }
        return "";
    }

    std::string device_name(const std::string& tid_value) {
        for (const auto& device : devices) {
            if (device.tid == tid_value) return device.name;
        }
        return "";
    }

    HttpResponse handle(const std::string& method, const std::string& path,
                        const std::map<std::string, std::string>& query, const json& body);
};

DemoTransport::DemoTransport(int incoming_every_seconds, bool simulate_latency) : state_(std::make_unique<State>()) {
    state_->incoming_every = incoming_every_seconds;
    state_->latency = simulate_latency;
    state_->seed();
}

DemoTransport::~DemoTransport() = default;

HttpResponse DemoTransport::send(const HttpRequest& request) {
    const auto scheme = request.url.find("://");
    const auto path_start = request.url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
    std::string path = path_start == std::string::npos ? "/" : request.url.substr(path_start);
    std::map<std::string, std::string> query;
    if (const auto q = path.find('?'); q != std::string::npos) {
        std::string rest = path.substr(q + 1);
        path = path.substr(0, q);
        size_t pos = 0;
        while (pos <= rest.size()) {
            const auto amp = rest.find('&', pos);
            const auto pair = rest.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            if (const auto eq = pair.find('='); eq != std::string::npos) {
                query[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
            }
            if (amp == std::string::npos) break;
            pos = amp + 1;
        }
    }

    if (state_->latency) {
        std::this_thread::sleep_for(std::chrono::milliseconds(120 + static_cast<int>(path.size()) * 3));
    }

    if (path.rfind("/v1/", 0) != 0 && path.rfind("/v2/", 0) != 0) {
        HttpResponse response;
        response.status = 200;
        response.body = "OK";
        return response;
    }

    json body = request.body.empty() ? json::object() : json::parse(request.body, nullptr, false);
    if (body.is_discarded()) {
        return reply(400, json{{"error", "Invalid JSON"}});
    }
    if (!body.is_object()) {
        return reply(400, json{{"error", "Body must be a JSON object"}});
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    try {
        return state_->handle(request.method, path, query, body);
    } catch (const json::exception& e) {
        // A real server answers a mistyped field with 400; so does the demo.
        return reply(400, json{{"error", e.what()}});
    }
}

HttpResponse DemoTransport::State::handle(const std::string& method, const std::string& path,
                                          const std::map<std::string, std::string>& query, const json& body) {
    deliver_incoming();
    const auto get = [&](const char* key) {
        const auto it = query.find(key);
        return it == query.end() ? std::string{} : it->second;
    };
    const auto tail = [&](const std::string& prefix) { return path.substr(prefix.size()); };
    const auto now = Clock::now();

    if (path == "/v1/auth/signin") {
        if (body.value("password", "") == "") {
            return reply(401, json{{"error", "invalid credentials"}});
        }
        if (body.value("password", "") == "2fa") {
            return reply(200, json{{"two_factor_required", true}, {"challenge", "demo-challenge"}});
        }
        return reply(200, json{{"token", "demo.jwt.token"}, {"name", "Demo"}, {"email", body.value("email", "")}});
    }
    if (path == "/v1/auth/2fa/login") {
        if (body.value("code", "") != "123456") {
            return reply(401, json{{"error", "Invalid code"}});
        }
        return reply(200, json{{"token", "demo.jwt.token"}, {"name", "Demo"}, {"email", "demo@pocketalert.app"}});
    }
    if (path == "/v1/auth/profile") {
        return reply(200, json{{"id", 4242},
                               {"username", "demo"},
                               {"email", "demo@pocketalert.app"},
                               {"timezone", "Europe/Berlin"},
                               {"email_verified", true},
                               {"subscription_status", "active"}});
    }

    if (path == "/v2/messages" && method == "GET") {
        std::vector<Message> filtered;
        const auto app_filter = get("application");
        const auto app_name = app_filter.empty() ? "" : application_name(app_filter);
        for (const auto& m : messages) {
            if (!app_filter.empty() && m.application != app_name) continue;
            filtered.push_back(m);
        }
        if (get("sort") == "asc") std::reverse(filtered.begin(), filtered.end());
        const int size = get("size").empty() ? 1000 : std::max(1, std::stoi(get("size")));
        const int offset = get("offset").empty() ? 0 : std::max(0, std::stoi(get("offset")));
        json data = json::array();
        for (int i = offset; i < static_cast<int>(filtered.size()) && i < offset + size; ++i) {
            data.push_back(filtered[static_cast<size_t>(i)]);
        }
        const int total = static_cast<int>(filtered.size());
        const bool has_more = offset + size < total;
        return reply(200, json{{"data", data},
                               {"meta", {{"total", total}, {"has_more", has_more}, {"next_offset", has_more ? offset + size : 0}}}});
    }
    if (path == "/v1/messages/read-all" && method == "PUT") {
        for (auto& m : messages) m.is_read = true;
        return success();
    }
    if (path == "/v1/messages/bulk" && method == "DELETE") {
        messages.clear();
        return success();
    }
    if (path == "/v1/messages/stat") {
        json out = json::array();
        const int counts[] = {41, 63, 38, 92, 77, 54, sent_today};
        for (int i = 6; i >= 0; --i) {
            out.push_back(json{{"date", format_date(now - std::chrono::hours(24 * i))}, {"count", counts[6 - i]}});
        }
        return reply(200, out);
    }
    if (path == "/v1/messages/usage") {
        return reply(200, json{{"used", sent_today},
                               {"limit", 1000},
                               {"unlimited", false},
                               {"rejected", 0},
                               {"resets_at", format_utc(now + std::chrono::hours(9))}});
    }
    if (path == "/v1/messages/scheduled" && method == "GET") {
        const auto status = get("status").empty() ? "pending" : get("status");
        json out = json::array();
        for (const auto& s : scheduled) {
            if (status == "all" || s.status == status) out.push_back(s);
        }
        return reply(200, out);
    }
    if (path.rfind("/v1/messages/scheduled/", 0) == 0 && method == "DELETE") {
        const auto id = tail("/v1/messages/scheduled/");
        for (auto& s : scheduled) {
            if (s.tid == id) {
                if (s.status != "pending") return reply(409, json{{"error", "Only pending messages can be canceled"}});
                s.status = "canceled";
                return reply(200, json{{"message", "canceled"}});
            }
        }
        return not_found();
    }
    if (path == "/v1/messages" && method == "POST") {
        const auto title = body.value("title", "");
        const auto text = body.value("message", "");
        if (title.empty() || text.empty()) {
            return reply(400, json{{"error", "title and message are required"}});
        }
        Template t{0, "", "", "", {}, nullptr};
        Message m = make_message(t, now, false);
        m.title = title;
        m.message = text;
        if (body.contains("level") && body["level"].is_number()) m.priority = body["level"].get<int>();
        const auto app_id = body.value("application_id", "");
        m.application = app_id.empty() ? "" : application_name(app_id);
        if (!app_id.empty() && m.application.empty()) return reply(404, json{{"error", "Application not found"}});
        const auto device_id = body.value("device_id", "");
        m.device = device_id.empty() || device_id == "all" ? "All devices" : device_name(device_id);
        if (body.contains("actions")) m.actions = parse_actions_field(body["actions"]);
        if (body.contains("image") && body["image"].is_string() && !body["image"].get<std::string>().empty()) {
            Attachment a;
            a.tid = tid();
            a.file_name = "image.jpg";
            a.content_type = "image/jpeg";
            a.link = body["image"].get<std::string>();
            m.attachment = a;
        }

        const auto send_at = body.value("send_at", "");
        const auto delay = body.value("delay", "");
        if (!send_at.empty() || !delay.empty()) {
            ScheduledMessage s;
            s.tid = m.tid;
            s.title = m.title;
            s.message = m.message;
            s.priority = m.priority;
            s.status = "pending";
            s.application = m.application;
            s.device = m.device;
            s.actions = m.actions;
            s.tz = "Europe/Berlin";
            s.created_at = format_time(now);
            s.deliver_at = send_at.empty() ? format_time(now + std::chrono::hours(1)) : send_at;
            scheduled.insert(scheduled.begin(), s);
            return reply(201, json{{"tid", s.tid},
                                   {"title", s.title},
                                   {"scheduled", true},
                                   {"status", "pending"},
                                   {"deliver_at", s.deliver_at},
                                   {"tz", s.tz}});
        }
        messages.insert(messages.begin(), m);
        ++sent_today;
        json out = m;
        return reply(201, out);
    }
    if (path.rfind("/v1/messages/", 0) == 0) {
        const auto id = tail("/v1/messages/");
        auto it = std::find_if(messages.begin(), messages.end(), [&](const Message& m) { return m.tid == id; });
        if (it == messages.end()) return not_found();
        if (method == "GET") {
            it->is_read = true;
            json out = *it;
            if (out["device"] == "All devices") out["device"] = "";
            return reply(200, out);
        }
        if (method == "PUT") {
            it->is_read = body.value("is_read", true);
            return success();
        }
        if (method == "DELETE") {
            messages.erase(it);
            return success();
        }
    }

    if (path == "/v1/applications") {
        if (method == "GET") return reply(200, json(applications));
        if (method == "POST") {
            Application app;
            app.tid = tid();
            app.name = body.value("name", "");
            if (app.name.empty()) return reply(400, json{{"error", "name is required"}});
            app.description = body.value("description", "");
            app.color = body.value("color", "#3B82F6");
            if (body.contains("default_level") && body["default_level"].is_number()) {
                app.default_level = body["default_level"].get<int>();
            }
            app.created_at = format_time(now);
            applications.push_back(app);
            return reply(201, json(app));
        }
    }
    if (path.rfind("/v1/applications/", 0) == 0) {
        const auto id = tail("/v1/applications/");
        auto it = std::find_if(applications.begin(), applications.end(), [&](const Application& a) { return a.tid == id; });
        if (it == applications.end()) return not_found();
        if (method == "GET") return reply(200, json(*it));
        if (method == "PUT") {
            if (body.contains("name")) {
                const auto old_name = it->name;
                it->name = body["name"].get<std::string>();
                for (auto& m : messages) {
                    if (m.application == old_name) m.application = it->name;
                }
            }
            if (body.contains("description")) it->description = body["description"].get<std::string>();
            if (body.contains("color")) it->color = body["color"].get<std::string>();
            if (body.contains("default_level") && body["default_level"].is_number()) {
                it->default_level = body["default_level"].get<int>();
            }
            return success();
        }
        if (method == "DELETE") {
            applications.erase(it);
            return success();
        }
    }

    if (path == "/v1/devices" && method == "GET") return reply(200, json(devices));
    if (path.rfind("/v1/devices/", 0) == 0) {
        auto id = tail("/v1/devices/");
        if (id.rfind("tid/", 0) == 0) id = id.substr(4);
        auto it = std::find_if(devices.begin(), devices.end(), [&](const Device& d) { return d.tid == id; });
        if (it == devices.end()) return not_found();
        if (method == "GET") return reply(200, json(*it));
        if (method == "PUT") {
            it->name = body.value("name", it->name);
            return success();
        }
        if (method == "DELETE") {
            devices.erase(it);
            return success();
        }
    }

    if (path == "/v1/webhooks") {
        if (method == "GET") return reply(200, json(webhooks));
        if (method == "POST") {
            Webhook hook;
            hook.tid = tid();
            hook.name = body.value("name", "");
            if (hook.name.empty()) return reply(400, json{{"error", "name is required"}});
            hook.url = body.value("url", "");
            if (hook.url.empty()) hook.url = "wh-" + tid().substr(0, 6);
            hook.message = body.value("message", "*");
            hook.application_tid = body.value("application_id", "");
            hook.device_tid = body.value("device_id", "");
            hook.actions = body.value("actions", "");
            if (body.contains("default_level") && body["default_level"].is_number()) {
                hook.default_level = body["default_level"].get<int>();
            }
            hook.created_at = format_time(now);
            webhooks.push_back(hook);
            webhook_history[hook.tid] = {};
            return reply(201, json(hook));
        }
    }
    if (path.rfind("/v1/webhooks/", 0) == 0) {
        auto id = tail("/v1/webhooks/");
        bool history = false;
        if (const auto slash = id.find('/'); slash != std::string::npos) {
            history = id.substr(slash) == "/history";
            id = id.substr(0, slash);
        }
        auto it = std::find_if(webhooks.begin(), webhooks.end(), [&](const Webhook& w) { return w.tid == id; });
        if (it == webhooks.end()) return not_found();
        if (history) {
            const auto& items = webhook_history[id];
            return reply(200, json{{"requests", items},
                                   {"total", items.size()},
                                   {"page", 1},
                                   {"per", 50},
                                   {"retention_days", 7}});
        }
        if (method == "GET") return reply(200, json(*it));
        if (method == "PUT") {
            if (body.contains("name")) it->name = body["name"].get<std::string>();
            if (body.contains("message")) it->message = body["message"].get<std::string>();
            if (body.contains("url")) it->url = body["url"].get<std::string>();
            if (body.contains("application_id")) it->application_tid = body["application_id"].get<std::string>();
            if (body.contains("device_id")) it->device_tid = body["device_id"].get<std::string>();
            if (body.contains("actions")) it->actions = body["actions"].get<std::string>();
            if (body.contains("is_active")) it->is_active = body["is_active"].get<bool>();
            if (body.contains("default_level") && body["default_level"].is_number()) {
                it->default_level = body["default_level"].get<int>();
            }
            return success();
        }
        if (method == "DELETE") {
            webhooks.erase(it);
            return success();
        }
    }

    if (path == "/v1/forms" && method == "GET") return reply(200, json(forms));
    if (path.rfind("/v1/forms/", 0) == 0 && method == "DELETE") {
        const auto id = tail("/v1/forms/");
        auto it = std::find_if(forms.begin(), forms.end(), [&](const Form& f) { return f.tid == id; });
        if (it == forms.end()) return not_found();
        forms.erase(it);
        return success();
    }

    if (path == "/v1/apikeys") {
        if (method == "GET") return reply(200, json(api_keys));
        if (method == "POST") {
            ApiKey key;
            key.tid = tid();
            key.name = body.value("name", "");
            if (key.name.empty()) return reply(400, json{{"error", "name is required"}});
            key.token = "pa_" + tid() + tid() + tid();
            key.created_at = format_time(now);
            api_keys.push_back(key);
            json out = key;
            api_keys.back().token.clear();
            return reply(201, out);
        }
    }
    if (path.rfind("/v1/apikeys/", 0) == 0 && method == "DELETE") {
        const auto id = tail("/v1/apikeys/");
        auto it = std::find_if(api_keys.begin(), api_keys.end(), [&](const ApiKey& k) { return k.tid == id; });
        if (it == api_keys.end()) return not_found();
        api_keys.erase(it);
        return success();
    }

    return not_found();
}

} // namespace pa
