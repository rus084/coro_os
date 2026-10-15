#pragma once
#include "scheduler.hpp"

namespace coro_os {
template<class Port> class notification_endpoint {
    template<class Task, class OtherPort> friend class job;
    detail::node<Port>* node_ = nullptr;
    explicit notification_endpoint(detail::node<Port>& n) noexcept : node_(&n) {}
public:
    notification_endpoint() noexcept = default;
    void signal() const noexcept {
        detail::require(node_ != nullptr);
        auto& s = scheduler<Port>::active();
        s.thread_only();
        s.signal(*node_);
    }
    // Also safe in thread mode; never calls resume or traverses timers.
    void signal_from_isr() const noexcept {
        detail::require(node_ != nullptr);
        scheduler<Port>::active().signal(*node_);
    }
};

template<class Task, class Port>
class job {
    friend class scheduler<Port>;
    // Node outlives Task, including user destructors run by Task's destructor.
    detail::node<Port> node_;
    Task task_;
    job(scheduler<Port>& s, Task&& task) noexcept : task_(std::move(task)) {
        static_assert(std::is_nothrow_move_constructible_v<Task>);
        static_assert(noexcept(std::declval<Task&>().resume()));
        static_assert(noexcept(std::declval<const Task&>().done()));
        node_.task = &task_;
        node_.resume = [](void* p) noexcept { static_cast<Task*>(p)->resume(); };
        node_.done = [](const void* p) noexcept { return static_cast<const Task*>(p)->done(); };
        if constexpr (requires { task_.bind_context(static_cast<void*>(&node_)); }) {
            static_assert(noexcept(task_.bind_context(static_cast<void*>(&node_))));
            task_.bind_context(&node_);
        }
        s.attach(node_);
    }
public:
    notification_endpoint<Port> notifier() noexcept { return notification_endpoint<Port>(node_); }
    ~job() { scheduler<Port>::active().detach(node_); }
    job(const job&) = delete;
    job& operator=(const job&) = delete;
    job(job&&) = delete;
    job& operator=(job&&) = delete;
};

template<class Port> template<class Task>
job<std::remove_cvref_t<Task>, Port> scheduler<Port>::spawn(Task&& task) noexcept {
    static_assert(!std::is_lvalue_reference_v<Task>, "spawn requires an owning rvalue task");
    return job<std::remove_cvref_t<Task>, Port>(*this, std::forward<Task>(task));
}
} // namespace coro_os
