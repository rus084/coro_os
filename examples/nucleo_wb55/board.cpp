#include "board.hpp"

extern "C" {
std::uint32_t SystemCoreClock = 16000000;
volatile nucleo::debug_info debug_state{};
volatile std::uint32_t board_ticks = 0;
}
namespace nucleo {
void init() noexcept {
    SCB->CPACR |= 0xFu << 20;
    __DSB(); __ISB();
    RCC->CR = RCC->CR | RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {}
    // Preserve conservative flash latency while switching clocks.
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_SW | RCC_CFGR_HPRE |
                    RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) | RCC_CFGR_SW_0;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_0) {}
    RCC->AHB2ENR = RCC->AHB2ENR | RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN;
    (void)RCC->AHB2ENR;
    constexpr std::uint32_t led_mask = (1u << 5) | 3u;
    GPIOB->BSRR = led_mask << 16;
    GPIOB->OTYPER = GPIOB->OTYPER & ~led_mask;
    GPIOB->PUPDR = GPIOB->PUPDR & ~((3u << 10) | 15u);
    GPIOB->MODER = (GPIOB->MODER & ~((3u << 10) | 15u)) | (1u << 10) | 5u;
    GPIOC->MODER = GPIOC->MODER & ~(3u << 8);
    GPIOC->PUPDR = (GPIOC->PUPDR & ~(3u << 8)) | (1u << 8);
    SYSCFG->EXTICR[1] = (SYSCFG->EXTICR[1] & ~15u) | 2u;
    EXTI->RTSR1 = EXTI->RTSR1 & ~(1u << 4);
    EXTI->FTSR1 = EXTI->FTSR1 | (1u << 4);
    EXTI->IMR1 = EXTI->IMR1 & ~(1u << 4);
    EXTI->PR1 = 1u << 4;
    NVIC_SetPriority(EXTI4_IRQn, 6);
    debug_state.signature = 0x57423535;
}
void enable_button_irq() noexcept {
    EXTI->PR1 = 1u << 4;
    NVIC_ClearPendingIRQ(EXTI4_IRQn);
    EXTI->IMR1 = EXTI->IMR1 | (1u << 4);
    NVIC_EnableIRQ(EXTI4_IRQn);
}
void start_ticks() noexcept {
    SysTick_Config(16000);
    NVIC_SetPriority(SysTick_IRQn, 15);
}
void tick_irq() noexcept { board_ticks = board_ticks + 1; __SEV(); }
std::uint32_t ticks() noexcept { return board_ticks; }
bool button_pressed() noexcept { return !(GPIOC->IDR & (1u << 4)); }
void led_toggle(unsigned led) noexcept {
    constexpr unsigned pins[] = {5, 0, 1};
    const auto mask = 1u << pins[led];
    GPIOB->BSRR = (GPIOB->ODR & mask) ? mask << 16 : mask;
    debug_state.toggles[led] = debug_state.toggles[led] + 1;
}
bool fast() noexcept { return __atomic_load_n(&debug_state.fast, __ATOMIC_RELAXED) != 0; }
void accept_press() noexcept {
    __atomic_store_n(&debug_state.fast, fast() ? 0u : 1u, __ATOMIC_RELAXED);
    debug_state.press_count = debug_state.press_count + 1;
}
} // namespace nucleo
extern "C" void EXTI4_IRQHandler() {
    if (EXTI->PR1 & (1u << 4)) {
        EXTI->PR1 = 1u << 4;
        debug_state.irq_count = debug_state.irq_count + 1;
        nucleo::button_irq();
        __SEV();
    }
}
