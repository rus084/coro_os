#include <cstddef>
extern "C" {
void* memset(void* dest, int value, std::size_t count) {
    auto* d = static_cast<volatile unsigned char*>(dest);
    while (count--) *d++ = static_cast<unsigned char>(value);
    return dest;
}
void* memcpy(void* dest, const void* source, std::size_t count) {
    auto* d = static_cast<volatile unsigned char*>(dest);
    auto* s = static_cast<const volatile unsigned char*>(source);
    while (count--) *d++ = *s++;
    return dest;
}
void __aeabi_memcpy(void* d, const void* s, std::size_t n) { memcpy(d,s,n); }
void __aeabi_memcpy4(void* d, const void* s, std::size_t n) { memcpy(d,s,n); }
void __aeabi_memcpy8(void* d, const void* s, std::size_t n) { memcpy(d,s,n); }
void __aeabi_memclr(void* d, std::size_t n) { memset(d,0,n); }
void __aeabi_memclr4(void* d, std::size_t n) { memset(d,0,n); }
void __aeabi_memclr8(void* d, std::size_t n) { memset(d,0,n); }
}
