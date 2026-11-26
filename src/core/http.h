#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace pa {

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    long timeout_seconds = 30;
    // API calls never follow redirects: the Token header would travel to
    // whatever host the redirect names. Message actions may opt in.
    bool follow_redirects = false;
    // Decompressed bytes; a larger body aborts the transfer instead of
    // exhausting the device's 512 MB.
    size_t max_body_bytes = 4U * 1024U * 1024U;
};

struct HttpResponse {
    long status = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    // Set when the request never produced an HTTP status (DNS, TLS, timeout).
    std::string transport_error;
};

class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual HttpResponse send(const HttpRequest& request) = 0;
};

class CurlTransport final : public HttpTransport {
public:
    explicit CurlTransport(std::string user_agent);
    HttpResponse send(const HttpRequest& request) override;

private:
    std::string user_agent_;
};

void http_global_init();
// Aborts transfers in flight (within about a second) and fails new ones, so
// a worker thread can be joined at exit instead of outliving main().
void http_cancel_all();
bool http_cancelled();

std::string url_encode(const std::string& value);
std::string build_query(const std::vector<std::pair<std::string, std::string>>& params);

} // namespace pa
