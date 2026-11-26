#include "http.h"

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>

namespace pa {
namespace {

std::atomic<bool> g_cancelled{false};

struct BodySink {
    std::string* out;
    size_t limit;
    bool overflow = false;
};

size_t write_body(char* data, size_t size, size_t count, void* user) {
    auto* sink = static_cast<BodySink*>(user);
    const size_t n = size * count;
    if (sink->out->size() + n > sink->limit) {
        sink->overflow = true;
        return 0;
    }
    sink->out->append(data, n);
    return n;
}

size_t write_header(char* data, size_t size, size_t count, void* user) {
    auto* headers = static_cast<std::map<std::string, std::string>*>(user);
    std::string line(data, size * count);
    const auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        std::string value = line.substr(colon + 1);
        const auto first = value.find_first_not_of(" \t");
        const auto last = value.find_last_not_of(" \t\r\n");
        value = first == std::string::npos ? "" : value.substr(first, last - first + 1);
        (*headers)[name] = value;
    }
    return size * count;
}

// curl calls this about once a second even while resolving or connecting.
int progress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return g_cancelled.load() ? 1 : 0;
}

// One handle per thread keeps curl's connection cache alive between requests,
// so a poll reuses the open TLS connection instead of a fresh TCP + TLS
// handshake each time: on the device's Wi-Fi that handshake is what times out.
struct ThreadHandle {
    CURL* curl = nullptr;
    ~ThreadHandle() {
        if (curl) curl_easy_cleanup(curl);
    }
};

CURL* thread_handle() {
    thread_local ThreadHandle handle;
    if (!handle.curl) {
        handle.curl = curl_easy_init();
    } else {
        curl_easy_reset(handle.curl);
    }
    return handle.curl;
}

void restrict_protocols(CURL* curl) {
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
}

} // namespace

void http_global_init() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

void http_cancel_all() {
    g_cancelled.store(true);
}

bool http_cancelled() {
    return g_cancelled.load();
}

CurlTransport::CurlTransport(std::string user_agent) : user_agent_(std::move(user_agent)) {
    http_global_init();
}

HttpResponse CurlTransport::send(const HttpRequest& request) {
    HttpResponse response;
    if (g_cancelled.load()) {
        response.transport_error = "cancelled";
        return response;
    }
    CURL* curl = thread_handle();
    if (!curl) {
        response.transport_error = "curl init failed";
        return response;
    }

    // curl_easy_strerror is generic ("SSL peer certificate ... was not OK");
    // the error buffer says what actually failed, e.g. "certificate is not yet valid".
    char error_buffer[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);

    curl_slist* header_list = nullptr;
    for (const auto& [name, value] : request.headers) {
        header_list = curl_slist_append(header_list, (name + ": " + value).c_str());
    }

    BodySink sink{&response.body, request.max_body_bytes};
    restrict_protocols(curl);
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent_.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeout_seconds);
    // Signals are unsafe from worker threads; DNS timeouts rely on the threaded resolver.
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(request.max_body_bytes));
    // No TCP keepalive: probes between polls would wake the Wi-Fi radio for a
    // connection the server drops anyway. Older idle connections are not reused.
    curl_easy_setopt(curl, CURLOPT_MAXAGE_CONN, 50L);
    // Too slow for this long means a dead link; give up and let the retry reconnect.
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 64L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 12L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
    if (request.method == "HEAD") {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }
    if (!request.body.empty() || request.method == "POST" || request.method == "PUT" || request.method == "PATCH") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    }

    const CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    } else if (sink.overflow || code == CURLE_FILESIZE_EXCEEDED) {
        response.body.clear();
        response.transport_error = "response too large";
    } else {
        response.transport_error = error_buffer[0] ? error_buffer : curl_easy_strerror(code);
    }

    curl_slist_free_all(header_list);
    // The handle outlives this call; drop pointers into this stack frame.
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, nullptr);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, nullptr);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, nullptr);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, nullptr);
    return response;
}

std::string url_encode(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0f]);
        }
    }
    return out;
}

std::string build_query(const std::vector<std::pair<std::string, std::string>>& params) {
    std::string out;
    for (const auto& [key, value] : params) {
        if (value.empty()) {
            continue;
        }
        out += out.empty() ? "?" : "&";
        out += url_encode(key) + "=" + url_encode(value);
    }
    return out;
}

} // namespace pa
