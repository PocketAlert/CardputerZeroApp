#include "worker.h"

#include "http.h"

#include <cstdio>
#include <exception>
#include <fcntl.h>
#include <unistd.h>

namespace ui {

Worker::State::~State() {
    if (wake_read >= 0) ::close(wake_read);
    if (wake_write >= 0) ::close(wake_write);
}

void Worker::State::wake() {
    if (wake_write >= 0) {
        const char byte = 1;
        // A full pipe already means "wake up"; losing this byte is fine.
        (void)!::write(wake_write, &byte, 1);
    }
}

Worker::Worker() : state_(std::make_shared<State>()) {
    int fds[2];
    if (::pipe(fds) == 0) {
        for (int fd : fds) {
            ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
            ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        }
        state_->wake_read = fds[0];
        state_->wake_write = fds[1];
    }
    thread_ = std::thread(loop, state_);
}

Worker::~Worker() {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->stop = true;
        state_->jobs.clear();
        state_->ui.clear();
    }
    state_->cv.notify_all();
    // A request in flight is aborted within about a second, so joining keeps
    // the thread from running past main() into static destructors.
    pa::http_cancel_all();
    if (thread_.joinable()) thread_.join();
}

void Worker::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->jobs.push_back(std::move(job));
    }
    state_->cv.notify_one();
}

Worker::UiPoster Worker::ui_poster() const {
    auto state = state_;
    return [state](std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->stop) return;
            state->ui.push_back(std::move(fn));
        }
        state->wake();
    };
}

bool Worker::drain_ui() {
    if (state_->wake_read >= 0) {
        char sink[64];
        while (::read(state_->wake_read, sink, sizeof(sink)) > 0) {
        }
    }
    std::deque<std::function<void()>> ready;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        ready.swap(state_->ui);
    }
    for (auto& fn : ready) {
        fn();
    }
    return !ready.empty();
}

int Worker::wake_fd() const {
    return state_->wake_read;
}

void Worker::loop(std::shared_ptr<State> state) {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(state->mutex);
            state->cv.wait(lock, [&] { return state->stop || !state->jobs.empty(); });
            if (state->stop) {
                return;
            }
            job = std::move(state->jobs.front());
            state->jobs.pop_front();
        }
        // Jobs report their own failures; this only keeps a stray exception
        // from taking the whole app down through std::terminate.
        try {
            job();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[pocketalert] worker job failed: %s\n", e.what());
        } catch (...) {
            std::fprintf(stderr, "[pocketalert] worker job failed\n");
        }
    }
}

} // namespace ui
