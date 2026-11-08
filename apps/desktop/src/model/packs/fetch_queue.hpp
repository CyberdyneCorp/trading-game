// One background thread for network reads that must not hold the simulation worker (PackService's
// catalog read): a slow or silent server would otherwise queue every engine command behind it, for up
// to curl's timeouts. Jobs run one at a time, in order; the thread starts with the first job. stop()
// drops the jobs not started and waits for the running one (shutdown: before the worker is joined).
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace tradelab::desktop::packs {

class FetchQueue {
 public:
  using Job = std::function<void()>;

  FetchQueue() = default;
  ~FetchQueue();
  FetchQueue(const FetchQueue&) = delete;
  FetchQueue& operator=(const FetchQueue&) = delete;

  void post(Job job);  // ignored after stop()
  void stop();

 private:
  void loop();

  std::mutex mu_;
  std::condition_variable wake_;
  std::deque<Job> jobs_;
  bool stopping_ = false;
  std::thread thread_;
};

}  // namespace tradelab::desktop::packs
