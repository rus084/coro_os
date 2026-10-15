#include <coro_os/scheduler.hpp>
namespace {
struct remote_port {
    static std::uint32_t enter_critical() noexcept { return 0; }
    static void exit_critical(std::uint32_t) noexcept {}
    static void notify_cpu() noexcept {}
    static bool in_isr() noexcept { return false; }
};
}
void create_scheduler_in_other_tu() {
    coro_os::scheduler scheduler(remote_port{});
}
