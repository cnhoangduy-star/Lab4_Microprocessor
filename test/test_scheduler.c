/*
 * test_scheduler.c - unit tests of Core/Src/scheduler.c, run on a PC.
 *
 *   gcc -Wall -Wextra -DSCH_HOST_TEST -I../stm32/Core/Inc \
 *       test_scheduler.c ../stm32/Core/Src/scheduler.c -o test_scheduler && ./test_scheduler
 *
 * Part 1: fixed scenarios (order, accuracy, one-shot, delete, full array).
 * Part 2: random test. A second, very simple model of the scheduler (an
 *         array with the absolute time of every task, searched completely
 *         every time) runs next to the real one. Every task that the real
 *         scheduler starts must be exactly the task the model expects, at
 *         exactly the tick the model expects - also when tasks run for
 *         several ticks, add new tasks or delete other tasks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { failures++; \
  printf("  FAILED line %d: %s\n", __LINE__, #cond); } } while (0)

/* ---------------------------------------------------------------- part 1 */

#define LOG_MAX 4096
static struct { char name; uint32_t time; } event_log[LOG_MAX];
static int log_n;
static void log_event(char name) {
  if (log_n < LOG_MAX) { event_log[log_n].name = name; event_log[log_n].time = get_time(); log_n++; }
}
static void tA(void) { log_event('A'); }
static void tB(void) { log_event('B'); }
static void tC(void) { log_event('C'); }
static void tD(void) { log_event('D'); }
static void tE(void) { log_event('E'); }

static int busy_ticks;                       /* a task that runs for several ticks */
static void tLong(void) { log_event('L'); for (int i = 0; i < busy_ticks; i++) SCH_Update(); }

/* one tick of the timer, then the super loop */
static void run_ticks(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) { SCH_Update(); SCH_Dispatch_Tasks(); }
}

static void test_lab_periods(void) {
  printf("test: five periodic tasks 0.5 s .. 2.5 s for 60 s\n");
  const uint32_t period[5] = {500, 1000, 1500, 2000, 2500};
  uint32_t count[5] = {0};
  SCH_Init(); log_n = 0;
  SCH_Add_Task(tA, 0, SCH_MS(500));
  SCH_Add_Task(tB, 0, SCH_MS(1000));
  SCH_Add_Task(tC, 0, SCH_MS(1500));
  SCH_Add_Task(tD, 0, SCH_MS(2000));
  SCH_Add_Task(tE, 0, SCH_MS(2500));
  SCH_Dispatch_Tasks();
  run_ticks(6000);
  for (int i = 0; i < log_n; i++) {
    int k = event_log[i].name - 'A';
    CHECK(event_log[i].time == count[k] * period[k]);       /* exact, no drift */
    count[k]++;
    if (i > 0) {
      CHECK(event_log[i].time >= event_log[i-1].time);      /* in time order */
      if (event_log[i].time == event_log[i-1].time)
        CHECK(event_log[i].name > event_log[i-1].name);     /* same tick: order of adding */
    }
  }
  CHECK(count[0] == 121 && count[1] == 61 && count[2] == 41 && count[3] == 31 && count[4] == 25);
  CHECK(Error_code_G == 0);
}

static void test_one_shot_order(void) {
  printf("test: one-shot tasks added in the wrong order\n");
  SCH_Init(); log_n = 0;
  SCH_Add_Task(tC, 125, 0);
  SCH_Add_Task(tA, 25, 0);
  SCH_Add_Task(tB, 75, 0);
  run_ticks(200);
  CHECK(log_n == 3);
  CHECK(event_log[0].name == 'A' && event_log[0].time == 250);
  CHECK(event_log[1].name == 'B' && event_log[1].time == 750);
  CHECK(event_log[2].name == 'C' && event_log[2].time == 1250);
  CHECK(SCH_Add_Task(0, 1, 1) == NO_TASK_ID);                /* no function */
}

static void test_delete(void) {
  printf("test: SCH_Delete_Task\n");
  SCH_Init(); log_n = 0;
  uint32_t a = SCH_Add_Task(tA, 10, 10);
  uint32_t b = SCH_Add_Task(tB, 15, 10);
  uint32_t c = SCH_Add_Task(tC, 30, 0);
  CHECK(a != NO_TASK_ID && b != NO_TASK_ID && c != NO_TASK_ID && a != b && b != c);
  run_ticks(12);                                             /* A ran at tick 10 */
  CHECK(SCH_Delete_Task(a) == RETURN_NORMAL);
  CHECK(SCH_Delete_Task(a) == RETURN_ERROR);                 /* already deleted */
  CHECK(Error_code_G == ERROR_SCH_CANNOT_DELETE_TASK);
  CHECK(SCH_Delete_Task(NO_TASK_ID) == RETURN_ERROR);
  CHECK(SCH_Delete_Task(12345) == RETURN_ERROR);
  run_ticks(28);                                             /* until tick 40 */
  /* expected: A@100, B@150, B@250, C@300, B@350 : deleting A did not move B or C */
  CHECK(log_n == 5);
  CHECK(event_log[0].name == 'A' && event_log[0].time == 100);
  CHECK(event_log[1].name == 'B' && event_log[1].time == 150);
  CHECK(event_log[2].name == 'B' && event_log[2].time == 250);
  CHECK(event_log[3].name == 'C' && event_log[3].time == 300);
  CHECK(event_log[4].name == 'B' && event_log[4].time == 350);
  CHECK(SCH_Delete_Task(c) == RETURN_ERROR);                 /* a one-shot is gone after it ran */
  CHECK(SCH_Delete_Task(b) == RETURN_NORMAL);
  log_n = 0; run_ticks(100); CHECK(log_n == 0);              /* nothing left */
}

static void test_full_array(void) {
  printf("test: more than SCH_MAX_TASKS tasks\n");
  uint32_t id[SCH_MAX_TASKS];
  SCH_Init();
  for (int i = 0; i < SCH_MAX_TASKS; i++) {
    id[i] = SCH_Add_Task(tA, 100 + i, 0);
    CHECK(id[i] != NO_TASK_ID);
  }
  CHECK(Error_code_G == 0);
  CHECK(SCH_Add_Task(tB, 5, 0) == NO_TASK_ID);
  CHECK(Error_code_G == ERROR_SCH_TOO_MANY_TASKS);
  CHECK(SCH_Delete_Task(id[7]) == RETURN_NORMAL);            /* a slot is free again */
  CHECK(SCH_Add_Task(tB, 5, 0) != NO_TASK_ID);
}

static void test_long_task(void) {
  printf("test: a task that runs for 35 ms does not shift the other tasks\n");
  SCH_Init(); log_n = 0; busy_ticks = 3;                     /* 3 ticks pass inside tLong */
  SCH_Add_Task(tLong, 0, 10);                                /* every 100 ms, takes > 30 ms */
  SCH_Add_Task(tA, 1, 5);                                    /* every 50 ms, from 10 ms */
  SCH_Dispatch_Tasks();
  while (get_time() < 10000) run_ticks(1);                   /* ticks also pass inside tLong */
  uint32_t nL = 0, nA = 0;
  for (int i = 0; i < log_n; i++) {
    if (event_log[i].name == 'L') { CHECK(event_log[i].time == nL * 100); nL++; }
    if (event_log[i].name == 'A') {
      uint32_t due = 10 + nA * 50;                           /* the time it was planned for */
      /* late by at most the 3 ticks of tLong, and never earlier than planned */
      CHECK(event_log[i].time >= due && event_log[i].time <= due + 30);
      nA++;
    }
  }
  CHECK(nL == get_time() / 100 + 1);                         /* exactly every 100 ms */
  CHECK(nA == (get_time() - 10) / 50 + 1);                   /* no run of A was lost */
}

/* ---------------------------------------------------------------- part 2 */

#define NFUNC 48                                 /* one function per living task */
static struct {
  int      alive;
  uint32_t id, due, period;                      /* due: absolute tick */
} model[NFUNC];
static uint32_t now;                             /* ticks, the model's clock */
static unsigned long started, added, deleted, rejected;
static uint32_t rng_state = 12345;
static uint32_t rnd(uint32_t n) {                /* small LCG, same on every PC */
  rng_state = rng_state * 1664525u + 1013904223u;
  return (rng_state >> 8) % n;
}
static void tick(void) { SCH_Update(); now++; CHECK(get_time() == now * SCH_TICK_MS); }

static void (*const func_table[NFUNC])(void);
static int model_count(void) { int n = 0; for (int i = 0; i < NFUNC; i++) n += model[i].alive; return n; }

static void random_add(void) {
  int f = -1;
  for (int i = 0; i < NFUNC; i++) if (!model[i].alive) { f = i; break; }
  if (f < 0) return;
  uint32_t delay  = rnd(4) == 0 ? 0 : rnd(60);
  uint32_t period = rnd(3) == 0 ? 0 : 1 + rnd(40);
  uint32_t id = SCH_Add_Task(func_table[f], delay, period);
  if (model_count() >= SCH_MAX_TASKS) {          /* array full: must be refused */
    CHECK(id == NO_TASK_ID); rejected++;
    return;
  }
  CHECK(id != NO_TASK_ID);
  for (int i = 0; i < NFUNC; i++) CHECK(!model[i].alive || model[i].id != id);   /* unique */
  model[f].alive = 1; model[f].id = id; model[f].due = now + delay; model[f].period = period;
  added++;
}

static void random_delete(void) {
  int k = (int)rnd(NFUNC);
  if (model[k].alive) {
    CHECK(SCH_Delete_Task(model[k].id) == RETURN_NORMAL);
    model[k].alive = 0; deleted++;
  } else {
    CHECK(SCH_Delete_Task(model[k].id) == RETURN_ERROR);     /* old ID */
  }
}

/* every task function of the random test ends here */
static void task_body(int f) {
  /* the model: among the tasks that are due, the earliest one, then the smallest ID */
  int expect = -1;
  for (int i = 0; i < NFUNC; i++) {
    if (!model[i].alive || model[i].due > now) continue;
    if (expect < 0 || model[i].due < model[expect].due ||
        (model[i].due == model[expect].due && model[i].id < model[expect].id)) expect = i;
  }
  CHECK(expect == f);
  CHECK(model[f].alive);
  started++;
  if (model[f].period > 0) model[f].due += model[f].period; else model[f].alive = 0;

  /* what the task does: use some time, add tasks, delete tasks */
  /* (the time used stays far below the period of the tasks on average:
   *  a cooperative scheduler cannot catch up when the CPU is overloaded) */
  uint32_t r = rnd(1000);
  if (r < 20)      { uint32_t n = 1 + rnd(3);  for (uint32_t i = 0; i < n; i++) tick(); }
  else if (r < 21) { uint32_t n = 5 + rnd(30); for (uint32_t i = 0; i < n; i++) tick(); }
  if (rnd(100) < 12) random_add();
  if (rnd(100) < 10) random_delete();
}

#define F(n) static void f##n(void) { task_body(n); }
F(0) F(1) F(2) F(3) F(4) F(5) F(6) F(7) F(8) F(9) F(10) F(11) F(12) F(13) F(14) F(15)
F(16) F(17) F(18) F(19) F(20) F(21) F(22) F(23) F(24) F(25) F(26) F(27) F(28) F(29) F(30) F(31)
F(32) F(33) F(34) F(35) F(36) F(37) F(38) F(39) F(40) F(41) F(42) F(43) F(44) F(45) F(46) F(47)
static void (*const func_table[NFUNC])(void) = {
  f0,f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11,f12,f13,f14,f15,f16,f17,f18,f19,f20,f21,f22,f23,
  f24,f25,f26,f27,f28,f29,f30,f31,f32,f33,f34,f35,f36,f37,f38,f39,f40,f41,f42,f43,f44,f45,f46,f47 };

static void test_random(uint32_t seed, uint32_t ticks) {
  rng_state = seed; now = 0; memset(model, 0, sizeof model);
  SCH_Init();
  for (int i = 0; i < 6; i++) random_add();
  for (uint32_t t = 0; t < ticks && failures == 0; t++) {
    SCH_Dispatch_Tasks();
    for (int i = 0; i < NFUNC; i++) CHECK(!model[i].alive || model[i].due > now);  /* nothing forgotten */
    if (rnd(100) < 8) random_add();
    if (rnd(100) < 4) random_delete();
    tick();
  }
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  test_lab_periods();
  test_one_shot_order();
  test_delete();
  test_full_array();
  test_long_task();
  printf("test: random, compared with the reference model\n");
  for (uint32_t seed = 1; seed <= 200 && failures == 0; seed++) test_random(seed, 20000);
  printf("  %lu task runs checked, %lu tasks added, %lu deleted, %lu refused (array full)\n",
         started, added, deleted, rejected);
  printf(failures ? "\nFAILED: %d check(s)\n" : "\nALL TESTS PASSED\n", failures);
  return failures != 0;
}
