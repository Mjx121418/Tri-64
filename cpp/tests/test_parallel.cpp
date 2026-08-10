#include "test_parallel.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace TestParallel {

struct Executor::Group {
    std::atomic<size_t> remaining {0};
};

namespace {

std::atomic<Executor *> active_executor {nullptr};

} // namespace

Executor::Executor(size_t worker_count) {
    workers_.reserve(worker_count);
    for (size_t i {0}; i < worker_count; i++) {
        workers_.emplace_back(&Executor::workerLoop, this);
    }
}

Executor::~Executor() {
    {
        std::lock_guard lock(queue_mutex_);
        stopping_ = true;
    }
    queue_condition_.notify_all();
    for (std::thread &worker : workers_) {
        worker.join();
    }
}

void Executor::enqueue(std::function<void()> task, Group *group) {
    {
        std::lock_guard lock(queue_mutex_);
        queue_.emplace_back([task = std::move(task), group]() mutable {
            task();
            if (group != nullptr) {
                group->remaining.fetch_sub(1, std::memory_order_release);
            }
        });
    }
    queue_condition_.notify_one();
}

void Executor::wait(Group &group) {
    while (group.remaining.load(std::memory_order_acquire) != 0) {
        std::function<void()> task;
        {
            std::lock_guard lock(queue_mutex_);
            if (!queue_.empty()) {
                task = std::move(queue_.front());
                queue_.pop_front();
            }
        }
        if (task) {
            task();
        } else {
            std::this_thread::yield();
        }
    }
}

void Executor::run(const std::vector<std::function<void()>> &tasks) {
    Group group;
    group.remaining.store(tasks.size(), std::memory_order_relaxed);
    for (const auto &task : tasks) {
        enqueue(task, &group);
    }
    if (!tasks.empty()) {
        wait(group);
    }
}

void Executor::parallelFor(size_t count, const std::function<void(size_t)> &task) {
    Group group;
    group.remaining.store(count, std::memory_order_relaxed);
    for (size_t i {0}; i < count; i++) {
        enqueue([&task, i]() { task(i); }, &group);
    }
    if (count != 0) {
        wait(group);
    }
}

void Executor::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock lock(queue_mutex_);
            queue_condition_.wait(lock, [this]() {
                return stopping_ || !queue_.empty();
            });
            if (stopping_ && queue_.empty()) {
                return;
            }
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        task();
    }
}

void setExecutor(Executor *value) {
    active_executor.store(value, std::memory_order_release);
}

Executor *executor() {
    return active_executor.load(std::memory_order_acquire);
}

} // namespace TestParallel
