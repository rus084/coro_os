#pragma once
#include "node.hpp"

namespace coro_os::detail {
template<class Port> class wait_queue;
enum class wait_state : std::uint8_t { idle, queued, granted, timed_out, consumed };

// Embedded in a coroutine awaiter. All published accesses hold the port guard.
template<class Port> struct wait_record {
    wait_record* prev = nullptr;
    wait_record* next = nullptr;
    node<Port>* task = nullptr;
    wait_queue<Port>* queue = nullptr;
    wait_state status = wait_state::idle;
};

template<class Port> class wait_queue {
    wait_record<Port>* head_ = nullptr;
    wait_record<Port>* tail_ = nullptr;
public:
    bool empty() const noexcept { return head_ == nullptr; }
    void push(wait_record<Port>& wait) noexcept {
        require(wait.queue == nullptr && wait.status == wait_state::idle);
        wait.queue = this;
        wait.prev = tail_;
        wait.next = nullptr;
        if (tail_) tail_->next = &wait; else head_ = &wait;
        tail_ = &wait;
        wait.status = wait_state::queued;
    }
    void remove(wait_record<Port>& wait) noexcept {
        require(wait.queue == this);
        if (wait.prev) wait.prev->next = wait.next; else head_ = wait.next;
        if (wait.next) wait.next->prev = wait.prev; else tail_ = wait.prev;
        wait.prev = wait.next = nullptr;
        wait.queue = nullptr;
    }
    wait_record<Port>* pop() noexcept {
        auto* wait = head_;
        if (wait) remove(*wait);
        return wait;
    }
};
} // namespace coro_os::detail
