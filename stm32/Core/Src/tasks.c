/*
 * tasks.c
 *
 * The tasks of the Lab 4 demonstration.
 *
 * Every line on the terminal is "<time in ms> <name>", where the time is the
 * value returned by get_time() at the moment the task starts.
 */
#include "tasks.h"
#include "scheduler.h"
#include "main.h"

extern UART_HandleTypeDef huart1;

uint32_t tick_task_id = NO_TASK_ID;

/* Length of a C string (no <string.h> needed). */
static uint16_t text_length(const char *text) {
  uint16_t n = 0;
  while (text[n] != '\0') n++;
  return n;
}

void tasks_print(const char *text) {
  HAL_UART_Transmit(&huart1, (uint8_t *)text, text_length(text), 100);
}

/* Builds "<time> <name>\r\n" in one buffer and sends it with one call.
 * printf() is not used: it is slow and large for this small MCU.           */
static void print_event(const char *name) {
  char     line[40];
  char     digits[10];
  uint8_t  n = 0, d = 0;
  uint32_t time = get_time();

  do {                                  /* number -> digits, last digit first */
    digits[d++] = (char)('0' + time % 10);
    time /= 10;
  } while (time > 0);
  while (d > 0) line[n++] = digits[--d];

  line[n++] = ' ';
  while (*name != '\0' && n < sizeof(line) - 2) line[n++] = *name++;
  line[n++] = '\r';
  line[n++] = '\n';

  HAL_UART_Transmit(&huart1, (uint8_t *)line, n, 100);
}

/* ---------------- periodic tasks: 0.5 s, 1 s, 1.5 s, 2 s, 2.5 s ---------- */

void task_led1(void) {
  HAL_GPIO_TogglePin(LED_1_GPIO_Port, LED_1_Pin);
  print_event("LED1");
}

void task_led2(void) {
  HAL_GPIO_TogglePin(LED_2_GPIO_Port, LED_2_Pin);
  print_event("LED2");
}

void task_led3(void) {
  HAL_GPIO_TogglePin(LED_3_GPIO_Port, LED_3_Pin);
  print_event("LED3");
}

void task_led4(void) {
  HAL_GPIO_TogglePin(LED_4_GPIO_Port, LED_4_Pin);
  print_event("LED4");
}

void task_led5(void) {
  HAL_GPIO_TogglePin(LED_5_GPIO_Port, LED_5_Pin);
  print_event("LED5");
}

/* ---------------- periodic task: every tick (10 ms) ---------------------- */

void task_tick(void) {
  print_event("TICK");
}

/* ---------------- one-shot tasks ----------------------------------------- */

void task_once_a(void) {
  print_event("ONCE-A");
}

void task_once_b(void) {
  print_event("ONCE-B");
}

void task_once_c(void) {
  print_event("ONCE-C");
}

void task_stop_tick(void) {
  if (SCH_Delete_Task(tick_task_id) == RETURN_NORMAL) {
    print_event("TICK deleted");
  } else {
    print_event("TICK not found");
  }
}

/* ---------------- scheduler error report --------------------------------- */

/* Replaces the weak function of scheduler.c. */
void SCH_Error_Callback(uint8_t error_code) {
  char name[] = "ERROR 0";
  name[6] = (char)('0' + error_code % 10);
  print_event(name);
}
