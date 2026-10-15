#pragma once
#include "node.hpp"

namespace coro_os::detail {
// Thread mode only. ISR never touches these links.
template<class Port> class ready_queue {
    node<Port>* head_ = nullptr;
    node<Port>* tail_ = nullptr;
public:
    bool empty() const noexcept { return head_ == nullptr; }
    void push(node<Port>& n) noexcept {
        require(!n.in_ready && !n.in_timer);
        n.in_ready = true;
        n.queue_next = nullptr;
        if (tail_) tail_->queue_next = &n; else head_ = &n;
        tail_ = &n;
    }
    node<Port>* pop() noexcept {
        node<Port>* n = head_;
        if (!n) return nullptr;
        head_ = n->queue_next;
        if (!head_) tail_ = nullptr;
        n->queue_next = nullptr;
        n->in_ready = false;
        return n;
    }
    void remove(node<Port>& n) noexcept {
        if (!n.in_ready) return;
        node<Port>* prev = nullptr;
        node<Port>* p = head_;
        while (p && p != &n) { prev = p; p = p->queue_next; }
        require(p != nullptr);
        if (prev) prev->queue_next = n.queue_next; else head_ = n.queue_next;
        if (tail_ == &n) tail_ = prev;
        n.queue_next = nullptr;
        n.in_ready = false;
    }
};

template<class Port> class timer_queue {
    node<Port>* head_ = nullptr;
public:
    tick_t next_delay() const noexcept { return head_ ? head_->delta : no_deadline; }
    void insert(node<Port>& n, tick_t delay) noexcept {
        require(delay != no_deadline && !n.in_timer && !n.in_ready);
        node<Port>** link = &head_;
        while (*link && delay >= (*link)->delta) {
            delay -= (*link)->delta;
            link = &(*link)->queue_next;
        }
        n.delta = delay;
        n.queue_next = *link;
        if (*link) (*link)->delta -= delay;
        *link = &n;
        n.in_timer = true;
    }
    void remove(node<Port>& n) noexcept {
        if (!n.in_timer) return;
        node<Port>** link = &head_;
        while (*link && *link != &n) link = &(*link)->queue_next;
        require(*link != nullptr);
        *link = n.queue_next;
        if (*link) (*link)->delta += n.delta;
        n.queue_next = nullptr;
        n.in_timer = false;
    }
    node<Port>* advance_one(tick_t& remaining) noexcept {
        if (!head_) { remaining = 0; return nullptr; }
        if (head_->delta > remaining) {
            head_->delta -= remaining;
            remaining = 0;
            return nullptr;
        }
        node<Port>* n = head_;
        remaining -= n->delta;
        head_ = n->queue_next;
        n->queue_next = nullptr;
        n->in_timer = false;
        return n;
    }
};
} // namespace coro_os::detail
