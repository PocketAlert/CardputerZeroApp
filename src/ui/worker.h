#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace ui {

// One background thread runs network jobs in order; their completions are
// queued back and drained on the LVGL thread, which is the only one that
// may touch widgets. Posting a completion also makes wake_fd() readable, so
// the main loop can sleep in poll() instead of checking the queue.
class Worker {
public:
    Worker();
    ~Worker();
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void post(std::function<void()> job);
    // Safe to call from a job even after the Worker itself is gone.
    using UiPoster = std::function<void(std::function<void()>)>;
    UiPoster ui_poster() const;
    // Runs queued completions; true when there were any.
    bool drain_ui();
    int wake_fd() const;

private:
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::function<void()>> jobs;
        std::deque<std::function<void()>> ui;
        bool stop = false;
        int wake_read = -1;
        int wake_write = -1;

        ~State();
        void wake();
    };

    static void loop(std::shared_ptr<State> state);

    std::shared_ptr<State> state_;
    std::thread thread_;
};

} // namespace ui
