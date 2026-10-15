#pragma once
#include "test_port.hpp"

// Host-only idle contract with deterministic hooks around simulated sleep.
struct idle_port : test_port {
    bool event = false;
    bool sleeping = false;
    void (*before_sleep)(idle_port&) = nullptr;
    void (*while_sleeping)(idle_port&) = nullptr;

    void notify_cpu() noexcept {
        test_port::notify_cpu();
        event = true;
        sleeping = false;
    }
    template<class Ready> void idle_if(Ready ready) noexcept {
        assert(depth == 0 && !isr);
        sleeping = false;
        if (ready()) return;
        if (auto hook = std::exchange(before_sleep, nullptr)) hook(*this);
        if (std::exchange(event, false)) return;
        sleeping = true;
        if (auto hook = std::exchange(while_sleeping, nullptr)) hook(*this);
        if (std::exchange(event, false)) sleeping = false;
    }
};
