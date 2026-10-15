# Coro OS

Переносимое кооперативное ядро C++20 и порт ARM Cortex-M0/M0+/M3/M4.
Ядро не выделяет память, не создаёт стеков задач и не переключает аппаратный
контекст. Оно вызывает `resume()` готовых корутин из основного цикла.

Для production-корутин требуется новый Clang с `[[coro::noalloc]]` и frame
builtins. `<coro/frame>` и `<coro/task>` поставляются этим проектом. Host tests используют
обычные C++20-корутины с выделением памяти **только для тестового кадра**.
Успех этих тестов не доказывает работу расширения компилятора.

Исходники расширения компилятора:
[rus084/llvm-project, ветка noalloc-coroutines](https://github.com/rus084/llvm-project/tree/noalloc-coroutines).

## Использование

```cpp
#include <coro/frame>
#include <coro/task>
#include <coro_os/scheduler.hpp>
#include <coro_os/ports/cortex_m.hpp>

using board_port = coro_os::ports::cortex_m::port;
using board_scheduler = coro_os::scheduler<board_port>;

[[coro::noalloc]]
coro::task<void> receiver() {
    for (;;) {
        co_await board_scheduler::wait();
        // Обработать накопленные данные драйвера.
        co_await board_scheduler::sleep_for(10);
    }
}

// После определения receiver; frame не перемещается.
static coro::frame<receiver> storage;

void application() {
    board_scheduler os(board_port{});
    auto job = os.spawn(storage());
    auto endpoint = job.notifier();
    // Сохранить endpoint в драйвере, затем включить соответствующий IRQ.
    // ISR вызывает endpoint.signal_from_isr().
    // Основной цикл вызывает os.poll(elapsed_ticks).
    // Перед выходом отключить источник IRQ и прекратить использование endpoint.
} // job снимается с очередей и уничтожает task; storage остаётся свободным
```

Полный skeleton с ISR, миганием и пробуждением второй корутины:
[`examples/noalloc_blink.cpp`](examples/noalloc_blink.cpp).
Пример основного цикла и SysTick:
[`examples/cortex_m_loop.cpp`](examples/cortex_m_loop.cpp).
Это примеры для интеграции с board startup, без настройки GPIO/NVIC/clock.
Типы порта, планировщика и endpoint объявлены один раз в
[`examples/board.hpp`](examples/board.hpp), который подключают оба cpp.

Готовые прошивки для P-NUCLEO-WB55 MB1355C с startup, GPIO, кнопкой и SysTick:
[`examples/nucleo_wb55`](examples/nucleo_wb55/README.md). Одинаковое приложение
реализовано без OS, на FreeRTOS и на coro_os; варианты проверены на плате,
а команды сборки/прошивки и сравнение Flash/RAM приведены в README примера.

## Вложенные корутины

```cpp
[[coro::noalloc]] coro::task<void> child() {
    co_await board_scheduler::sleep_for(10);
    co_await board_scheduler::wait();
}

[[coro::noalloc]] coro::task<void> parent() {
    coro::frame<child> child_frame;
    co_await child_frame();
    // child завершён и уничтожен; child_frame можно использовать повторно.
}
```

Локальный child_frame, живущий через co_await, хранится внутри кадра родителя.
Для всей цепочки создаётся один job: ready/timer/pending узел и бинарное
уведомление общие. Уведомление job потребляется текущей ожидающей корутиной.
Родитель приостановлен до завершения ребёнка, параллельного выполнения нет.
В основном [`examples/noalloc_blink.cpp`](examples/noalloc_blink.cpp) blink
хранит кадр blink_once и повторно ожидает его в цикле; время ждёт ребёнок.
Пример ожидания уведомления внутри ребёнка: [`examples/nested.cpp`](examples/nested.cpp).

`co_await` принимает owning rvalue `coro::task<T>`. Awaiter владеет ребёнком,
уничтожает его при возврате из co_await либо при отмене родителя. Незавершённый
ребёнок должен быть свежим, ещё не запущенным; присоединение уже запущенной
цепочки и ожидание пустого task вызывают trap. Завершённый task можно ожидать:
его результат извлекается и кадр уничтожается. Родитель должен иметь promise
одного из типов coro::task<T>; типы результатов родителя и ребёнка могут отличаться.

Promise хранит три указателя: root, active и link. У корня link содержит
контекст job, у ребёнка — адрес продолжения родителя. На ARM это 12 байт полей
на каждый кадр, плюс возможное изменение
выравнивания. Размер task остаётся одним handle. Корневой resume возобновляет
активный дочерний кадр. Внутренний цикл resume обрабатывает смену handle при
вызове ребёнка и возврате родителю до внешнего ожидания. Это не требует
поддержки musttail от ARM backend и не вкладывает вызовы resume друг в друга.
При отмене job сначала снимается с очередей и таймера,
затем его task уничтожает цепочку: ребёнок раньше содержащего его child_frame.

Вложенность должна иметь конечный размер, известный компилятору; рекурсивное
вложение кадров без конечной границы не поддерживается. Отдельных стеков задач
нет; фактическое использование общего машинного стека зависит от codegen,
включая вложенные деструкторы при отмене цепочки.

## Возвращаемые значения

```cpp
[[coro::noalloc]] coro::task<int> read_sensor() {
    co_await board_scheduler::sleep_for(10);
    co_return 42;
}

[[coro::noalloc]] coro::task<void> process() {
    coro::frame<read_sensor> child;
    auto value = co_await child();
    // value == 42; кадр ребёнка уже освобождён для повторного использования.
}
```

Значение конструируется прямо в promise при co_return, перемещается в результат
co_await и лишь затем уничтожается дочерний кадр. Буфер результата не требует
конструктора по умолчанию; поддерживаются типы без копирования и с повышенным
выравниванием. При отмене до co_return неинициализированное значение не
уничтожается; готовое, но не полученное значение уничтожается вместе с кадром.
Поддерживается `co_return {...}`. Frame и frame_slot принимают task с разными T,
включая смесь void и значений в одном слоте; вся цепочка по-прежнему имеет один job.

T должен быть объектным типом без const/volatile и без массива или ссылки.
Конструирование результата из co_return, перемещение T и его деструктор должны
быть noexcept. Исключения не передаются между корутинами. task<void> не хранит
буфер результата; для task<T> размер кадра дополнительно включает T, флаг его
инициализации и необходимое выравнивание. Размер самого task остаётся одним handle.

## Общий слот для последовательных вызовов

```cpp
#include <coro/frame_slot>

// Внутри родительской корутины:
coro::frame_slot<first, second> children;
co_await children.invoke<first>(args_for_first);
co_await children.invoke<second>(args_for_second);
```

Слот содержит один буфер с максимальными размером и выравниванием кадров
перечисленных функций, один флаг занятости и padding до выравнивания.
`capacity` — размер буфера в байтах, `alignment` — его выравнивание.
Выбор функции проверяется при компиляции, runtime-диспетчера нет.

Одновременно разрешён только один живой task. Завершение корутины само по себе
не освобождает слот: требуется уничтожить её task. `co_await` делает это до
возврата родителю, поэтому следующий вызов может сразу использовать тот же
буфер. Занятый invoke и уничтожение занятого слота вызывают trap. Слот нельзя
копировать или перемещать; он должен жить дольше созданного в нём task.
Пустой набор функций запрещён. Пример двух последовательных детей находится
в [`examples/nested.cpp`](examples/nested.cpp).

В измеренном Cortex-M0 примере receive возвращает received_packet, process
принимает его по значению. Отдельные кадры занимают 48 и 40 байт; общий слот —
48 байт. Полный родительский frame занимает 100 вместо 140 байт при `-Os`.

## Mutex и semaphore

```cpp
#include <coro_os/mutex.hpp>
#include <coro_os/semaphore.hpp>

coro_os::mutex<board_port> resource;
coro_os::semaphore<board_port, 8> packets{0};

// Внутри корутины:
if (co_await packets.acquire_for(100)) {
    auto guard = co_await resource.lock();
    // guard освобождает mutex при выходе из scope, включая отмену job.
}
// Без ограничения времени: co_await packets.acquire();
// Из IRQ: packets.release_from_isr();
// Из другой корутины: packets.release();
```

Оба примитива обслуживают ожидающих FIFO. Mutex нерекурсивный, без таймаута;
повторный lock из той же цепочки корутин вызывает trap. Guard можно перемещать
внутри этой цепочки и освобождать явно через `guard.unlock()`. Он сохраняет
владение через co_await. Lock и unlock выполняются только в thread mode.

`acquire_for(ticks)` возвращает true при получении разрешения и false при
таймауте. Нулевая задержка проверяет доступность без ожидания; no_deadline
запрещён, для неограниченного ожидания используйте acquire(). Время учитывается
так же, как у sleep_for. Если IRQ успел выдать разрешение до обработки
истечения таймера под critical section, побеждает разрешение.

`release()` и `release_from_isr()` возвращают false при достижении ёмкости.
В ёмкость входят сохранённые разрешения и уже назначенные ожидающим, которые
ещё не исполнили await_resume. ISR только назначает разрешение и ставит
пробуждение в pending; корутина продолжит выполнение в poll. Операция ISR — O(1).
Обычные уведомления job независимы от mutex/semaphore и не завершают их ожидание.

При отмене ожидающий удаляется из очереди и таймера. Назначенное, но ещё не
принятое разрешение передаётся следующему ожидающему либо возвращается в счётчик;
mutex аналогично передаёт владение следующему. Примитивы нельзя перемещать,
они должны жить дольше всех использующих их job и guards. Перед уничтожением
semaphore нужно отключить производящий IRQ. Guard должен оставаться в своей
цепочке job. Пример с двумя worker и IRQ: [`examples/sync.cpp`](examples/sync.cpp).

Запись ожидания находится в awaiter внутри кадра корутины. Heap не используется;
общие node и scheduler сохраняют размеры 36 и 20 байт на ARM.

## API

Типы ядра находятся в `coro_os`; task, frame и frame_slot — в `coro`.

| Вызов | Действие |
|---|---|
| `scheduler<Port>(port)` | Создать планировщик со статически выбранным портом |
| `auto job = os.spawn(std::move(task))` | Передать task во владение стабильного job; первый resume произойдёт в poll |
| `job.notifier()` | Получить копируемый non-owning endpoint |
| `endpoint.signal()` | Сохранить уведомление из thread mode |
| `endpoint.signal_from_isr()` | Сохранить уведомление из ISR или thread mode, O(1) |
| `co_await board_scheduler::wait()` | Потребить сохранённое уведомление или ждать его |
| `co_await board_scheduler::wait_for(ticks)` | Получить `wait_result::notified` или `wait_result::timeout` |
| `co_await board_scheduler::sleep_for(ticks)` | Ждать время; signal не сокращает sleep |
| `co_await board_scheduler::yield()` | Уступить управление, вставив задачу в конец FIFO |
| `os.poll(elapsed)` | Учесть elapsed и исполнять задачи до опустошения ready-очереди |
| `os.has_work()` | Проверить ready/pending/due работу; использовать только из thread mode |

Во всей программе разрешён **один активный scheduler**, включая разные
специализации Port и разные единицы трансляции. Он явно создаётся в main или
run_application; попытка создать второй вызывает trap. После уничтожения
пустого scheduler можно создать новый, что позволяет последовательно запускать
изолированные тесты.

Awaiters — статические методы выбранного `scheduler<Port>`. Они обращаются к
активному экземпляру, не хранят ссылку на него и не требуют аргумента scheduler
у корутины. Ожидание без активного scheduler соответствующего типа вызывает
trap при await_suspend. Перед уничтожением scheduler нужно уничтожить все job
и отключить их источники ISR.

Spawn связывает корневой task с узлом job через `bind_context(void*) noexcept`.
Typed await_suspend получает этот узел из `handle.promise().context()`;
вложенные корутины возвращают тот же контекст через promise корня. Поля
«текущая задача» в scheduler нет. Пользовательские coroutine task, использующие
OS awaiters, должны предоставлять эти методы; callback task без OS awaiters
может обходиться без привязки.

Task должен иметь nothrow move и `resume() noexcept` с результатом void или bool (результат игнорируется),
`bool done() const noexcept`, начальную приостановку и final suspend,
сохраняющий кадр до destroy. Job владеет task и не копируется/перемещается;
возврат из spawn использует guaranteed copy elision. Ядро не владеет raw handle.
После каждого resume проверяется done; завершённый кадр освобождается при
уничтожении job. Поддерживаются только awaiters этой ОС; обычный
`std::suspend_always` в теле задачи нарушает контракт.

`sleep_for(0)` эквивалентен yield. `wait_for(0)` сначала потребляет token,
при его отсутствии немедленно возвращает timeout.

## Статический порт

Планировщик — шаблон `scheduler<Port>`, реализация доступна в заголовках.
Отдельный scheduler.cpp больше не компилируется; CMake target `coro_os`
остаётся доступным как INTERFACE library. Таблицы port_hooks и контекста
runtime-диспетчеризации порта нет.
Порт предоставляет методы:

```cpp
std::uint32_t enter_critical() noexcept;
void exit_critical(std::uint32_t saved) noexcept;
void notify_cpu() noexcept;
bool in_isr() const noexcept;
```

Статические методы также подходят. Concept `scheduler_port<Port>` проверяет
типы результатов и noexcept. Порт Cortex-M — пустой тип `ports::cortex_m::port`;
член scheduler хранится через `[[no_unique_address]]`, вызовы порта прямые.
Обработка разных task по-прежнему использует callbacks resume/done в узле задачи.

Можно явно выбрать хранение порта по значению (`scheduler<MyPort>`) или
по ссылке (`scheduler<MyPort&>`). Для ссылочного варианта порт обязан жить
дольше scheduler. CTAD для rvalue владеет портом; для lvalue пустого порта,
копируемого и перемещаемого без исключений, также выбирает значение, для остальных lvalue —
ссылку. Например, `scheduler os(test_port)` сохраняет состояние TestPort
по ссылке, если тип содержит поля. `job<Task, Port>` и
`notification_endpoint<Port>` используют тот же параметр Port; endpoints
разных специализаций несовместимы.

Измеренный размер ARM scheduler с пустым Cortex-M портом — 36 Б.
Job занимает 56 Б, node — 52 Б. Пример с тремя noalloc-корутинами после
перехода на один scheduler при -Os без LTO/section GC: M0 2000 Б Flash,
M3 2032 Б. Статическая RAM — 108 Б; scheduler, три job и локальный endpoint
занимают ещё 208 Б в основном стеке: минимум 316 Б хранения объектов,
без дополнительных кадров вызовов/IRQ. Board startup, vector table и реальные
драйверы в измерительный образ не входят. Раскладка:
[`docs/size-report.txt`](docs/size-report.txt).

## Уведомления и ISR

Уведомление — один бинарный token на job. Два signal до потребления token
сливаются. Для UART, DMA и счётчиков измерений данные хранит сам драйвер,
а token сообщает корутине, что очередь нужно проверить. Несколько IRQ и
корутин могут использовать один endpoint.

Сигнал до wait сохраняется. Проверка token и регистрация ожидания атомарны
относительно обычных IRQ. Сигнал sleeping job сохраняется до следующего wait;
сигнал completed job игнорируется. В гонке signal/timeout уведомление побеждает,
если token сохранён до защищённого перевода задачи в ready по timeout;
более поздний token остаётся следующему ожиданию.

ISR только устанавливает token и добавляет intrusive node в pending FIFO,
без resume, поиска таймера и пользовательских деструкторов. Ready FIFO и
delta-list обслуживаются в thread mode. Изменения pending и состояния ожидания
защищены короткими critical sections. Обработка pending заканчивается на
хвосте, запомненном при входе в drain; непрерывные сигналы не делают один
drain бесконечным. Отдельный счётчик pending не нужен.

Ready и timer используют одну ссылку queue_next: узел не может состоять в обеих
очередях одновременно. Флаги in_ready/in_timer меняет только основной поток.
ISR-флаги in_pending/token/accepts_signals собраны в отдельный байт; все обращения
к нему после публикации узла выполняются под critical guard. Этот байт не делит
битовое поле с флагами ready/timer, поэтому их изменения не затирают IRQ token.
State хранится отдельным uint8_t.

Операции с таймерами и удаление ready job — O(N), enqueue/dequeue ready и
signal — O(1). Память задаётся числом объектов job/frame, общей таблицы ёмкости нет.
Латентность исполнения зависит от длительности чужого resume и нагрузки ISR.

## Время и основной цикл

`tick_t` — uint32_t. Единицу времени выбирает приложение, например 1 ms.
Задержки допустимы в диапазоне `0..UINT32_MAX-1`; `UINT32_MAX` (`no_deadline`)
обозначает отсутствие ближайшего срока и запрещён как конечная задержка.

Poll принимает **реально прошедшее время между образцами clock**. Передавать
постоянную единицу на каждый вызов неверно. Логическое время ядра обновляется
на входе poll; весь elapsed применяется до resume, поэтому новая задержка не
получает уже учтённое время. Время, проведённое внутри resume, будет учтено
при следующем poll. Следовательно, sleep отсчитывается от логического времени
этого poll: физический интервал может быть короче на время работы до регистрации
sleep и длиннее на задержку диспетчеризации. Для гарантированной минимальной
физической задержки потребуется отдельная интеграция с now/deadline clock.

`poll` возвращает 0 при готовой/pending работе, оставшуюся задержку до таймера
или no_deadline. Значение может устареть сразу после возврата из-за IRQ;
это штатно. Готовые задачи обслуживаются FIFO до опустошения очереди,
yield ставит задачу в хвост. Ограничения числа возобновлений нет: бесконечный
yield удерживает управление внутри poll. Задачи должны переходить к ожиданию
или завершаться, чтобы основной цикл мог снова считать clock.

При uint32_t clock интервал между образцами должен быть меньше полного оборота
счётчика. Clock должен продолжать идти в выбранном idle режиме. Пример использует
периодический SysTick; его ISR обновляет counter и вызывает SEV, без resume.
Tickless timer, выбор частоты SysTick и deep sleep настраиваются board-кодом.

## Cortex-M

Порт использует PRIMASK и сохраняет исходную маску при вложении critical
sections. SEV будит event register; idle выполняет очистку старого event,
проверку готовности и WFE. Новый signal в окне между проверкой и WFE сохраняет
event и предотвращает потерю пробуждения. После любого пробуждения основной
цикл должен повторно проверить clock и работу. Ready predicate обязан также
учесть изменение board clock после последнего образца, как в примере loop.

При входе в idle IRQ должны быть включены. Poll, spawn, уничтожение job и
has_work выполняются только в thread mode. NMI и HardFault не вызывают API.
Порт предназначен для одного ядра, без BASEPRI, HAL, CMSIS и libatomic.
GCC/Clang barriers сохраняют порядок обращений к общей памяти.

## Владение и отмена

Frame живёт дольше job, scheduler живёт дольше всех своих job. При выходе из
scope job сначала запрещает сигналы, удаляет pending/ready/timer ссылки,
затем деструктор task уничтожает кадр. Отмена не возобновляет тело корутины.
Деструкторы объектов, уже построенных внутри кадра, исполняются при destroy.

Endpoint не продлевает жизнь job. Перед уничтожением job отключите аппаратный
источник IRQ и прекратите использование endpoint всеми производителями.
Critical section не исправляет висячий указатель. Default endpoint нельзя
сигнализировать до назначения. Уничтожение running job и запрещённые формы
ожидания вызывают trap даже с NDEBUG. Рекурсивный poll и уничтожение scheduler
до его job запрещены контрактом; отдельных runtime-счётчиков для их проверки нет.

## Сборка и проверки

Нужны GCC/Clang с C++20, CMake >=3.20; на Windows можно использовать WSL.
Корень репозитория в этих командах — `coro_os`:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-sanitize -G Ninja -DCORO_OS_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure

bash tools/check_cortex_m.sh
```

Host tests проверяют FIFO, timers, notification races с управляемой инъекцией
ISR, отмену, запреты контрактов, отсутствие новых аллокаций ядра и взаимодействие
планировщика с отдельным тестовым idle-портом. Host idle tests не исполняют
алгоритм Cortex-M; ARM smoke проверяет сборку и инструкции аппаратного порта.
Unix death tests проверяют именно trap signal, а не любое падение.
ASan/UBSan не являются проверкой аппаратных прерываний.

Cortex-M smoke компилирует и линкует kernel/port для M0/M3 при -O0/-O2/-Os,
без allocator, libatomic, exception runtime и unresolved symbols. Проверяется
наличие PRIMASK/SEV/WFE в дизассемблировании. Fixture использует callback-backed
task без coroutine frame и не является прошивкой: vector table/startup/board
отсутствуют. M0+/M4 используют те же инструкции; отдельные board tests нужны.
`ARM_CXX`, `ARM_NM`, `ARM_OBJDUMP` позволяют выбрать инструменты.

### Интеграция с новым компилятором

```sh
NOALLOC_CXX=/path/to/noalloc-clang/bin/clang++ bash tools/check_noalloc.sh
# Или включить в CTest:
cmake -S . -B build -DNOALLOC_CXX=/path/to/noalloc-clang/bin/clang++
```

Библиотечные заголовки `<coro/task>` и `<coro/frame>` находятся в `include/coro/`
самого проекта и доступны через CMake target `coro_os`. `task<T>` содержит
promise и владеет временем жизни корутины; `frame<F>` хранит её кадр без heap.
Для freestanding-целей без `<coroutine>` заголовок task предоставляет минимальный
coroutine protocol. Компилятор предоставляет `[[coro::noalloc]]` и builtins
`__builtin_coro_frame_size`, `__builtin_coro_frame_align`, `__builtin_coro_invoke`;
библиотечные заголовки в его установке не требуются.

Проверяется task API, initial suspend, sleep/wait, уничтожение/повторное
использование frame и символы при -O0/-O2/-Os. Дополнительные include/sysroot
параметры передаются отдельными аргументами скрипта. `--cross` отключает запуск
результата: board/linker/runtime flags и memory primitives предоставляет
вызывающий код; `NOALLOC_NM` выбирает cross nm.

Если NOALLOC_CXX не задан, проверка возвращает 77 (SKIP). Если заданный compiler
отсутствует или не поддерживает контракт, возвращается FAIL, без heap/HALO fallback.
Сборка большого LLVM-проекта не запускается автоматически.

Текущее состояние: host tests (GCC 15.2 и Clang 23.1.3), настоящий noalloc ABI
на хосте при -O0/-O2/-Os и freestanding Cortex-M0 линковка noalloc-корутин
проверены. Kernel/port smoke для M0/M3 также прошёл. На P-NUCLEO-WB55
(Cortex-M4) проверены реальный запуск, LED, таймеры и пробуждение из EXTI.
Размеры и условия измерений:
[`Cortex-M0/M3`](docs/size-report.txt) и
[`P-NUCLEO-WB55`](docs/nucleo-wb55-size-report.md).
