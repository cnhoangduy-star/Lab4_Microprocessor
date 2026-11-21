/*
 * scheduler.c
 *
 * Cooperative scheduler, O(1) update.
 *
 * Data structure
 * --------------
 * All tasks live in the static array SCH_tasks_G[]. Two singly linked lists
 * run through this array (the field Next holds an array index):
 *
 *   - the waiting list (sch_head): tasks sorted by the time they are due.
 *     Delay is NOT the time until the task runs, it is the number of ticks
 *     after the task in front of it. Example: A in 50, B in 80, C in 80 and
 *     D in 200 ticks are stored as
 *
 *         sch_head -> A(50) -> B(30) -> C(0) -> D(120)
 *
 *     Counting one tick down for every task is therefore one single
 *     decrement on the first task: this is the whole SCH_Update().
 *
 *   - the free list (sch_free): the empty slots, so that SCH_Add_Task()
 *     finds a slot without searching.
 *
 * Who does what
 * -------------
 *   SCH_Update()          timer ISR    O(1)   counts the tick
 *   SCH_Dispatch_Tasks()  super loop          runs the tasks that are due
 *   SCH_Add_Task()        super loop   O(n)   sorted insert
 *   SCH_Delete_Task()     super loop   O(n)   search by ID
 */
#include "scheduler.h"

/* ------------------------------------------------------------------------
 * Port section: the only hardware dependent lines of the scheduler.
 * The lists are changed in the super loop and read in the timer ISR, so the
 * interrupt must not arrive in the middle of a change.
 * ------------------------------------------------------------------------ */
#ifndef SCH_HOST_TEST
#include "main.h"
#define SCH_ENTER_CRITICAL()   uint32_t sch_primask = __get_PRIMASK(); __disable_irq()
#define SCH_EXIT_CRITICAL()    __set_PRIMASK(sch_primask)
#define SCH_CPU_SLEEP()        __WFI()
#else   /* built on a PC for the unit tests in /test */
#define SCH_ENTER_CRITICAL()
#define SCH_EXIT_CRITICAL()
#define SCH_CPU_SLEEP()
#endif

#define SCH_NO_TASK   0xFF    /* "no index": end of a list */

typedef struct {
  void (*pTask)(void);    /* pointer to the task (must be a 'void (void)' function)  */
  uint32_t Delay;         /* ticks after the PREVIOUS task of the waiting list       */
  uint32_t Period;        /* ticks between two runs, 0 = one-shot task               */
  uint32_t TaskID;        /* ID returned by SCH_Add_Task()                           */
  uint8_t  Next;          /* index of the next task in the list, or SCH_NO_TASK      */
} sTask;

static sTask   SCH_tasks_G[SCH_MAX_TASKS];
static uint8_t sch_head;          /* first task of the waiting list (the next one due) */
static uint8_t sch_free;          /* first empty slot                                  */
static uint32_t sch_next_id;      /* ID given to the next task that is added           */

static volatile uint32_t sch_ticks;       /* ticks since SCH_Init(), for get_time()     */
static volatile uint32_t sch_late_ticks;  /* ticks that arrived while the first task was
                                             already due and still waiting to be run   */
uint8_t Error_code_G = 0;
static uint8_t Last_error_code_G = 0;

/* ------------------------------------------------------------------------ */
/* Private functions: call them inside a critical section only              */
/* ------------------------------------------------------------------------ */

/* Put task 'index' into the waiting list so that it is due 'delay' ticks
 * after the time the list is counting from.
 * Tasks due at the same tick keep the order in which they were added
 * (smaller TaskID first), like the array scan of the scheduler in the manual. */
static void SCH_Insert(uint8_t index, uint32_t delay) {
  uint8_t previous = SCH_NO_TASK;
  uint8_t current  = sch_head;

  while (current != SCH_NO_TASK &&
         (SCH_tasks_G[current].Delay < delay ||
          (SCH_tasks_G[current].Delay == delay &&
           SCH_tasks_G[current].TaskID < SCH_tasks_G[index].TaskID))) {
    delay   -= SCH_tasks_G[current].Delay;    /* what is left after this task */
    previous = current;
    current  = SCH_tasks_G[current].Next;
  }

  SCH_tasks_G[index].Delay = delay;
  SCH_tasks_G[index].Next  = current;
  if (current != SCH_NO_TASK) {
    SCH_tasks_G[current].Delay -= delay;      /* the next task now waits behind the new one */
  }
  if (previous == SCH_NO_TASK) {
    sch_head = index;
  } else {
    SCH_tasks_G[previous].Next = index;
  }
}

/* Give an empty slot back to the free list. */
static void SCH_Free_Slot(uint8_t index) {
  SCH_tasks_G[index].pTask  = 0;
  SCH_tasks_G[index].Delay  = 0;
  SCH_tasks_G[index].Period = 0;
  SCH_tasks_G[index].TaskID = NO_TASK_ID;
  SCH_tasks_G[index].Next   = sch_free;
  sch_free = index;
}

/* While the first task is due but not run yet, SCH_Update() cannot count the
 * ticks on it, so it saves them in sch_late_ticks. As soon as the first task
 * of the list is a task that is still waiting, pay these ticks back to it.
 * Without this, every long task would delay all the others for ever.        */
static void SCH_Settle_Late_Ticks(void) {
  if (sch_head == SCH_NO_TASK) {
    sch_late_ticks = 0;                       /* nobody is waiting: nothing to pay back */
  } else if (sch_late_ticks > 0 && SCH_tasks_G[sch_head].Delay > 0) {
    uint32_t n = sch_late_ticks;
    if (n > SCH_tasks_G[sch_head].Delay) {
      n = SCH_tasks_G[sch_head].Delay;
    }
    SCH_tasks_G[sch_head].Delay -= n;
    sch_late_ticks              -= n;
  }
}

/* ------------------------------------------------------------------------ */
/* Public functions                                                         */
/* ------------------------------------------------------------------------ */

void SCH_Init(void) {
  uint8_t i;
  SCH_ENTER_CRITICAL();
  sch_head = SCH_NO_TASK;
  sch_free = SCH_NO_TASK;
  for (i = SCH_MAX_TASKS; i > 0; i--) {       /* slot 0 ends up first in the free list */
    SCH_Free_Slot(i - 1);
  }
  sch_next_id       = 1;
  sch_ticks         = 0;
  sch_late_ticks    = 0;
  Error_code_G      = 0;
  Last_error_code_G = 0;
  SCH_EXIT_CRITICAL();
}

/* Called from the timer ISR every SCH_TICK_MS. No loop, no search: O(1). */
void SCH_Update(void) {
  sch_ticks++;
  if (sch_head == SCH_NO_TASK) {
    return;                                   /* no task at all */
  }
  if (SCH_tasks_G[sch_head].Delay > 0) {
    SCH_tasks_G[sch_head].Delay--;            /* 0 means: due, the dispatcher will run it */
  } else {
    sch_late_ticks++;                         /* still not run: do not lose this tick */
  }
}

/* DELAY  : ticks before the first run, 0 = as soon as possible
 * PERIOD : ticks between two runs, 0 = run only once
 * Returns the ID of the task, or NO_TASK_ID when the task cannot be added.  */
uint32_t SCH_Add_Task(void (*pFunction)(void), uint32_t DELAY, uint32_t PERIOD) {
  uint8_t  index;
  uint32_t id;

  if (pFunction == 0) {
    return NO_TASK_ID;
  }

  SCH_ENTER_CRITICAL();
  index = sch_free;
  if (index == SCH_NO_TASK) {                 /* task array is full */
    Error_code_G = ERROR_SCH_TOO_MANY_TASKS;
    SCH_EXIT_CRITICAL();
    return NO_TASK_ID;
  }
  sch_free = SCH_tasks_G[index].Next;

  id = sch_next_id++;
  if (sch_next_id == NO_TASK_ID) {
    sch_next_id = 1;                          /* 0 is reserved */
  }
  SCH_tasks_G[index].pTask  = pFunction;
  SCH_tasks_G[index].Period = PERIOD;
  SCH_tasks_G[index].TaskID = id;
  /* The list counts from the moment its first task was due, which is
   * sch_late_ticks ticks ago: add them so that DELAY starts from now.       */
  SCH_Insert(index, DELAY + sch_late_ticks);
  SCH_EXIT_CRITICAL();

  return id;
}

uint8_t SCH_Delete_Task(uint32_t taskID) {
  uint8_t previous = SCH_NO_TASK;
  uint8_t current;
  uint8_t next;

  SCH_ENTER_CRITICAL();
  current = sch_head;
  while (current != SCH_NO_TASK && SCH_tasks_G[current].TaskID != taskID) {
    previous = current;
    current  = SCH_tasks_G[current].Next;
  }
  if (current == SCH_NO_TASK || taskID == NO_TASK_ID) {   /* no task with this ID */
    Error_code_G = ERROR_SCH_CANNOT_DELETE_TASK;
    SCH_EXIT_CRITICAL();
    return RETURN_ERROR;
  }

  next = SCH_tasks_G[current].Next;
  if (next != SCH_NO_TASK) {
    SCH_tasks_G[next].Delay += SCH_tasks_G[current].Delay;  /* next task keeps its time */
  }
  if (previous == SCH_NO_TASK) {
    sch_head = next;
  } else {
    SCH_tasks_G[previous].Next = next;
  }
  SCH_Free_Slot(current);
  SCH_Settle_Late_Ticks();
  SCH_EXIT_CRITICAL();

  return RETURN_NORMAL;
}

/* Runs every task that is due, earliest first. Call it in the super loop. */
void SCH_Dispatch_Tasks(void) {
  void (*pTask)(void);
  uint8_t index;

  for (;;) {
    pTask = 0;

    SCH_ENTER_CRITICAL();
    if (sch_head != SCH_NO_TASK && SCH_tasks_G[sch_head].Delay == 0) {
      index    = sch_head;                    /* take the due task out of the list */
      sch_head = SCH_tasks_G[index].Next;
      pTask    = SCH_tasks_G[index].pTask;
      if (SCH_tasks_G[index].Period > 0) {
        /* Periodic task: schedule the next run BEFORE running it, one Period
         * after the time it was due (not after the time it finishes).        */
        SCH_Insert(index, SCH_tasks_G[index].Period);
      } else {
        SCH_Free_Slot(index);                 /* one-shot task: remove it */
      }
      SCH_Settle_Late_Ticks();
    }
    SCH_EXIT_CRITICAL();

    if (pTask == 0) {
      break;                                  /* nothing (more) to run */
    }
    (*pTask)();                               /* run the task, interrupts enabled */
  }

  SCH_Report_Status();
  SCH_Go_To_Sleep();
}

/* Milliseconds since SCH_Init(). The resolution is one tick (SCH_TICK_MS). */
uint32_t get_time(void) {
  return sch_ticks * SCH_TICK_MS;
}

/* Reports a new error code once. */
void SCH_Report_Status(void) {
  if (Error_code_G != Last_error_code_G) {
    Last_error_code_G = Error_code_G;
    if (Error_code_G != 0) {
      SCH_Error_Callback(Error_code_G);
    }
  }
}

/* Default: do nothing. Write a function with the same name in the
 * application to show the error (same idea as the HAL callbacks).           */
__attribute__((weak)) void SCH_Error_Callback(uint8_t error_code) {
  (void)error_code;
}

/* The CPU stops until the next interrupt (at the latest the next tick). */
void SCH_Go_To_Sleep(void) {
#if SCH_SLEEP_ENABLED
  SCH_CPU_SLEEP();
#endif
}
