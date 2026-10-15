# STM32WB55 comparison

Clang23.1.3, Cortex-M4, softfp, -Os, no LTO, section GC.
Same startup/clock/GPIO/debug counters; 1kHz SysTick, HSI16.

| Variant | Flash bytes | .data+.bss | Task stacks included in .bss | MSP reserve | Total reserved RAM |
|---|---:|---:|---:|---:|---:|
| baremetal | 1404 | 48 | 0 | 2048 | 2096 |
| freertos | 4932 | 5224 | 4608 | 2048 | 7272 |
| coroutines | 3352 | 224 | 0 | 2048 | 2272 |

## baremetal

Linked Flash contributions:
- startup.o: 524
- board.o: 540
- baremetal.o: 318
- exidx/alignment: 22

RAM objects:
- _ZL14button_pending: 1
- debug_state: 36
- board_ticks: 4

main .su (included in MSP reserve, do not add twice):
coro_os/examples/nucleo_wb55/baremetal.cpp:7:main	16	static

## freertos

Linked Flash contributions:
- startup.o: 524
- board.o: 472
- memory.o: 14
- freertos.o: 484
- tasks.o: 2472
- list.o: 118
- port.o: 786
- exidx/alignment: 62

RAM objects:
- _ZL13button_handle: 4
- _ZL8idle_tcb: 72
- _ZL10idle_stack: 512
- _ZL11task_stacks: 4096
- _ZL9task_tcbs: 288
- uxCurrentNumberOfTasks: 4
- xSchedulerRunning: 4
- pxReadyTasksLists: 60
- xDelayedTaskList1: 20
- xDelayedTaskList2: 20
- xPendingReadyList: 20
- xSuspendedTaskList: 20
- pxDelayedTaskList: 4
- pxOverflowDelayedTaskList: 4
- uxTaskNumber: 4
- uxTopReadyPriority: 4
- uxSchedulerSuspended: 4
- xTickCount: 4
- xNextTaskUnblockTime: 4
- xYieldPendings: 4
- xPendedTicks: 4
- xIdleTaskHandles: 4
- xNumOfOverflows: 4
- uxCriticalNesting: 4
- ucMaxSysCallPriority: 1
- ulMaxPRIGROUPValue: 4
- debug_state: 36
- pxCurrentTCB: 4

main .su (included in MSP reserve, do not add twice):
coro_os/examples/nucleo_wb55/freertos.cpp:51:main	16	static

## coroutines

Linked Flash contributions:
- startup.o: 524
- board.o: 540
- coroutines.o: 2254
- exidx/alignment: 34

RAM objects:
- _ZL15button_endpoint: 4
- _ZL10led_frames: 120
- _ZL12button_frame: 48
- debug_state: 36
- board_ticks: 4
- _ZN7coro_os9schedulerINS_5ports8cortex_m4portEE9instance_E: 4
- _ZN7coro_os6detail16scheduler_activeE: 1

main .su (included in MSP reserve, do not add twice):
coro_os/examples/nucleo_wb55/coroutines.cpp:40:main	184	static

MSP reserve is a chosen budget, not measured worst-case call/IRQ stack.
FreeRTOS application stacks are 4*256 words + idle128, not a minimum-RAM claim.
Hardware snapshots briefly halt/resume CPU: counters prove progress, not precise timing.
Images include vector table, exidx/alignment, instrumentation and memory helpers.
No BLE/CPU2 stack, HAL, allocator, C library startup or per-task stack in coro_os.
