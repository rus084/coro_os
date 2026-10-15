#include <coro/task>
#include <type_traits>
#include <utility>

using task = coro::task<void>;
static_assert(std::is_nothrow_move_constructible_v<task>);
static_assert(noexcept(std::declval<task&>().resume()));
static_assert(noexcept(std::declval<const task&>().done()));
using resume_result = decltype(std::declval<task&>().resume());
static_assert(std::is_same_v<resume_result, void> || std::is_same_v<resume_result, bool>);
static_assert(std::is_same_v<decltype(std::declval<const task&>().done()), bool>);
static_assert(requires(task& value, void* context) {
    { value.bind_context(context) } noexcept;
});
