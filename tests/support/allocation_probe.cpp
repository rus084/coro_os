#include <cassert>
#include <cstdlib>
#include <new>

// Separate translation unit: GCC's inlined mismatched-new-delete diagnostic
// cannot interpret a replacement delete's free() as an unrelated deallocator.
bool forbid_allocations = false;
void* operator new(std::size_t size) {
    assert(!forbid_allocations);
    if (void* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
