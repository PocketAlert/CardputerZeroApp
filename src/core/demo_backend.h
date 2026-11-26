#pragma once

#include "http.h"

#include <memory>

namespace pa {

// In-memory imitation of the Pocket Alert API so the app can be explored
// without an account or network. It also delivers a fresh alert periodically.
class DemoTransport final : public HttpTransport {
public:
    explicit DemoTransport(int incoming_every_seconds = 40, bool simulate_latency = true);
    ~DemoTransport() override;
    HttpResponse send(const HttpRequest& request) override;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace pa
