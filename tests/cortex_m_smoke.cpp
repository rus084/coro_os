#include <coro_os/scheduler.hpp>
#include <coro_os/ports/cortex_m.hpp>
#include <cstddef>

// This fixture checks kernel/port linkage, not the noalloc coroutine ABI.
static unsigned resumes;
static unsigned destroys;
static coro_os::notification_endpoint<coro_os::ports::cortex_m::port> irq_endpoint;
struct callback_task {
    bool live = true;
    bool complete = false;
    callback_task() noexcept = default;
    callback_task(callback_task&& other) noexcept { other.live = false; }
    ~callback_task() { if (live) ++destroys; }
    void resume() noexcept { ++resumes; complete = true; }
    bool done() const noexcept { return complete; }
};

// A freestanding board supplies memory primitives; these are not allocators.
extern "C" void* memset(void* dest, int value, std::size_t size) {
    auto* p = static_cast<volatile unsigned char*>(dest);
    while (size--) *p++ = static_cast<unsigned char>(value);
    return dest;
}
extern "C" void* memcpy(void* dest, const void* src, std::size_t size) {
    auto* d = static_cast<volatile unsigned char*>(dest);
    auto* s = static_cast<const unsigned char*>(src);
    while (size--) *d++ = *s++;
    return dest;
}
extern "C" void __aeabi_memclr(void* p, std::size_t n) { memset(p, 0, n); }
extern "C" void __aeabi_memclr4(void* p, std::size_t n) { memset(p, 0, n); }
extern "C" void IRQ_Handler() { irq_endpoint.signal_from_isr(); }

extern "C" [[noreturn]] void _start() {
    namespace cm = coro_os::ports::cortex_m;
    cm::port port{};
    {
        coro_os::detail::critical_guard outer(port);
        { coro_os::detail::critical_guard inner(port); }
        coro_os::detail::require(cm::primask() == 1);
    }
    {
        coro_os::scheduler scheduler(port);
        auto job = scheduler.spawn(callback_task{});
        irq_endpoint = job.notifier();
        IRQ_Handler();
        scheduler.poll(0);
        coro_os::detail::require(resumes == 1);
        cm::idle_if([&]() noexcept { return scheduler.has_work(); });
    }
    coro_os::detail::require(destroys == 1);
    for (;;) asm volatile("wfe" ::: "memory");
}
