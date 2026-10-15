#pragma once
#include <coro_os/scheduler.hpp>
#include <cassert>
#include <utility>

struct test_port {
    unsigned depth = 0;
    unsigned signals = 0;
    bool isr = false;
    void (*on_enter)(test_port&) = nullptr;
    void (*on_exit)(test_port&) = nullptr;
    void* payload = nullptr;
    std::uint32_t enter_critical() noexcept {
        if (!depth && on_enter) {
            auto f = std::exchange(on_enter, nullptr); f(*this);
        }
        return depth++;
    }
    void exit_critical(std::uint32_t saved) noexcept {
        assert(depth == saved + 1);
        depth = saved;
        if (!depth && on_exit) {
            auto f = std::exchange(on_exit, nullptr); f(*this);
        }
    }
    void notify_cpu() noexcept { ++signals; }
    bool in_isr() const noexcept { return isr; }
};
using test_scheduler = coro_os::scheduler<test_port&>;
using test_endpoint = coro_os::notification_endpoint<test_port&>;
