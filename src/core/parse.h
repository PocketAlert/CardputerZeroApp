#pragma once

#include "models.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pa {

constexpr int kMinLevel = -2;
constexpr int kMaxLevel = 2;
constexpr size_t kMaxActions = 3;

std::string level_name(int level);
std::optional<int> parse_level(const std::string& text);

struct ParseError {
    std::string message;
};

// "type:label:value", same grammar as `pocketalert send --action`.
// Template mode skips URL checks for values with %field% placeholders (webhooks).
std::optional<Action> parse_action(const std::string& spec, bool template_mode, ParseError& error);
std::string format_action_spec(const Action& action);

// Accepts Go durations with a leading day count ("1d", "2d12h", "30m") or bare seconds.
std::optional<std::string> normalize_delay(const std::string& text, ParseError& error);

bool is_http_url(const std::string& text);
// https://host[:port][/path], or http:// only for a server on this machine.
bool is_valid_base_url(const std::string& text);
// Whole string must be a base-10 int; no exceptions, no partial parses.
std::optional<int> parse_int(const std::string& text);

struct HttpActionConfig {
    std::string url;
    std::string method = "GET";
    std::map<std::string, std::string> headers;
    std::string body;
};

// Mirrors the mobile app: a bare URL or JSON without "method" runs as GET.
// Unknown methods and header names or values that could split the request
// (CR, LF, ':' in a name) are rejected.
std::optional<HttpActionConfig> parse_http_action(const std::string& value);

// Indices of messages that pass the inbox filters, in order. The needle is
// matched case-insensitively against title, text and application.
std::vector<size_t> filter_messages(const std::vector<Message>& messages, bool unread_only, const std::string& needle);

std::string trim(const std::string& text);
std::string to_lower(std::string text);

} // namespace pa
