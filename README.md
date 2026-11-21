# Lab4_Microprocessor

CO3009 Microprocessors - Microcontrollers, Lab 4: A cooperative scheduler (STM32F103C6).

- `stm32/` - STM32CubeIDE project (`Lab4.ioc`); the compiled `Debug/Lab4.hex` is kept for Proteus
- `proteus/` - Proteus schematic `Lab4.pdsprj`
- `report/` - Lab 4 report (PDF and LaTeX source)
- `test/` - unit tests of the scheduler that run on a PC

## Scheduler

`stm32/Core/Src/scheduler.c` keeps the waiting tasks in a list sorted by the time they are due. Each task stores only the ticks after the task in front of it, so `SCH_Update()` (called from the TIM2 interrupt every 10 ms) decrements one counter and has no loop: it is O(1).

| Function | Called from | Cost |
| --- | --- | --- |
| `void SCH_Update(void)` | TIM2 interrupt, every 10 ms | O(1) |
| `void SCH_Dispatch_Tasks(void)` | super loop | runs the tasks that are due |
| `uint32_t SCH_Add_Task(void (*pFunction)(void), uint32_t DELAY, uint32_t PERIOD)` | super loop / tasks | O(n) sorted insert, returns the task ID |
| `uint8_t SCH_Delete_Task(uint32_t taskID)` | super loop / tasks | O(n) search by ID |
| `uint32_t get_time(void)` | anywhere | milliseconds since `SCH_Init()` |

`DELAY` and `PERIOD` are in ticks; `SCH_MS(500)` converts milliseconds to ticks.

## Demonstration

| Task | First run | Period | Action |
| --- | --- | --- | --- |
| `task_led1` ... `task_led5` | 0 | 0.5 s, 1 s, 1.5 s, 2 s, 2.5 s | toggle `LED_1` ... `LED_5` (PA11 ... PA15), print the time |
| `task_tick` | 0 | 10 ms | print the time; deleted after 1 s |
| `task_once_a`, `_b`, `_c` | 250 ms, 750 ms, 1250 ms | one-shot | print the time |
| `task_stop_tick` | 1000 ms | one-shot | `SCH_Delete_Task()` on `task_tick` |

Every task prints one line `<time in ms> <name>` on USART1 (PA9, 57600 baud, 8N1), shown by the Virtual Terminal in Proteus.

## Unit tests

```
cd test
gcc -Wall -Wextra -DSCH_HOST_TEST -I../stm32/Core/Inc test_scheduler.c ../stm32/Core/Src/scheduler.c -o test_scheduler
./test_scheduler
```
