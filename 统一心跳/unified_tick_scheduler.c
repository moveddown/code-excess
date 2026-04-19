#include <stdio.h>
#include <stdint.h>

#define DEMO_RUNTIME_MS 260U
#define DEMO_BLOCKING_BUG 0

typedef struct {
    const char *name;
    uint32_t period_ms;
    uint32_t next_release_ms;
    uint8_t ready;
    uint32_t run_count;
    uint32_t missed_releases;
} periodic_task_t;

static volatile uint32_t g_system_tick_ms = 0U;
static volatile uint8_t g_uart_rx_event = 0U;

static periodic_task_t g_control_task = {"control", 10U, 10U, 0U, 0U, 0U};
static periodic_task_t g_alarm_task = {"alarm", 20U, 20U, 0U, 0U, 0U};
static periodic_task_t g_led_task = {"led", 100U, 100U, 0U, 0U, 0U};
static periodic_task_t g_modem_task = {"modem", 20U, 20U, 0U, 0U, 0U};

typedef enum {
    MODEM_IDLE = 0,
    MODEM_SEND_HTTPINIT,
    MODEM_WAIT_HTTPINIT_OK,
    MODEM_SEND_HTTPACTION,
    MODEM_WAIT_HTTPACTION_RESULT,
    MODEM_DONE
} modem_state_t;

static modem_state_t g_modem_state = MODEM_IDLE;
static uint8_t g_upload_request = 0U;
static uint8_t g_led_on = 0U;

static uint8_t time_reached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static void release_task(periodic_task_t *task, uint32_t now) {
    while (time_reached(now, task->next_release_ms)) {
        if (task->ready != 0U) {
            task->missed_releases++;
        } else {
            task->ready = 1U;
        }

        task->next_release_ms += task->period_ms;
    }
}

static void fake_uart_rx_isr(void) {
    g_uart_rx_event = 1U;
}

static void simulate_external_events(void) {
    if (g_system_tick_ms == 60U) {
        g_upload_request = 1U;
        printf("[%03lu ms] event  : upload request created\n", (unsigned long)g_system_tick_ms);
    }

    if (g_system_tick_ms == 100U) {
        fake_uart_rx_isr();
    }

    if (g_system_tick_ms == 160U) {
        fake_uart_rx_isr();
    }
}

static void systick_isr_1ms(void) {
    g_system_tick_ms++;

    simulate_external_events();

    release_task(&g_control_task, g_system_tick_ms);
    release_task(&g_alarm_task, g_system_tick_ms);
    release_task(&g_led_task, g_system_tick_ms);
    release_task(&g_modem_task, g_system_tick_ms);
}

static void control_task_step(void) {
    g_control_task.ready = 0U;
    g_control_task.run_count++;

    printf("[%03lu ms] task   : control step, fixed-period work executed\n",
           (unsigned long)g_system_tick_ms);
}

static void alarm_task_step(void) {
    g_alarm_task.ready = 0U;
    g_alarm_task.run_count++;

    printf("[%03lu ms] task   : alarm scan executed\n",
           (unsigned long)g_system_tick_ms);
}

static void led_task_step(void) {
    g_led_task.ready = 0U;
    g_led_task.run_count++;
    g_led_on = (g_led_on == 0U) ? 1U : 0U;

    printf("[%03lu ms] task   : led toggled -> %s\n",
           (unsigned long)g_system_tick_ms,
           (g_led_on != 0U) ? "ON" : "OFF");
}

static void modem_task_step(void) {
    g_modem_task.ready = 0U;
    g_modem_task.run_count++;

    switch (g_modem_state) {
    case MODEM_IDLE:
        if (g_upload_request != 0U) {
            printf("[%03lu ms] modem  : send AT+HTTPINIT\n",
                   (unsigned long)g_system_tick_ms);
            g_modem_state = MODEM_WAIT_HTTPINIT_OK;
        }
        break;

    case MODEM_WAIT_HTTPINIT_OK:
        if (g_uart_rx_event != 0U) {
            g_uart_rx_event = 0U;
            printf("[%03lu ms] modem  : rx OK, send HTTPACTION\n",
                   (unsigned long)g_system_tick_ms);
            g_modem_state = MODEM_WAIT_HTTPACTION_RESULT;
        }
        break;

    case MODEM_WAIT_HTTPACTION_RESULT:
        if (g_uart_rx_event != 0U) {
            g_uart_rx_event = 0U;
            printf("[%03lu ms] modem  : rx +HTTPACTION result, upload done\n",
                   (unsigned long)g_system_tick_ms);
            g_modem_state = MODEM_DONE;
            g_modem_task.ready = 1U;
        }
        break;

    case MODEM_DONE:
        printf("[%03lu ms] modem  : cleanup and return to idle\n",
               (unsigned long)g_system_tick_ms);
        g_upload_request = 0U;
        g_modem_state = MODEM_IDLE;
        break;

    case MODEM_SEND_HTTPINIT:
    case MODEM_SEND_HTTPACTION:
    default:
        g_modem_state = MODEM_IDLE;
        break;
    }
}

static void main_loop_iteration(void) {
    if (g_control_task.ready != 0U) {
        control_task_step();
    }

    if (g_alarm_task.ready != 0U) {
        alarm_task_step();
    }

    if (g_led_task.ready != 0U) {
        led_task_step();
    }

    if ((g_modem_task.ready != 0U) || (g_uart_rx_event != 0U)) {
        modem_task_step();
    }
}

int main(void) {
    printf("=== unified tick demo ===\n");
    printf("1ms heartbeat releases 10ms/20ms/100ms tasks and cooperates with UART events.\n");

    for (uint32_t i = 0U; i < DEMO_RUNTIME_MS; ++i) {
        systick_isr_1ms();

#if DEMO_BLOCKING_BUG
        if ((g_system_tick_ms >= 120U) && (g_system_tick_ms < 170U)) {
            continue;
        }
#endif

        main_loop_iteration();
    }

    printf("\n=== summary ===\n");
    printf("%s runs=%lu missed=%lu\n",
           g_control_task.name,
           (unsigned long)g_control_task.run_count,
           (unsigned long)g_control_task.missed_releases);
    printf("%s runs=%lu missed=%lu\n",
           g_alarm_task.name,
           (unsigned long)g_alarm_task.run_count,
           (unsigned long)g_alarm_task.missed_releases);
    printf("%s runs=%lu missed=%lu\n",
           g_led_task.name,
           (unsigned long)g_led_task.run_count,
           (unsigned long)g_led_task.missed_releases);
    printf("%s runs=%lu missed=%lu\n",
           g_modem_task.name,
           (unsigned long)g_modem_task.run_count,
           (unsigned long)g_modem_task.missed_releases);

    return 0;
}
