#include "screens.h"

#include "modals.h"
#include "parse.h"
#include "tr.h"

namespace ui {

namespace {
// Option lists start with a capital, unlike the inline level names, so they get their own keys.
const char* level_option_label(int level) {
    switch (level) {
        case -2: return tr("common.levelOption.silent");
        case -1: return tr("common.levelOption.low");
        case 0: return tr("common.levelOption.default");
        case 1: return tr("common.levelOption.high");
        case 2: return tr("common.levelOption.critical");
        default: return "";
    }
}
} // namespace

std::vector<std::pair<std::string, std::string>> level_options(const std::string& unset_label) {
    std::vector<std::pair<std::string, std::string>> options;
    if (!unset_label.empty()) {
        options.emplace_back("", unset_label);
    }
    for (int level = pa::kMinLevel; level <= pa::kMaxLevel; ++level) {
        options.emplace_back(std::to_string(level), level_option_label(level));
    }
    return options;
}

namespace {
void run_http_action(Ctx& ctx, const pa::HttpActionConfig& config, const std::string& label,
                     std::weak_ptr<bool> owner) {
    ctx.toast(tr("common.http.running", {{"label", label}, {"request", config.method + " " + one_line(config.url, 60)}}));
    ctx.run<pa::HttpResponse>(
        owner,
        [config](pa::Api& api) {
            pa::HttpRequest request;
            request.method = config.method;
            request.url = config.url;
            request.body = config.body;
            request.timeout_seconds = 20;
            for (const auto& [name, value] : config.headers) {
                request.headers.emplace_back(name, value);
            }
            if (!config.body.empty() && !config.headers.count("Content-Type")) {
                request.headers.emplace_back("Content-Type", "application/json");
            }
            return api.run_external(request);
        },
        [&ctx, label](pa::HttpResponse& response) {
            if (response.status == 0) {
                ctx.toast(tr("common.http.failed", {{"label", label}, {"error", response.transport_error}}), ToastKind::Error);
            } else if (response.status < 400) {
                ctx.toast(tr("common.http.done", {{"label", label}, {"status", std::to_string(response.status)}}),
                      ToastKind::Success);
            } else {
                ctx.toast(tr("common.http.httpError", {{"label", label}, {"status", std::to_string(response.status)}}),
                      ToastKind::Error);
            }
        });
}
} // namespace

void run_message_action(Ctx& ctx, const pa::Action& action, std::weak_ptr<bool> owner) {
    if (action.type == "view") {
        ctx.open_modal(info_modal(ctx, action.label, tr("common.view.text", {{"url", action.value}}),
                                  action.value, icon::kExternal));
        return;
    }
    if (action.type == "copy") {
        ctx.open_modal(info_modal(ctx, action.label, action.value, action.value, icon::kCopy));
        return;
    }
    if (action.type != "http") {
        ctx.toast(tr("common.action.unknownType", {{"type", action.type}}), ToastKind::Error);
        return;
    }
    const auto config = pa::parse_http_action(action.value);
    if (!config) {
        ctx.toast(tr("common.action.invalidRequest", {{"label", action.label}}), ToastKind::Error);
        return;
    }
    // The request goes wherever the message says, from inside this network:
    // show the full target and let the user confirm it first.
    ctx.open_modal(confirm_modal(ctx, action.label, config->method + " " + config->url, tr("common.action.run"), false,
                                 [&ctx, config = *config, label = action.label, owner] {
                                     run_http_action(ctx, config, label, owner);
                                 }));
}

} // namespace ui
