#pragma once

#include "models.h"
#include "screen.h"

#include <memory>
#include <optional>
#include <string>

namespace ui {

struct InboxFilter {
    std::string app_tid;
    std::string app_name;
};

struct ComposePreset {
    std::string app_tid;
    std::string device_tid;
};

std::unique_ptr<Screen> make_login(Ctx& ctx);
std::unique_ptr<Screen> make_inbox(Ctx& ctx, InboxFilter filter = {});
std::unique_ptr<Screen> make_message(Ctx& ctx, pa::Message message);
std::unique_ptr<Screen> make_compose(Ctx& ctx, ComposePreset preset = {});
std::unique_ptr<Screen> make_menu(Ctx& ctx);
std::unique_ptr<Screen> make_scheduled(Ctx& ctx);
std::unique_ptr<Screen> make_applications(Ctx& ctx);
std::unique_ptr<Screen> make_application_form(Ctx& ctx, std::optional<pa::Application> existing);
std::unique_ptr<Screen> make_devices(Ctx& ctx);
std::unique_ptr<Screen> make_webhooks(Ctx& ctx);
std::unique_ptr<Screen> make_webhook_form(Ctx& ctx, std::optional<pa::Webhook> existing);
std::unique_ptr<Screen> make_webhook_history(Ctx& ctx, pa::Webhook webhook);
std::unique_ptr<Screen> make_forms(Ctx& ctx);
std::unique_ptr<Screen> make_api_keys(Ctx& ctx);
std::unique_ptr<Screen> make_account(Ctx& ctx);
std::unique_ptr<Screen> make_settings(Ctx& ctx);
// First-run tour; finishing or skipping it sets Config::onboarded.
std::unique_ptr<Screen> make_welcome(Ctx& ctx);
// Every screen's keys on one page, plus the tour again.
std::unique_ptr<Screen> make_shortcuts(Ctx& ctx);

std::string compose_help();
std::string scheduled_help();

// view → URL + QR to open on a phone, copy → value + QR, http → request in background.
void run_message_action(Ctx& ctx, const pa::Action& action, std::weak_ptr<bool> owner);

std::vector<std::pair<std::string, std::string>> level_options(const std::string& unset_label);

} // namespace ui
