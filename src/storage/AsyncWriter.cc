#include "storage/AsyncWriter.h"

namespace gs::storage {

AsyncWriter::AsyncWriter() : thread_([this] { threadFunc(); }) {}

AsyncWriter::~AsyncWriter() {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    stop_ = true;
  }
  cond_.notify_one();
  thread_.join();
}

void AsyncWriter::enqueue(Task t) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    front_.push_back(std::move(t));
  }
  cond_.notify_one();
}

void AsyncWriter::threadFunc() {
  std::vector<Task> batch;
  while (true) {
    {
      std::unique_lock<std::mutex> lk(mutex_);
      cond_.wait(lk, [this] { return stop_ || !front_.empty(); });
      if (front_.empty() && stop_) break;  // 队列已清空才允许退出(flush 语义)
      batch.swap(front_);                  // 整批换走,锁外执行
    }
    for (auto& t : batch) t();
    batch.clear();
  }
}

}  // namespace gs::storage
