#pragma once
#include "scheduler.hpp"

namespace coro_os {
template<class Port> class mutex {
    detail::wait_queue<Port> waiters_;
    detail::node<Port>* owner_ = nullptr;

    bool unlock_locked(scheduler<Port>& s, detail::node<Port>* owner) noexcept {
        detail::require(owner_ == owner);
        if (auto* next = waiters_.pop()) {
            owner_ = next->task;
            next->status = detail::wait_state::granted;
            s.request_wake(*next->task);
            return true;
        }
        owner_ = nullptr;
        return false;
    }
    void unlock(detail::node<Port>* owner) noexcept {
        auto& s = scheduler<Port>::active();
        s.thread_only();
        bool wake;
        { detail::critical_guard lock(s.port_); wake = unlock_locked(s, owner); }
        if (wake) s.port_.notify_cpu();
    }
public:
    mutex() noexcept = default;
    mutex(const mutex&) = delete;
    mutex& operator=(const mutex&) = delete;
    ~mutex() { detail::require(owner_ == nullptr && waiters_.empty()); }

    class guard {
        friend class mutex;
        mutex* mutex_;
        detail::node<Port>* owner_;
        guard(mutex& m, detail::node<Port>* n) noexcept : mutex_(&m), owner_(n) {}
    public:
        guard(const guard&) = delete;
        guard& operator=(const guard&) = delete;
        guard(guard&& other) noexcept : mutex_(std::exchange(other.mutex_, nullptr)), owner_(other.owner_) {}
        guard& operator=(guard&& other) noexcept {
            if (this != &other) {
                unlock(); mutex_ = std::exchange(other.mutex_, nullptr); owner_ = other.owner_;
            }
            return *this;
        }
        ~guard() { unlock(); }
        void unlock() noexcept {
            if (auto* m = std::exchange(mutex_, nullptr)) m->unlock(owner_);
        }
    };

    class lock_awaiter {
        mutex& mutex_;
        detail::wait_record<Port> wait_;
    public:
        explicit lock_awaiter(mutex& m) noexcept : mutex_(m) {}
        lock_awaiter(const lock_awaiter&) = delete;
        lock_awaiter& operator=(const lock_awaiter&) = delete;
        ~lock_awaiter() {
            if (!wait_.task) return;
            auto& s = scheduler<Port>::active();
            bool wake = false;
            {
                detail::critical_guard lock(s.port_);
                if (wait_.status == detail::wait_state::queued) mutex_.waiters_.remove(wait_);
                else if (wait_.status == detail::wait_state::granted) wake = mutex_.unlock_locked(s, wait_.task);
            }
            if (wake) s.port_.notify_cpu();
        }
        bool await_ready() const noexcept { return false; }
        template<class Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            auto& s = scheduler<Port>::active();
            s.thread_only();
            auto& n = scheduler<Port>::awaited_node(handle);
            detail::critical_guard lock(s.port_);
            detail::require(n.status == detail::state::running && mutex_.owner_ != &n);
            wait_.task = &n;
            if (!mutex_.owner_) {
                mutex_.owner_ = &n;
                wait_.status = detail::wait_state::granted;
                return false;
            }
            mutex_.waiters_.push(wait_);
            n.wait = &wait_;
            n.status = detail::state::blocked;
            return true;
        }
        guard await_resume() noexcept {
            auto& s = scheduler<Port>::active();
            detail::critical_guard lock(s.port_);
            detail::require(wait_.status == detail::wait_state::granted);
            wait_.status = detail::wait_state::consumed;
            return guard(mutex_, wait_.task);
        }
    };
    lock_awaiter lock() noexcept { return lock_awaiter(*this); }
};
} // namespace coro_os
