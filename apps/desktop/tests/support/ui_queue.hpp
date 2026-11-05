// Stand-in for Rtask_post in model/viewmodel tests: the worker posts tasks here and the test thread
// (playing the UI thread) runs them.
#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

namespace tradelab::desktop::testing {

class UiQueue {
 public:
  void post(std::function<void()> task) {
    {
      std::lock_guard lock(mu_);
      tasks_.push_back(std::move(task));
      ++posted_;
    }
    cv_.notify_all();
  }

  // Runs the queued tasks; returns how many ran.
  int drain() {
    std::deque<std::function<void()>> now;
    {
      std::lock_guard lock(mu_);
      now.swap(tasks_);
    }
    for (auto& t : now) t();
    return static_cast<int>(now.size());
  }

  // Runs tasks until `done()` or the timeout; returns done().
  template <class Pred>
  bool pump_until(Pred done, std::chrono::milliseconds timeout = std::chrono::milliseconds(20000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done()) {
      if (drain() > 0) continue;
      std::unique_lock lock(mu_);
      if (cv_.wait_until(lock, deadline, [this] { return !tasks_.empty(); })) continue;
      return done();
    }
    return true;
  }

  [[nodiscard]] int posted() const {
    std::lock_guard lock(mu_);
    return posted_;
  }

 private:
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> tasks_;
  int posted_ = 0;
};

}  // namespace tradelab::desktop::testing
