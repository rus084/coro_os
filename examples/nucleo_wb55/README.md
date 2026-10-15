# P-NUCLEO-WB55 MB1355C: baremetal / FreeRTOS / coro_os

Три прошивки одного приложения на CPU1 Cortex-M4 STM32WB55RG.
LED1/PB5, LED2/PB0 и LED3/PB1 мигают с полными периодами 200/500/1000 мс.
SW1/PC4 переключает скорость между обычной и удвоенной. Debounce — 30 мс
стабильного нажатия/отпускания, удержание не повторяет переключения.
После reset LED выключены, режим обычный. Текущий интервал при смене скорости
заканчивается; следующий рассчитывается в новом режиме. SW2/SW3 не используются.

Общая аппаратная часть: HSI16 без PLL, SysTick1kHz, GPIO/EXTI через CMSIS,
один startup/vector/linker script. CPU2/радиостек не используются и не прошиваются.
Подключение — USB встроенного ST-LINK. При одном probe выбор автоматический;
при нескольких задайте flash.ps1 -Probe UID (список доступен через pyocd list).

## Сборка

Зависимости зафиксированы в dependencies.lock. prepare.ps1 скачивает CMSIS
headers с фиксированных commit и архив FreeRTOS. Можно вместо загрузки
экспортировать snapshot локального checkout через -FreeRTOSSource, без его изменения.
Лицензии находятся рядом с dependencies.

Из корня репозитория coro_os, Windows PowerShell:

```powershell
.\examples\nucleo_wb55\prepare.ps1
# Другой checkout FreeRTOS с нужным commit:
# .\examples\nucleo_wb55\prepare.ps1 -FreeRTOSSource C:\path\FreeRTOS-Kernel
wsl --exec env NOALLOC_CXX=/path/to/noalloc-clang/bin/clang++ NOALLOC_LLD=/path/to/ld.lld bash examples/nucleo_wb55/build.sh all
```

Нужны noalloc Clang23.1.3/LLD и ARM GCC14.2.1 C++ headers/binutils в WSL.
NOALLOC_CXX/NOALLOC_LLD задают другие пути. Все варианты собираются одним
Clang: Thumb Cortex-M4, fpv4-sp-d16, softfp, -Os, без LTO, с --gc-sections.
Heap отключён во всех вариантах. FreeRTOS использует static allocation и
direct-to-task notifications, без HAL/CMSIS-RTOS wrappers и timer task.
Idle/tick/stack-overflow hooks отключены. FreeRTOS использует собственный
xTickCount; отдельного счётчика времени приложения в этом варианте нет.
В FreeRTOS и coro_os debounce записан прямо в последовательной задаче:
30 последовательных выборок уровня с delay/sleep_for(1), одно событие нажатия,
затем ожидание 30 выборок отпускания. Абсолютные тики для debounce не читаются.
Только baremetal хранит явную debounce-машину. FreeRTOS idle работает штатно,
без добавленного приложением WFI-hook.

Артефакты: build-nucleo-wb55/{baremetal,freertos,coroutines}/firmware.{elf,bin,map},
symbols.txt, sections.txt, *.su. Файлы BIN непосредственно пригодны для записи
в 0x08000000; ELF содержит символы для отладки. Каждый образ ограничен 64KiB
Flash/64KiB SRAM1; это выбранный бюджет примера, не полный размер памяти MCU.

## Прошивка

Используется Windows pyOCD, а не устаревший ST-LINK Utility: последний видел
probe, но не подключался к этому MCU. Разовая подготовка из корня workspace:

```powershell
py -3.10 -m venv .\build-hardware-probe\venv
.\build-hardware-probe\venv\Scripts\python.exe -m pip install pyocd==0.45.1
Invoke-WebRequest 'https://github.com/Open-CMSIS-Pack/STM32WBxx_DFP/releases/download/v2.0.1/Keil.STM32WBxx_DFP.2.0.1.pack' -OutFile .\build-hardware-probe\Keil.STM32WBxx_DFP.2.0.1.pack
```

```powershell
.\examples\nucleo_wb55\flash.ps1 -Variant baremetal
.\examples\nucleo_wb55\flash.ps1 -Variant freertos
.\examples\nucleo_wb55\flash.ps1 -Variant coroutines
# Наблюдение уже записанного варианта без перезаписи:
.\examples\nucleo_wb55\flash.ps1 -Variant coroutines -ObserveOnly
```

hardware.py использует sector-only erase области приложения, readback verify,
reset/run. Backup/restore и изменение option bytes не выполняются. Перед flash
алгоритмом останавливаются SysTick/NVIC старой программы. После проверки
прошивка остаётся на плате. Для другого probe используйте hardware.py --probe UID.

Снимки debug_state в hardware-result.json кратковременно останавливают и
возобновляют CPU: без halt SRAM спящего WB55 может читаться нулями. Эти снимки
доказывают прогресс, но не являются точным измерением задержек или энергопотребления.
Поле fault и CFSR/HFSR должны быть нулевыми; tick/LED counters — увеличиваться.
ObserveOnly тоже сверяет Flash с текущим BIN. Успешный hardware-result содержит
status=passed, flash_verified=true и SHA256 образа; отчёт отвергает неуспешные
или устаревшие результаты. Начатая, но не завершённая проверка сохраняет incomplete.

## Размеры

Проверенная сборка на 2026-10-15:

| Вариант | Flash, байт | .data+.bss | В том числе task stacks | Общий MSP reserve | RAM с резервами |
|---|---:|---:|---:|---:|---:|
| baremetal | 1404 | 48 | 0 | 2048 | 2096 |
| FreeRTOS | 4932 | 5224 | 4608 | 2048 | 7272 |
| coro_os | 3352 | 224 | 0 | 2048 | 2272 |

Резерв MSP одинаковый — 2KiB. Main frame внутри него занимает16/16/184 байта
соответственно; добавлять эти размеры поверх резерва не нужно. В coro_os сюда
входят scheduler20 и четыре job40. Значения .su не дают максимальную глубину
общего стека вызовов/IRQ.

FreeRTOS: четыре application stack по256 слов и idle128, всего4608 байт.
Это консервативные резервы, не утверждение о минимальном RAM FreeRTOS.
High-water instrumentation удалена вместе с idle hook.
TCB занимают72 байта каждый; debug_state общий для всех вариантов —36 байт.
В coro_os три LED frame занимают120 байт суммарно, button frame48.

Flash включает vector table320, .ARM.exidx, padding, startup и измерительные
счётчики. Это реальные образы для платы; BLE/CPU2 и полноценные драйверы в них
не включены. Отдельного heap/per-task stack у coro_os нет.

Полная раскладка по объектам и RAM-символам генерируется:

```powershell
.\build-hardware-probe\venv\Scripts\python.exe .\examples\nucleo_wb55\report.py
```

Результаты: build-nucleo-wb55/size-report.{md,json}, карты каждого образа.
Размер BIN включает load image .data; .data также учитывается в RAM.

## Проверки

Host nucleo_behavior проверяет реальные debounce-переходы, hold/release,
стартовое удержание и wrap времени. Общий CTest включает прежние noalloc/OS tests.
Сборка проверяет undefined/allocator symbols. Startup проверяется в реальном
запуске как с пустой .data (baremetal/coro_os), так и непустой (FreeRTOS).
Визуальная проверка SW1/LED фиксируется отдельно от инструментальных счётчиков.
Все три варианта прошли запись/readback и аппаратный запуск с растущими LED/tick
счётчиками и нулевыми fault/CFSR/HFSR. Baremetal также визуально подтверждён
пользователем, включая удержание SW1. FreeRTOS/coro_os прошли два нажатия через
PC4 open-drain, управляемый SWD: проверяются GPIO, EXTI, debounce, удержание и
возврат режима, отклонение короткого нажатия и дребезг отпускания;
механический контакт кнопки этим методом не проверяется.
Для воспроизведения используйте flash.ps1 -ExerciseButton; вход PC4 восстанавливается
после теста. На плате оставлен coro_os.

После сборки и аппаратного прогона проверка надёжности отчётов запускается
probe-venv Python командой `python tests/nucleo_evidence_test.py` из coro_os.
Опция `--hardware-stale-check` дополнительно временно меняет локальный BIN
и проверяет, что observe-only отвергает его; Flash платы не изменяется, BIN
возвращается к исходным байтам, после теста нужно повторить ObserveOnly для
обновления успешного hardware-result.

Источники аппаратных определений: ST cmsis-device-wb и
https://github.com/STMicroelectronics/p-nucleo-wb55-nucleo-bsp.
