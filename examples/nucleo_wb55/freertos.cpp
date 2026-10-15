#include "board.hpp"
extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}
static StaticTask_t task_tcbs[4], idle_tcb;
static StackType_t task_stacks[4][256], idle_stack[128];
static TaskHandle_t button_handle;
static void led_task(void* argument) {
    const auto led = reinterpret_cast<std::uintptr_t>(argument);
    for (;;) {
        vTaskDelay(nucleo::half_period(led, nucleo::fast()));
        nucleo::led_toggle(led);
    }
}
static void button_task(void*) {
    using namespace nucleo;
    enable_button_irq(); // Scheduler and notification target already exist.
    if (button_pressed()) xTaskNotifyGive(button_handle);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        std::uint8_t pressed = 0, released = 0;
        while (pressed < 30 && released < 30) {
            vTaskDelay(1);
            if (button_pressed()) { ++pressed; released = 0; }
            else { ++released; pressed = 0; }
        }
        if (released == 30) continue; // Short press or bounce, no stable press.
        accept_press();
        for (released = 0; released < 30;) {
            vTaskDelay(1);
            released = button_pressed() ? 0 : released + 1;
        }
    }
}
namespace nucleo {
void button_irq() noexcept {
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveFromISR(button_handle, &wake);
    portYIELD_FROM_ISR(wake);
}
}
extern "C" void vApplicationGetIdleTaskMemory(StaticTask_t** tcb, StackType_t** stack,
                                               configSTACK_DEPTH_TYPE* depth) {
    *tcb = &idle_tcb; *stack = idle_stack; *depth = 128;
}
extern "C" void firmware_assert() {
    nucleo::debug_state.fault = 0xA55E;
    __disable_irq(); for (;;) {}
}
extern "C" int main() {
    nucleo::init(); nucleo::debug_state.variant = 2;
    for (unsigned i = 0; i < 3; ++i) {
        auto handle = xTaskCreateStatic(led_task, "led", 256,
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(i)), 1,
            task_stacks[i], &task_tcbs[i]);
        configASSERT(handle);
    }
    button_handle = xTaskCreateStatic(button_task, "button", 256, nullptr, 2,
        task_stacks[3], &task_tcbs[3]);
    configASSERT(button_handle);
    vTaskStartScheduler();
    firmware_assert();
}
