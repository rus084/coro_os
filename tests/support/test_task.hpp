#pragma once
#include <coroutine>
#include <cstdlib>
#include <utility>

// Host fixture only: its frame uses ordinary coroutine allocation.
struct test_task {
    struct promise_type {
        void* context_ = nullptr;
        void* context() const noexcept { return context_; }
        test_task get_return_object() noexcept {
            return test_task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::abort(); }
    };
    std::coroutine_handle<promise_type> handle;
    explicit test_task(std::coroutine_handle<promise_type> h) noexcept : handle(h) {}
    test_task(test_task&& other) noexcept : handle(std::exchange(other.handle, {})) {}
    test_task(const test_task&) = delete;
    ~test_task() { if (handle) handle.destroy(); }
    void resume() noexcept { handle.resume(); }
    bool done() const noexcept { return handle.done(); }
    void bind_context(void* context) noexcept { handle.promise().context_ = context; }
};
