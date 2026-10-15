#pragma once
#include <coro_os/scheduler.hpp>
#include <coro_os/ports/cortex_m.hpp>

using board_port = coro_os::ports::cortex_m::port;
using board_scheduler = coro_os::scheduler<board_port>;
using board_endpoint = coro_os::notification_endpoint<board_port>;

[[noreturn]] void run_scheduler(board_scheduler&);
void configure_board_irqs(); // Enable peripheral IRQ only after endpoint assignment.
