//
// Created by loulfy on 21/01/2024.
//

#pragma once

#include <utility>
#include <coroutine>

namespace ler::sys
{
    template <typename T>
    class Task
    {
    public:
        struct promise_type
        {
            T m_result;

            Task get_return_object() { return Task(this); }
            void unhandled_exception() noexcept {}
            void return_value(T result) noexcept { m_result = std::move(result); }
            std::suspend_never initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }
        };

        //Task() : m_handle(nullptr) {}
        explicit Task(promise_type* promise) : m_handle{Handle::from_promise(*promise)} {}
        //Task(Task&& other) : m_handle{std::exchange(other.m_handle, nullptr)} {}

        ~Task()
        {
            if (m_handle)
                m_handle.destroy();
        }

        [[nodiscard]] T getResult() const & {
            assert(m_handle.done());
            return m_handle.promise().m_result;
        }

        [[nodiscard]] T&& getResult() const && {
            assert(m_handle.done());
            return std::move(m_handle.promise().m_result);
        }

        [[nodiscard]] bool done() const { return m_handle.done(); }

        using Handle = std::coroutine_handle<promise_type>;
        Handle m_handle = nullptr;
    };
}
