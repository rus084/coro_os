#pragma once
#include "detail/node.hpp"
#include <concepts>
#include <type_traits>

namespace coro_os {
template<class Port>
concept scheduler_port = requires(std::remove_reference_t<Port>& p,
                                  const std::remove_reference_t<Port>& cp,
                                  std::uint32_t saved) {
    { p.enter_critical() } noexcept -> std::same_as<std::uint32_t>;
    { p.exit_critical(saved) } noexcept -> std::same_as<void>;
    { p.notify_cpu() } noexcept -> std::same_as<void>;
    { cp.in_isr() } noexcept -> std::same_as<bool>;
};
namespace detail {
template<class Port> class critical_guard {
    Port& port_;
    std::uint32_t saved_;
public:
    explicit critical_guard(Port& port) noexcept
        : port_(port), saved_(port.enter_critical()) {}
    ~critical_guard() { port_.exit_critical(saved_); }
    critical_guard(const critical_guard&) = delete;
    critical_guard& operator=(const critical_guard&) = delete;
};
} // namespace detail
} // namespace coro_os
