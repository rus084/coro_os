#include "board.hpp"
extern "C" {
extern std::uint32_t _estack;
extern std::uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[];
extern void (*__init_array_start[])();
extern void (*__init_array_end[])();
int main();
void Reset_Handler();
void EXTI4_IRQHandler();
void SysTick_Handler();
void Default_Handler() { nucleo::debug_state.fault = __get_IPSR(); __disable_irq(); for (;;) {} }
void SVC_Handler() __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler() __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler() __attribute__((weak, alias("Default_Handler")));
using handler = void (*)();
#define D Default_Handler
#define D8 D,D,D,D,D,D,D,D
__attribute__((section(".isr_vector"), used))
const handler vector_table[] = {
    reinterpret_cast<handler>(&_estack), Reset_Handler,
    D, HardFault_Handler, D,D,D,D,D,D,D,SVC_Handler,D,D,PendSV_Handler,SysTick_Handler,
    D8,D,D,EXTI4_IRQHandler,D,D,D,D,D, // IRQ0..15
    D8,D8,D8,D8,D8,D8 // IRQ16..63
};
#undef D8
#undef D
void Reset_Handler() {
    __disable_irq();
    // Linker boundaries may have identical addresses for empty sections.
    // Integer lengths avoid C++ distinct-object pointer assumptions.
    const auto data_words = (reinterpret_cast<std::uintptr_t>(&_edata) -
                             reinterpret_cast<std::uintptr_t>(&_sdata)) / 4;
    const auto bss_words = (reinterpret_cast<std::uintptr_t>(&_ebss) -
                            reinterpret_cast<std::uintptr_t>(&_sbss)) / 4;
    for (std::uintptr_t i = 0; i < data_words; ++i) _sdata[i] = _sidata[i];
    for (std::uintptr_t i = 0; i < bss_words; ++i) _sbss[i] = 0;
    SCB->VTOR = reinterpret_cast<std::uint32_t>(vector_table);
    for (auto* init = __init_array_start; init != __init_array_end; ++init) (*init)();
    __enable_irq();
    (void)main();
    Default_Handler();
}
}
