#pragma once
#include "scheduler.hpp"

namespace coro_os {
template<class Port, std::uint32_t Limit> class semaphore {
    static_assert(Limit > 0, "semaphore requires a positive capacity");
    detail::wait_queue<Port> waiters_;
    std::uint32_t available_;
    // Outstanding grants remain counted until await_resume accepts them.
    std::uint32_t reserved_ = 0;

    bool give_locked(scheduler<Port>& s) noexcept {
        if (auto* next = waiters_.pop()) {
            ++reserved_;
            next->status = detail::wait_state::granted;
            s.request_wake(*next->task);
            return true;
        }
        ++available_;
        return false;
    }
public:
    constexpr explicit semaphore(std::uint32_t initial = 0) noexcept : available_(initial) {
        if (initial > Limit) __builtin_trap();
    }
    semaphore(const semaphore&) = delete;
    semaphore& operator=(const semaphore&) = delete;
    ~semaphore() { detail::require(waiters_.empty() && reserved_ == 0); }

    // Returns false at capacity, including grants not yet accepted by waiters.
    bool release_from_isr() noexcept {
        auto& s = scheduler<Port>::active();
        bool wake;
        {
            detail::critical_guard lock(s.port_);
            if (available_ == Limit - reserved_) return false;
            wake = give_locked(s);
        }
        if (wake) s.port_.notify_cpu();
        return true;
    }
    bool release() noexcept {
        scheduler<Port>::active().thread_only();
        return release_from_isr();
    }

    template<bool Timed> class acquire_awaiter {
        semaphore& semaphore_;
        detail::wait_record<Port> wait_;
        tick_t delay_;
    public:
        acquire_awaiter(semaphore& s, tick_t delay) noexcept : semaphore_(s), delay_(delay) {}
        acquire_awaiter(const acquire_awaiter&) = delete;
        acquire_awaiter& operator=(const acquire_awaiter&) = delete;
        ~acquire_awaiter() {
            if (!wait_.task) return;
            auto& s = scheduler<Port>::active();
            bool wake = false;
            {
                detail::critical_guard lock(s.port_);
                if (wait_.status == detail::wait_state::queued) semaphore_.waiters_.remove(wait_);
                else if (wait_.status == detail::wait_state::granted) {
                    --semaphore_.reserved_;
                    wake = semaphore_.give_locked(s);
                }
            }
            if (wake) s.port_.notify_cpu();
        }
        bool await_ready() const noexcept { return false; }
        template<class Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            auto& s = scheduler<Port>::active();
            s.thread_only();
            auto& n = scheduler<Port>::awaited_node(handle);
            if constexpr (Timed) detail::require(delay_ != no_deadline);
            {
                detail::critical_guard lock(s.port_);
                detail::require(n.status == detail::state::running);
                wait_.task = &n;
                if (semaphore_.available_) {
                    --semaphore_.available_; ++semaphore_.reserved_;
                    wait_.status = detail::wait_state::granted;
                    return false;
                }
                if constexpr (Timed) {
                    if (!delay_) { wait_.status = detail::wait_state::timed_out; return false; }
                }
                semaphore_.waiters_.push(wait_);
                n.wait = &wait_;
                n.status = Timed ? detail::state::timed_blocked : detail::state::blocked;
            }
            if constexpr (Timed) s.timers_.insert(n, delay_);
            return true;
        }
        auto await_resume() noexcept {
            auto& s = scheduler<Port>::active();
            detail::critical_guard lock(s.port_);
            bool granted = wait_.status == detail::wait_state::granted;
            detail::require(granted || (Timed && wait_.status == detail::wait_state::timed_out));
            if (granted) --semaphore_.reserved_;
            wait_.status = detail::wait_state::consumed;
            if constexpr (Timed) return granted;
        }
    };
    acquire_awaiter<false> acquire() noexcept { return {*this, 0}; }
    acquire_awaiter<true> acquire_for(tick_t delay) noexcept { return {*this, delay}; }
};
} // namespace coro_os
