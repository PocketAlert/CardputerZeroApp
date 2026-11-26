#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// UI strings live in <root>/<locale>/<namespace>.json, the same layout as the
// website's messages/ so its translation script can fill them in. A key is
// "<namespace>.<path.inside.file>". Lookups fall back to English, then to the
// key itself, so a missing translation never shows an empty label.
namespace pa::i18n {

struct Language {
    const char* code;
    const char* native_name;
};

// English first; the rest are the website's locales.
const std::vector<Language>& languages();
bool is_supported(std::string_view code);

// "" means "follow the system": LC_ALL, LC_MESSAGES, then LANG ("de_DE.UTF-8" -> "de").
std::string resolve(std::string_view setting);
std::string from_locale_env(std::string_view value);

// Loads English plus `code` from `root`. Returns false when English itself is missing.
bool load(const std::filesystem::path& root, const std::string& code);
const std::string& current();

// The returned pointer stays valid until the next load().
const char* tr(std::string_view key);

using Args = std::initializer_list<std::pair<std::string_view, std::string>>;
// Replaces {name} placeholders with the given values.
std::string tr(std::string_view key, Args args);
std::string format(std::string_view text, Args args);

} // namespace pa::i18n
