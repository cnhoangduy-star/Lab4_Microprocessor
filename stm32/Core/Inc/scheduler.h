/*
 * scheduler.h
 *
 * A cooperative scheduler with an O(1) SCH_Update().
 *
 * The waiting tasks are kept in a list sorted by the time they are due.
 * Each task stores only the number of ticks AFTER the task in front of it
 * (a "delta list"), so the timer interrupt has to touch the first task only.
 */
#ifndef INC_SCHEDULER_H_
#define INC_SCHEDULER_H_

#include <stdint.h>

/* MUST BE ADJUSTED FOR EACH NEW PROJECT */
#define SCH_MAX_TASKS     40    /* the largest number of tasks at one time     */
#define SCH_TICK_MS       10    /* MUST equal the TIM2 interrupt period (ms)   */

/* SCH_Add_Task() and SCH_Delete_Task() work in TICKS, like in the manual.
 * SCH_MS() converts milliseconds to ticks: SCH_MS(500) = 50 ticks.          */
#define SCH_MS(ms)        ((uint32_t)(ms) / SCH_TICK_MS)

#define NO_TASK_ID        0     /* never returned for a task that was added    */

/* Return values of SCH_Delete_Task() */
#define RETURN_NORMAL     0
#define RETURN_ERROR      1

/* Values of Error_code_G */
#define ERROR_SCH_TOO_MANY_TASKS        1
#define ERROR_SCH_CANNOT_DELETE_TASK    2

/* Set to 1 to stop the CPU between two interrupts (optional part 2.3.8) */
#define SCH_SLEEP_ENABLED 0

extern uint8_t Error_code_G;

void     SCH_Init(void);
void     SCH_Update(void);              /* call it from the timer ISR, every tick */
void     SCH_Dispatch_Tasks(void);      /* call it from the super loop            */
uint32_t SCH_Add_Task(void (*pFunction)(void), uint32_t DELAY, uint32_t PERIOD);
uint8_t  SCH_Delete_Task(uint32_t taskID);

uint32_t get_time(void);                /* milliseconds since SCH_Init()          */

void     SCH_Report_Status(void);
void     SCH_Go_To_Sleep(void);
void     SCH_Error_Callback(uint8_t error_code);   /* weak, see scheduler.c       */

#endif /* INC_SCHEDULER_H_ */
