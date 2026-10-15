#include <coro/task>

#if defined(REFERENCE_RESULT)
using invalid = coro::task<int&>::promise_type;
invalid storage;
#elif defined(CONST_RESULT)
using invalid = coro::task<const int>::promise_type;
invalid storage;
#elif defined(VOLATILE_RESULT)
using invalid = coro::task<volatile int>::promise_type;
invalid storage;
#elif defined(ARRAY_RESULT)
using invalid = coro::task<int[2]>::promise_type;
invalid storage;
#elif defined(THROWING_MOVE)
struct value { value(value&&) noexcept(false); };
using invalid = coro::task<value>::promise_type;
invalid storage;
#elif defined(THROWING_DESTRUCTOR)
struct value { value(value&&) noexcept; ~value() noexcept(false); };
using invalid = coro::task<value>::promise_type;
invalid storage;
#elif defined(THROWING_CONVERSION)
struct value { value(int) noexcept(false); value(value&&) noexcept; };
[[coro::noalloc]] coro::task<value> invalid() { co_return 1; }
#endif
