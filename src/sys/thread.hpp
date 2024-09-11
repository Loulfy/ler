//
// Created by loulfy on 04/05/2024.
//

#pragma once

#include <condition_variable>
#include <coroutine>
#include <functional>
#include <stop_token>
#include <thread>
#include <mutex>
#include <queue>

namespace ler::sys
{
class ThreadPool
{
  public:
    ThreadPool() : ThreadPool(4)
    {
    }

    explicit ThreadPool(const unsigned int num_threads)
    {
        threads = std::make_unique<std::jthread[]>(num_threads);
        for (unsigned int i = 0; i < num_threads; ++i)
            threads[i] = std::jthread(std::bind_front(&ThreadPool::worker, this));
    }

    void enqueue_task(std::coroutine_handle<> coro) noexcept
    {
        {
            const std::scoped_lock tasks_lock(m_mutex);
            m_coroutines.emplace(coro);
        }
        coro_available_cv.notify_one();
    }

    auto schedule()
    {
        struct awaiter : public std::suspend_always
        {
            // clang-format off
            explicit awaiter(ThreadPool* pool) : m_threadPool(pool) {}
            ThreadPool* m_threadPool;
            void await_suspend(std::coroutine_handle<> coro) const noexcept {
                m_threadPool->enqueue_task(coro);
            }
            // clang-format on
        };
        return awaiter(this);
    }

  private:
    void worker(const std::stop_token& stoken)
    {
        std::coroutine_handle<> coro;
        for (;;)
        {
            {
                std::unique_lock tasks_lock(m_mutex);
                coro_available_cv.wait(tasks_lock, stoken, [&] { return !m_coroutines.empty(); });

                if (stoken.stop_requested())
                    return;

                coro = m_coroutines.front();
                m_coroutines.pop();
            }
            coro.resume();
        }
    }

    std::condition_variable_any coro_available_cv = {};
    std::unique_ptr<std::jthread[]> threads = nullptr;
    std::queue<std::coroutine_handle<>> m_coroutines;
    mutable std::mutex m_mutex = {};
};
} // namespace ler::sys