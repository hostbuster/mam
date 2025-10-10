#pragma once

#include <thread>
#include <vector>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <mutex>

class JobPool {
public:
  explicit JobPool(size_t numThreads) : stop_(false) {
    workers_.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
      workers_.emplace_back([this] { this->workerLoop(); });
    }
  }
  ~JobPool() { stop(); }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(m_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_) if (t.joinable()) t.join();
  }

  void submit(std::function<void()> job) {
    {
      std::lock_guard<std::mutex> lock(m_);
      jobs_.push_back(std::move(job));
    }
    cv_.notify_one();
  }

private:
  void workerLoop() {
    for (;;) {
      std::function<void()> job;
      {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
        if (stop_ && jobs_.empty()) return;
        job = std::move(jobs_.back());
        jobs_.pop_back();
      }
      job();
    }
  }

  std::vector<std::thread> workers_;
  std::vector<std::function<void()>> jobs_;
  std::mutex m_;
  std::condition_variable cv_;
  bool stop_;
};


