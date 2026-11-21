/*
 * tasks.h
 *
 * The tasks of the Lab 4 demonstration. Every task is a 'void (void)'
 * function that prints the current time and returns at once.
 */
#ifndef INC_TASKS_H_
#define INC_TASKS_H_

#include <stdint.h>

/* The 10 ms callback prints 100 lines per second, so it is deleted after
 * this time (ms) to keep the terminal readable. 0 = never delete it.        */
#define TICK_DEMO_MS    1000

extern uint32_t tick_task_id;   /* ID of task_tick(), used by task_stop_tick() */

void tasks_print(const char *text);   /* text to the virtual terminal */

/* Periodic tasks: toggle one LED and print "<time> LEDx" */
void task_led1(void);   /* every 0.5 s */
void task_led2(void);   /* every 1 s   */
void task_led3(void);   /* every 1.5 s */
void task_led4(void);   /* every 2 s   */
void task_led5(void);   /* every 2.5 s */

/* Periodic task, every tick (10 ms): print "<time> TICK" */
void task_tick(void);

/* One-shot tasks: print "<time> ONCE-x" when their delay expires */
void task_once_a(void);
void task_once_b(void);
void task_once_c(void);

/* One-shot task: delete task_tick() with SCH_Delete_Task() */
void task_stop_tick(void);

#endif /* INC_TASKS_H_ */
