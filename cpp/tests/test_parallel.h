#ifndef TEST_PARALLEL_H
#define TEST_PARALLEL_H

#include <cstddef>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace TestParallel {

class Executor {
public:
    explicit Executor(size_t worker_count);
    ~Executor();

    Executor(const Executor &) = delete;
    Executor &operator=(const Executor &) = delete;

    void run(const std::vector<std::function<void()>> &tasks);
    void parallelFor(size_t count, const std::function<void(size_t)> &task);

private:
    struct Group;

    void enqueue(std::function<void()> task, Group *group);
    void wait(Group &group);
    void workerLoop();

    std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    std::deque<std::function<void()>> queue_;
    bool stopping_ {false};
    std::vector<std::thread> workers_;
};

void setExecutor(Executor *executor);
Executor *executor();

template<typename Function>
void parallelFor(size_t count, Function &&function) {
    if (Executor *active = executor()) {
        active->parallelFor(count, std::function<void(size_t)>(function));
        return;
    }

    for (size_t i {0}; i < count; i++) {
        function(i);
    }
}

} // namespace TestParallel

#endif /* TEST_PARALLEL_H */
