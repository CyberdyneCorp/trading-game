#include "model/packs/fetch_queue.hpp"

#include <utility>

namespace tradelab::desktop::packs {

FetchQueue::~FetchQueue() { stop(); }

void FetchQueue::post(Job job) {
  std::lock_guard lock(mu_);
  if (stopping_) return;
  jobs_.push_back(std::move(job));
  if (!thread_.joinable()) thread_ = std::thread([this] { loop(); });
  wake_.notify_one();
}

void FetchQueue::stop() {
  {
    std::lock_guard lock(mu_);
    stopping_ = true;
    jobs_.clear();
  }
  wake_.notify_one();
  if (thread_.joinable()) thread_.join();
}

void FetchQueue::loop() {
  for (;;) {
    Job job;
    {
      std::unique_lock lock(mu_);
      wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
      if (stopping_) return;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    job();
  }
}

}  // namespace tradelab::desktop::packs
