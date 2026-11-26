#include "screens.h"

#include "config.h"
#include "modals.h"
#include "parse.h"
#include "tr.h"

namespace ui {
namespace {

constexpr const char* kDeviceKeyName = "Cardputer Zero";

struct KeyCheck {
    pa::Result<pa::Profile> profile;
};

struct EmailSignin {
    pa::Result<pa::SigninResult> signin;
    pa::Result<pa::ApiKey> key;
};

// Exchanges a JWT for a dedicated API key, so the device (and the notifier
// daemon) authenticate the same way as the CLI and can be revoked separately.
pa::Result<pa::ApiKey> mint_device_key(pa::Api& api, const std::string& jwt) {
    pa::Api authed = api;
    pa::Credentials credentials;
    credentials.jwt = jwt;
    authed.set_credentials(credentials);
    return authed.create_api_key(kDeviceKeyName);
}

class EmailLoginScreen final : public Screen {
public:
    explicit EmailLoginScreen(Ctx& ctx) : Screen(ctx, tr("login.email.title")) {}

    void build() override {
        chrome_.set_keys({tr("login.email.keySignIn"), "", "", "", ""});
        form_.create(chrome_.body());
        attach(form_);
        std::vector<Field> fields;
        fields.push_back({"email", tr("login.email.emailLabel"), Field::Kind::Text, ctx_.config().account_email, {}, false,
                          tr("login.email.emailPlaceholder")});
        fields.push_back({"password", tr("login.email.passwordLabel"), Field::Kind::Secret, "", {}, false,
                          tr("login.email.passwordPlaceholder")});
        Field submit;
        submit.id = "submit";
        submit.label = tr("login.email.submit");
        submit.kind = Field::Kind::Button;
        submit.action = [this] { submit_form(); };
        fields.push_back(submit);
        Field note;
        note.id = "note";
        note.kind = Field::Kind::Note;
        note.value = tr("login.email.note", {{"name", kDeviceKeyName}});
        fields.push_back(note);
        form_.set_fields(std::move(fields));
    }

    bool on_key(const Key& key) override {
        if (!form_.editing() && key.slot() == 0) {
            submit_form();
            return true;
        }
        return form_.handle_key(key);
    }

private:
    void submit_form() {
        if (busy_) return;
        const auto email = pa::trim(form_.value("email"));
        const auto password = form_.value("password");
        if (email.empty() || password.empty()) {
            ctx_.toast(tr("login.email.missingFields"), ToastKind::Error);
            return;
        }
        busy_ = true;
        ctx_.toast(tr("login.email.signingIn"));
        run<EmailSignin>(
            [email, password](pa::Api& api) {
                EmailSignin out;
                out.signin = api.signin(email, password);
                if (out.signin.ok && !out.signin.value.two_factor_required && !out.signin.value.token.empty()) {
                    out.key = mint_device_key(api, out.signin.value.token);
                }
                return out;
            },
            [this, email](EmailSignin& result) {
                busy_ = false;
                if (!result.signin.ok) {
                    ctx_.show_error(result.signin.error, tr("login.error.signInFailed"));
                    return;
                }
                if (result.signin.value.two_factor_required) {
                    ask_two_factor(email, result.signin.value.challenge);
                    return;
                }
                finish(result.key, email);
            });
    }

    void ask_two_factor(const std::string& email, const std::string& challenge) {
        ctx_.open_modal(prompt_modal(
            ctx_, tr("login.twoFactor.title"), tr("login.twoFactor.hint"), "", false,
            [this, email, challenge](const std::string& code) {
                busy_ = true;
                run<EmailSignin>(
                    [challenge, code](pa::Api& api) {
                        EmailSignin out;
                        out.signin = api.two_factor_login(challenge, pa::trim(code));
                        if (out.signin.ok && !out.signin.value.token.empty()) {
                            out.key = mint_device_key(api, out.signin.value.token);
                        }
                        return out;
                    },
                    [this, email, challenge](EmailSignin& result) {
                        busy_ = false;
                        if (!result.signin.ok) {
                            ctx_.show_error(result.signin.error, tr("login.error.codeRejected"));
                            if (result.signin.error.status == 401) ask_two_factor(email, challenge);
                            return;
                        }
                        finish(result.key, email);
                    });
            }));
    }

    void finish(const pa::Result<pa::ApiKey>& key, const std::string& email) {
        if (!key.ok || key.value.token.empty()) {
            ctx_.show_error(key.error, tr("login.error.deviceKeyFailed"));
            return;
        }
        ctx_.toast(tr("login.signedInAs", {{"email", email}}), ToastKind::Success);
        ctx_.sign_in_with_key(key.value.token, email);
    }

    FormView form_;
    bool busy_ = false;
};

class LoginScreen final : public Screen {
public:
    explicit LoginScreen(Ctx& ctx) : Screen(ctx, "Pocket Alert") {}

    bool is_root() const override { return true; }

    void build() override {
        const auto& t = theme();
        chrome_.set_keys({"", "", "", "", ""});
        auto* body = chrome_.body();

        auto* brand = make_box(body);
        lv_obj_set_size(brand, kScreenW, 38);
        lv_obj_set_style_pad_hor(brand, 8, 0);
        set_row_flex(brand, 8);
        make_icon(brand, icon::kBellRinging, t.p.coral, t.f.icon_big);
        auto* words = make_box(brand);
        lv_obj_set_size(words, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        set_column_flex(words, 0);
        make_label(words, "Pocket Alert", t.f.title, t.p.text);
        make_label(words, tr("login.tagline"), t.f.small, t.p.muted);

        auto* list_area = make_box(body);
        lv_obj_set_pos(list_area, 0, 40);
        lv_obj_set_size(list_area, kScreenW, kBodyH - 40);

        options_.push_back({tr("login.options.email"), icon::kAt, [this] { ctx_.push(std::make_unique<EmailLoginScreen>(ctx_)); }});
        options_.push_back({tr("login.options.apiKey"), icon::kKey, [this] { ask_key(); }});
        if (const auto key = pa::cli_api_key()) {
            options_.push_back({tr("login.options.cliKey"), icon::kLightning, [this, key = *key] { check_key(key); }});
        }
        options_.push_back({tr("login.options.demo"), icon::kCursor, [this] { ctx_.start_demo(); }});

        list_.create(list_area, 22);
        list_.set_items(options_.size(), [this](lv_obj_t* row, size_t i) {
            const auto& th = theme();
            set_row_flex(row, 8);
            make_icon(row, options_[i].glyph, th.p.accent, th.f.icon_small);
            make_label(row, options_[i].label, th.f.body, th.p.text);
        });
    }

    bool on_key(const Key& key) override {
        if (list_.handle_key(key)) return true;
        if (key.enter()) {
            auto action = options_[list_.selected()].action;
            action();
            return true;
        }
        return false;
    }

    std::string help() const override {
        return tr("login.help", {{"path", "~/.config/pocketalert-cardputer"}, {"command", "pocketalert auth"}});
    }

private:
    struct Option {
        std::string label;
        const char* glyph;
        std::function<void()> action;
    };

    void ask_key() {
        ctx_.open_modal(prompt_modal(ctx_, tr("login.apiKey.title"), tr("login.apiKey.hint", {{"url", pa::kApiKeysPageUrl}}), "", false,
                                     [this](const std::string& key) {
                                         if (!pa::trim(key).empty()) check_key(pa::trim(key));
                                     }));
    }

    void check_key(const std::string& key) {
        ctx_.toast(tr("login.apiKey.checking"));
        run<KeyCheck>(
            [key](pa::Api& api) {
                pa::Api probe = api;
                pa::Credentials credentials;
                credentials.api_key = key;
                probe.set_credentials(credentials);
                return KeyCheck{probe.profile()};
            },
            [this, key](KeyCheck& result) {
                if (!result.profile.ok) {
                    ctx_.show_error(result.profile.error, tr("login.error.keyRejected"));
                    return;
                }
                ctx_.toast(tr("login.signedInAs", {{"email", result.profile.value.email}}), ToastKind::Success);
                ctx_.sign_in_with_key(key, result.profile.value.email);
            });
    }

    std::vector<Option> options_;
    ListView list_;
};

} // namespace

std::unique_ptr<Screen> make_login(Ctx& ctx) {
    return std::make_unique<LoginScreen>(ctx);
}

} // namespace ui
