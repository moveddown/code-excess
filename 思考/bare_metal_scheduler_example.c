#include <stdio.h>
#include <stdint.h>

#define DEMO_RUNTIME_MS 640U
#define DEMO_BLOCKING_BUG 0

typedef struct {
    uint32_t period_ms;
    uint32_t next_release_ms;
    uint8_t ready;
    uint32_t missed_releases;
} periodic_task_t;

typedef struct {
    float kp;
    float ki;
    float kd;
    float dt;
    float integral;
    float prev_error;
    float integral_min;
    float integral_max;
    float out_min;
    float out_max;
} pid_controller_t;

typedef struct {
    float state;
    float gain;
    float time_constant;
    float disturbance;
} plant_t;

typedef enum {
    MODEM_IDLE = 0,
    MODEM_SEND_HTTP_INIT,
    MODEM_WAIT_HTTP_INIT_OK,
    MODEM_SEND_HTTP_URL,
    MODEM_WAIT_HTTP_URL_OK,
    MODEM_SEND_HTTP_ACTION,
    MODEM_WAIT_HTTP_ACTION_RESULT,
    MODEM_DONE,
    MODEM_RETRY_DELAY
} modem_state_t;

typedef struct {
    modem_state_t state;
    uint8_t upload_request;
    uint8_t response_ready;
    uint8_t scheduled_response_pending;
    uint32_t wait_deadline_ms;
    uint32_t retry_deadline_ms;
    uint32_t scheduled_response_at_ms;
    const char *response_text;
    const char *scheduled_response_text;
} modem_fsm_t;

typedef struct {
    uint8_t warning_active;
    uint8_t fault_latched;
    uint8_t report_pending;
    uint8_t report_sent_for_current_condition;
    uint8_t led_on;
    uint32_t next_led_toggle_ms;
} alarm_manager_t;

static uint32_t g_tick_ms = 0U;
static periodic_task_t g_pid_task = {10U, 10U, 0U, 0U};
static periodic_task_t g_alarm_task = {20U, 20U, 0U, 0U};
static periodic_task_t g_modem_task = {20U, 20U, 0U, 0U};

static pid_controller_t g_pid;
static plant_t g_plant;
static modem_fsm_t g_modem;
static alarm_manager_t g_alarm;

static float g_setpoint = 35.0f;
static float g_feedback = 0.0f;
static float g_output = 0.0f;
static uint8_t g_uart_event = 0U;

static uint8_t time_reached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static float clampf(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

static void release_periodic_task(periodic_task_t *task, uint32_t now) {
    while (time_reached(now, task->next_release_ms)) {
        if (task->ready != 0U) {
            task->missed_releases++;
        } else {
            task->ready = 1U;
        }

        task->next_release_ms += task->period_ms;
    }
}

static void pid_init(pid_controller_t *pid,
                     float kp,
                     float ki,
                     float kd,
                     float dt,
                     float integral_min,
                     float integral_max,
                     float out_min,
                     float out_max) {
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->dt = dt;
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->integral_min = integral_min;
    pid->integral_max = integral_max;
    pid->out_min = out_min;
    pid->out_max = out_max;
}

static float pid_update(pid_controller_t *pid, float setpoint, float measurement) {
    float error = setpoint - measurement;
    float derivative = (error - pid->prev_error) / pid->dt;
    float output;

    pid->integral += error * pid->dt;
    pid->integral = clampf(pid->integral, pid->integral_min, pid->integral_max);

    output = pid->kp * error
           + pid->ki * pid->integral
           + pid->kd * derivative;
    output = clampf(output, pid->out_min, pid->out_max);

    pid->prev_error = error;
    return output;
}

static void plant_init(plant_t *plant, float gain, float time_constant) {
    plant->state = 20.0f;
    plant->gain = gain;
    plant->time_constant = time_constant;
    plant->disturbance = 0.0f;
}

static float plant_update(plant_t *plant, float control, float dt_seconds) {
    float rate = (-plant->state + plant->gain * control + plant->disturbance) / plant->time_constant;
    plant->state += rate * dt_seconds;
    return plant->state;
}

static void modem_init(modem_fsm_t *modem) {
    modem->state = MODEM_IDLE;
    modem->upload_request = 0U;
    modem->response_ready = 0U;
    modem->scheduled_response_pending = 0U;
    modem->wait_deadline_ms = 0U;
    modem->retry_deadline_ms = 0U;
    modem->scheduled_response_at_ms = 0U;
    modem->response_text = NULL;
    modem->scheduled_response_text = NULL;
}

static void alarm_init(alarm_manager_t *alarm) {
    alarm->warning_active = 0U;
    alarm->fault_latched = 0U;
    alarm->report_pending = 0U;
    alarm->report_sent_for_current_condition = 0U;
    alarm->led_on = 0U;
    alarm->next_led_toggle_ms = 0U;
}

static void uart_rx_isr_fake(const char *line) {
    g_uart_event = 1U;
    g_modem.response_ready = 1U;
    g_modem.response_text = line;
}

static void schedule_modem_response(const char *line, uint32_t delay_ms) {
    g_modem.scheduled_response_pending = 1U;
    g_modem.scheduled_response_at_ms = g_tick_ms + delay_ms;
    g_modem.scheduled_response_text = line;
}

static void scenario_update(void) {
    if (g_tick_ms == 120U) {
        g_setpoint = 70.0f;
        printf("[%03lu ms] scenario: setpoint changed to %.1f\n",
               (unsigned long)g_tick_ms,
               g_setpoint);
    }

    if (g_tick_ms == 260U) {
        g_plant.disturbance = -12.0f;
        printf("[%03lu ms] scenario: disturbance injected\n",
               (unsigned long)g_tick_ms);
    }

    if (g_tick_ms == 340U) {
        g_plant.disturbance = 0.0f;
        printf("[%03lu ms] scenario: disturbance removed\n",
               (unsigned long)g_tick_ms);
    }

    if (g_tick_ms == 440U) {
        g_plant.state = 83.5f;
        printf("[%03lu ms] scenario: plant value jumped high to demonstrate fast protection\n",
               (unsigned long)g_tick_ms);
    }
}

static void systick_isr(void) {
    g_tick_ms++;

    scenario_update();
    g_feedback = plant_update(&g_plant, g_output, 0.001f);

    if ((g_alarm.fault_latched == 0U) && (g_feedback >= 82.0f)) {
        g_alarm.fault_latched = 1U;
        g_output = 0.0f;
        g_alarm.report_pending = 1U;
        g_alarm.report_sent_for_current_condition = 1U;
        printf("[%03lu ms] protect: fast shutdown triggered, output forced to zero\n",
               (unsigned long)g_tick_ms);
    }

    if ((g_modem.scheduled_response_pending != 0U)
        && time_reached(g_tick_ms, g_modem.scheduled_response_at_ms)) {
        g_modem.scheduled_response_pending = 0U;
        uart_rx_isr_fake(g_modem.scheduled_response_text);
    }

    release_periodic_task(&g_pid_task, g_tick_ms);
    release_periodic_task(&g_alarm_task, g_tick_ms);
    release_periodic_task(&g_modem_task, g_tick_ms);
}

static void pid_task_step(void) {
    g_pid_task.ready = 0U;

    if (g_alarm.fault_latched != 0U) {
        g_output = 0.0f;
        printf("[%03lu ms] pid : skipped because fault is latched\n",
               (unsigned long)g_tick_ms);
        return;
    }

    g_output = pid_update(&g_pid, g_setpoint, g_feedback);

    printf("[%03lu ms] pid : sp=%5.1f fb=%5.1f out=%5.1f\n",
           (unsigned long)g_tick_ms,
           g_setpoint,
           g_feedback,
           g_output);
}

static void alarm_task_step(void) {
    uint8_t condition_active;

    g_alarm_task.ready = 0U;

    g_alarm.warning_active = (g_feedback >= 65.0f) ? 1U : 0U;
    condition_active = (uint8_t)((g_alarm.warning_active != 0U) || (g_alarm.fault_latched != 0U));

    if (condition_active != 0U) {
        if (time_reached(g_tick_ms, g_alarm.next_led_toggle_ms)) {
            g_alarm.led_on = (g_alarm.led_on == 0U) ? 1U : 0U;
            g_alarm.next_led_toggle_ms = g_tick_ms + ((g_alarm.fault_latched != 0U) ? 80U : 150U);
            printf("[%03lu ms] alarm: led=%s warning=%u fault=%u\n",
                   (unsigned long)g_tick_ms,
                   (g_alarm.led_on != 0U) ? "ON" : "OFF",
                   g_alarm.warning_active,
                   g_alarm.fault_latched);
        }
    } else if (g_alarm.led_on != 0U) {
        g_alarm.led_on = 0U;
        printf("[%03lu ms] alarm: led=OFF warning cleared\n",
               (unsigned long)g_tick_ms);
    }

    if (condition_active == 0U) {
        g_alarm.report_sent_for_current_condition = 0U;
    }

    if ((condition_active != 0U)
        && (g_alarm.report_pending == 0U)
        && (g_alarm.report_sent_for_current_condition == 0U)
        && (g_modem.upload_request == 0U)
        && (g_modem.state == MODEM_IDLE)) {
        g_alarm.report_pending = 1U;
        g_alarm.report_sent_for_current_condition = 1U;
        printf("[%03lu ms] alarm: report request created\n",
               (unsigned long)g_tick_ms);
    }
}

static void modem_enter_retry_delay(void) {
    g_modem.state = MODEM_RETRY_DELAY;
    g_modem.retry_deadline_ms = g_tick_ms + 100U;
    printf("[%03lu ms] modem: entering retry delay\n",
           (unsigned long)g_tick_ms);
}

static void modem_task_step(void) {
    const char *rx_line = NULL;

    if ((g_modem_task.ready == 0U)
        && (g_uart_event == 0U)
        && (g_alarm.report_pending == 0U)
        && (g_modem.upload_request == 0U)) {
        return;
    }

    g_modem_task.ready = 0U;

    if (g_uart_event != 0U) {
        g_uart_event = 0U;
        rx_line = g_modem.response_text;
        g_modem.response_ready = 0U;
        printf("[%03lu ms] modem: rx event -> %s\n",
               (unsigned long)g_tick_ms,
               rx_line);
    }

    if ((g_alarm.report_pending != 0U) && (g_modem.upload_request == 0U)) {
        g_alarm.report_pending = 0U;
        g_alarm.report_sent_for_current_condition = 1U;
        g_modem.upload_request = 1U;
        printf("[%03lu ms] modem: upload request accepted\n",
               (unsigned long)g_tick_ms);
    }

    switch (g_modem.state) {
    case MODEM_IDLE:
        if (g_modem.upload_request != 0U) {
            printf("[%03lu ms] modem: send AT+HTTPINIT\n",
                   (unsigned long)g_tick_ms);
            schedule_modem_response("OK", 30U);
            g_modem.wait_deadline_ms = g_tick_ms + 80U;
            g_modem.state = MODEM_WAIT_HTTP_INIT_OK;
        }
        break;

    case MODEM_WAIT_HTTP_INIT_OK:
        if (rx_line != NULL) {
            if (rx_line[0] == 'O') {
                g_modem.state = MODEM_SEND_HTTP_URL;
            } else {
                modem_enter_retry_delay();
            }
        } else if (time_reached(g_tick_ms, g_modem.wait_deadline_ms)) {
            modem_enter_retry_delay();
        }
        break;

    case MODEM_SEND_HTTP_URL:
        printf("[%03lu ms] modem: send URL configuration\n",
               (unsigned long)g_tick_ms);
        schedule_modem_response("OK", 30U);
        g_modem.wait_deadline_ms = g_tick_ms + 80U;
        g_modem.state = MODEM_WAIT_HTTP_URL_OK;
        break;

    case MODEM_WAIT_HTTP_URL_OK:
        if (rx_line != NULL) {
            if (rx_line[0] == 'O') {
                g_modem.state = MODEM_SEND_HTTP_ACTION;
            } else {
                modem_enter_retry_delay();
            }
        } else if (time_reached(g_tick_ms, g_modem.wait_deadline_ms)) {
            modem_enter_retry_delay();
        }
        break;

    case MODEM_SEND_HTTP_ACTION:
        printf("[%03lu ms] modem: send HTTPACTION=1\n",
               (unsigned long)g_tick_ms);
        schedule_modem_response("+HTTPACTION:1,200,0", 60U);
        g_modem.wait_deadline_ms = g_tick_ms + 120U;
        g_modem.state = MODEM_WAIT_HTTP_ACTION_RESULT;
        break;

    case MODEM_WAIT_HTTP_ACTION_RESULT:
        if (rx_line != NULL) {
            printf("[%03lu ms] modem: upload finished with response %s\n",
                   (unsigned long)g_tick_ms,
                   rx_line);
            g_modem.state = MODEM_DONE;
        } else if (time_reached(g_tick_ms, g_modem.wait_deadline_ms)) {
            modem_enter_retry_delay();
        }
        break;

    case MODEM_DONE:
        printf("[%03lu ms] modem: request completed, back to idle\n",
               (unsigned long)g_tick_ms);
        g_modem.upload_request = 0U;
        g_modem.state = MODEM_IDLE;
        break;

    case MODEM_RETRY_DELAY:
        if (time_reached(g_tick_ms, g_modem.retry_deadline_ms)) {
            printf("[%03lu ms] modem: retrying request\n",
                   (unsigned long)g_tick_ms);
            g_modem.state = MODEM_IDLE;
        }
        break;

    default:
        g_modem.state = MODEM_IDLE;
        break;
    }
}

static void main_loop_iteration(void) {
    if (g_pid_task.ready != 0U) {
        pid_task_step();
    }

    if (g_alarm_task.ready != 0U) {
        alarm_task_step();
    }

    if ((g_modem_task.ready != 0U)
        || (g_uart_event != 0U)
        || (g_alarm.report_pending != 0U)
        || (g_modem.upload_request != 0U)) {
        modem_task_step();
    }
}

int main(void) {
    pid_init(&g_pid, 1.4f, 0.28f, 0.05f, 0.01f, -40.0f, 40.0f, 0.0f, 100.0f);
    plant_init(&g_plant, 1.0f, 0.08f);
    modem_init(&g_modem);
    alarm_init(&g_alarm);

    printf("=== bare-metal scheduler demo ===\n");
    printf("PID=10ms, alarm=20ms, modem step=20ms, events come from fake UART ISR\n");

    for (uint32_t i = 0U; i < DEMO_RUNTIME_MS; ++i) {
        systick_isr();

#if DEMO_BLOCKING_BUG
        if ((g_tick_ms >= 220U) && (g_tick_ms < 280U)) {
            continue;
        }
#endif

        main_loop_iteration();
    }

    printf("\n=== summary ===\n");
    printf("pid missed releases   : %lu\n", (unsigned long)g_pid_task.missed_releases);
    printf("alarm missed releases : %lu\n", (unsigned long)g_alarm_task.missed_releases);
    printf("modem missed releases : %lu\n", (unsigned long)g_modem_task.missed_releases);
    printf("final feedback        : %.2f\n", g_feedback);
    printf("final output          : %.2f\n", g_output);
    printf("fault latched         : %u\n", g_alarm.fault_latched);

    return 0;
}
