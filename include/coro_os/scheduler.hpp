#pragma once
#include "detail/queues.hpp"
#include "detail/wait_queue.hpp"
#include "port.hpp"
#include <coroutine>
#include <type_traits>
#include <utility>

namespace coro_os {
template<class Task, class Port> class job;
template<class Port> class notification_endpoint;
template<class Port> class mutex;
template<class Port, std::uint32_t Limit> class semaphore;

template<class Port> class scheduler {
    static_assert(scheduler_port<Port>, "Port must provide noexcept critical-section, notify_cpu and in_isr methods");
    static_assert(std::is_nothrow_move_constructible_v<Port>);
    template<class Task, class OtherPort> friend class job;
    friend class notification_endpoint<Port>;
    template<class> friend class mutex;
    template<class, std::uint32_t> friend class semaphore;
    [[no_unique_address]] Port port_;
    detail::ready_queue<Port> ready_;
    detail::timer_queue<Port> timers_;
    detail::node<Port>* pending_head_ = nullptr;
    detail::node<Port>* pending_tail_ = nullptr;
    inline static constinit scheduler* instance_ = nullptr;

    static scheduler& active() noexcept {
        detail::require(instance_ != nullptr);
        return *instance_;
    }
    template<class Promise>
    static detail::node<Port>& awaited_node(std::coroutine_handle<Promise> handle) noexcept {
        static_assert(noexcept(std::declval<Promise&>().context()));
        auto* n = static_cast<detail::node<Port>*>(handle.promise().context());
        detail::require(n != nullptr);
        return *n;
    }

    void thread_only() const noexcept;
    void attach(detail::node<Port>&) noexcept;
    void detach(detail::node<Port>&) noexcept;
    bool suspend_sleep(detail::node<Port>&, tick_t) noexcept;
    bool suspend_wait(detail::node<Port>&, tick_t, bool timed, wait_result&) noexcept;
    void signal(detail::node<Port>&) noexcept;
    void enqueue_pending(detail::node<Port>&) noexcept; // Caller holds critical guard.
    void request_wake(detail::node<Port>&) noexcept; // Caller holds critical guard.
    void remove_pending(detail::node<Port>&) noexcept; // Caller holds critical guard.
    void drain_pending() noexcept;
public:
    explicit scheduler(Port) noexcept;
    ~scheduler();
    scheduler(const scheduler&) = delete;
    scheduler& operator=(const scheduler&) = delete;

    template<class Task>
    job<std::remove_cvref_t<Task>, Port> spawn(Task&& task) noexcept;
    tick_t poll(tick_t elapsed) noexcept;
    bool has_work() noexcept;

    class sleep_awaiter {
        tick_t delay_;
    public:
        explicit sleep_awaiter(tick_t d) noexcept : delay_(d) {}
        bool await_ready() const noexcept { return false; }
        template<class Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            auto& s = scheduler::active();
            auto& n = scheduler::awaited_node(handle);
            return s.suspend_sleep(n, delay_);
        }
        void await_resume() const noexcept {}
    };
    static sleep_awaiter sleep_for(tick_t delay) noexcept { return sleep_awaiter(delay); }
    static sleep_awaiter yield() noexcept { return sleep_awaiter(0); }

    template<bool Timed> class notification_awaiter {
        tick_t delay_;
        wait_result result_ = wait_result::timeout;
    public:
        explicit notification_awaiter(tick_t d) noexcept : delay_(d) {}
        bool await_ready() const noexcept { return false; }
        template<class Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            auto& s = scheduler::active();
            auto& n = scheduler::awaited_node(handle);
            return s.suspend_wait(n, delay_, Timed, result_);
        }
        auto await_resume() const noexcept {
            if constexpr (Timed) return result_;
        }
    };
    static notification_awaiter<false> wait() noexcept { return notification_awaiter<false>(0); }
    static notification_awaiter<true> wait_for(tick_t delay) noexcept { return notification_awaiter<true>(delay); }
};
// Empty ports are owned values; lvalue stateful ports retain their identity.
template<class Port> scheduler(Port&) -> scheduler<std::conditional_t<
    std::is_empty_v<Port> && std::is_nothrow_copy_constructible_v<Port>
                        && std::is_nothrow_move_constructible_v<Port>,
    std::remove_cv_t<Port>, Port&>>;
template<class Port> scheduler(Port&&) -> scheduler<std::remove_cvref_t<Port>>;
} // namespace coro_os

#include "job.hpp"
#include "detail/scheduler_impl.hpp"
