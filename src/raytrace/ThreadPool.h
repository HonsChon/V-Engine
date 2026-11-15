/**
 * @file ThreadPool.h
 * @brief 简单线程池 — 软光逐分块并行渲染用
 *
 * 任务队列 + 条件变量；waitAll() 阻塞至全部任务完成（帧同步用）。
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace VEngine {
namespace RT {

class ThreadPool {
public:
    explicit ThreadPool(uint32_t threadCount = 0) {
        if (threadCount == 0) {
            threadCount = std::max(1u, std::thread::hardware_concurrency());
        }
        workers_.reserve(threadCount);
        for (uint32_t i = 0; i < threadCount; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// 提交任务（异步执行）
    void enqueue(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(task));
            ++pendingTasks_;
        }
        cv_.notify_one();
    }

    /// 阻塞等待所有已提交任务完成
    void waitAll() {
        std::unique_lock<std::mutex> lock(mutex_);
        doneCv_.wait(lock, [this] { return pendingTasks_ == 0; });
    }

    uint32_t threadCount() const { return static_cast<uint32_t>(workers_.size()); }

private:
    void workerLoop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_ && queue_.empty()) return;
                task = std::move(queue_.front());
                queue_.pop();
            }
            task();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (--pendingTasks_ == 0) {
                    doneCv_.notify_all();
                }
            }
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;       // 唤醒工作者
    std::condition_variable doneCv_;   // waitAll 完成
    std::atomic<bool> stopping_{false};
    uint32_t pendingTasks_ = 0;        // 已入队未完成任务数（mutex_ 保护）
};

} // namespace RT
} // namespace VEngine
