#include "board.hpp"
static volatile bool button_pending = false;
namespace nucleo {
void button_irq() noexcept { button_pending = true; }
}
extern "C" void SysTick_Handler() { nucleo::tick_irq(); }
extern "C" int main() {
    using namespace nucleo;
    init(); debug_state.variant = 1;
    debounce button;
    std::uint32_t due[] = {100, 250, 500};
    enable_button_irq();
    if (button_pressed()) button_irq();
    start_ticks();
    for (;;) {
        auto now = ticks();
        auto mask = __get_PRIMASK(); __disable_irq();
        bool pending = button_pending; button_pending = false;
        __set_PRIMASK(mask);
        if (pending) button.start(now, button_pressed());
        if (button.sample(now, button_pressed())) accept_press();
        for (unsigned led = 0; led != 3; ++led) {
            if (static_cast<std::int32_t>(now - due[led]) >= 0) {
                led_toggle(led); due[led] = now + half_period(led, fast());
            }
        }
        __SEV(); __WFE();
        if (ticks() == now && !button_pending) idle();
    }
}
