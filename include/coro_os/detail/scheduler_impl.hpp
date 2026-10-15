#pragma once

namespace coro_os {

template<class Port>
scheduler<Port>::scheduler(Port port) noexcept : port_(std::forward<Port>(port)) {
    thread_only();
    detail::require(!detail::scheduler_active);
    detail::scheduler_active = true;
    instance_ = this;
}
template<class Port>
scheduler<Port>::~scheduler() {
    thread_only();
    detail::require(instance_ == this && detail::scheduler_active);
    instance_ = nullptr;
    detail::scheduler_active = false;
}
template<class Port>
void scheduler<Port>::thread_only() const noexcept { detail::require(!port_.in_isr()); }
template<class Port>
void scheduler<Port>::attach(detail::node<Port>& n) noexcept {
    thread_only();
    detail::require(n.status == detail::state::detached && !n.done(n.task));
    {
        detail::critical_guard lock(port_);
        n.status = detail::state::ready;
        n.irq.accepts_signals = true;
    }
    ready_.push(n);
}
template<class Port>
void scheduler<Port>::detach(detail::node<Port>& n) noexcept {
    thread_only();
    detail::require(n.status != detail::state::detached && n.status != detail::state::running);
    {
        detail::critical_guard lock(port_);
        n.irq.accepts_signals = false;
        n.irq.wake_requested = false;
        n.status = detail::state::detached;
        n.result = nullptr;
        remove_pending(n);
    }
    ready_.remove(n);
    timers_.remove(n);
    // Node outlives Task destruction: signals then see irq.accepts_signals=false.
}
template<class Port>
bool scheduler<Port>::suspend_sleep(detail::node<Port>& n, tick_t delay) noexcept {
    thread_only();
    detail::require(n.status == detail::state::running && delay != no_deadline);
    {
        detail::critical_guard lock(port_);
        n.status = delay ? detail::state::sleeping : detail::state::ready;
    }
    if (delay) timers_.insert(n, delay); else ready_.push(n);
    return true;
}
template<class Port>
bool scheduler<Port>::has_work() noexcept {
    thread_only();
    detail::critical_guard lock(port_);
    return pending_head_ || !ready_.empty() || timers_.next_delay() == 0;
}
template<class Port>
void scheduler<Port>::remove_pending(detail::node<Port>& n) noexcept {
    if (!n.irq.in_pending) return;
    if (n.pending_prev) n.pending_prev->pending_next = n.pending_next;
    else pending_head_ = n.pending_next;
    if (n.pending_next) n.pending_next->pending_prev = n.pending_prev;
    else pending_tail_ = n.pending_prev;
    n.pending_next = n.pending_prev = nullptr;
    n.irq.in_pending = false;
}
template<class Port>
void scheduler<Port>::enqueue_pending(detail::node<Port>& n) noexcept {
    if (n.irq.in_pending) return;
    n.irq.in_pending = true;
    n.pending_prev = pending_tail_;
    n.pending_next = nullptr;
    if (pending_tail_) pending_tail_->pending_next = &n;
    else pending_head_ = &n;
    pending_tail_ = &n;
}
template<class Port>
void scheduler<Port>::request_wake(detail::node<Port>& n) noexcept {
    if (!n.irq.accepts_signals) return;
    detail::require(n.status == detail::state::blocked || n.status == detail::state::timed_blocked);
    n.irq.wake_requested = true;
    enqueue_pending(n);
}
template<class Port>
void scheduler<Port>::signal(detail::node<Port>& n) noexcept {
    {
        detail::critical_guard lock(port_);
        if (!n.irq.accepts_signals) return;
        n.irq.token = true;
        enqueue_pending(n);
    }
    port_.notify_cpu();
}
template<class Port>
bool scheduler<Port>::suspend_wait(detail::node<Port>& n, tick_t delay, bool timed, wait_result& result) noexcept {
    thread_only();
    detail::require(n.status == detail::state::running);
    detail::require(!timed || delay != no_deadline);
    {
        detail::critical_guard lock(port_);
        if (n.irq.token) {
            n.irq.token = false;
            result = wait_result::notified;
            return false;
        }
        if (timed && delay == 0) {
            result = wait_result::timeout;
            return false;
        }
        n.result = &result;
        n.status = timed ? detail::state::timed_wait : detail::state::waiting;
    }
    if (timed) timers_.insert(n, delay);
    return true;
}
template<class Port>
void scheduler<Port>::drain_pending() noexcept {
    // Stop at the original tail even if ISR requeues nodes. No task resumes
    // during draining, so the original tail cannot be cancelled or destroyed.
    detail::node<Port>* last;
    { detail::critical_guard lock(port_); last = pending_tail_; }
    if (!last) return;
    for (;;) {
        detail::node<Port>* n;
        bool wake = false;
        {
            detail::critical_guard lock(port_);
            n = pending_head_;
            if (!n) break;
            remove_pending(*n);
            if (n->irq.wake_requested) {
                detail::require(n->wait && n->wait->status == detail::wait_state::granted);
                n->irq.wake_requested = false;
                n->wait = nullptr;
                n->status = detail::state::ready;
                wake = true;
            } else if (n->irq.token && (n->status == detail::state::waiting || n->status == detail::state::timed_wait)) {
                n->irq.token = false;
                *n->result = wait_result::notified;
                n->result = nullptr;
                n->status = detail::state::ready;
                wake = true;
            }
        }
        if (wake) {
            timers_.remove(*n);
            ready_.push(*n);
        }
        if (n == last) break;
    }
}
template<class Port>
tick_t scheduler<Port>::poll(tick_t elapsed) noexcept {
    thread_only();
    while (auto* n = timers_.advance_one(elapsed)) {
        {
            detail::critical_guard lock(port_);
            detail::require(n->status == detail::state::sleeping || n->status == detail::state::timed_wait || n->status == detail::state::timed_blocked);
            if (n->status == detail::state::timed_blocked) {
                auto& wait = *n->wait;
                if (wait.status == detail::wait_state::queued) {
                    wait.queue->remove(wait);
                    wait.status = detail::wait_state::timed_out;
                } else {
                    detail::require(wait.status == detail::wait_state::granted);
                }
                n->wait = nullptr;
                n->irq.wake_requested = false;
            } else if (n->status == detail::state::timed_wait) {
                *n->result = n->irq.token ? wait_result::notified : wait_result::timeout;
                n->irq.token = false;
                n->result = nullptr;
            }
            n->status = detail::state::ready;
        }
        ready_.push(*n);
    }
    for (;;) {
        drain_pending();
        auto* n = ready_.pop();
        if (!n) break;
        {
            detail::critical_guard lock(port_);
            detail::require(n->status == detail::state::ready);
            n->status = detail::state::running;
        }
        n->resume(n->task);
        {
            detail::critical_guard lock(port_);
            if (n->done(n->task)) {
                detail::require(n->status == detail::state::running);
                n->status = detail::state::completed;
                n->irq.accepts_signals = false;
                remove_pending(*n);
            } else {
                // An arbitrary suspend_always would leave the job unscheduled.
                detail::require(n->status != detail::state::running);
            }
        }
    }
    detail::critical_guard lock(port_);
    return (pending_head_ || !ready_.empty()) ? 0 : timers_.next_delay();
}
} // namespace coro_os
