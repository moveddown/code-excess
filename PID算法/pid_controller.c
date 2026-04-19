#include <stdio.h>

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
} positional_pid_t;

typedef struct {
    float kp;
    float ki;
    float kd;
    float dt;
    float prev_error;
    float prev_prev_error;
    float output;
    float last_delta;
    float out_min;
    float out_max;
} incremental_pid_t;

typedef struct {
    float state;
    float gain;
    float time_constant;
} first_order_plant_t;

static float clampf(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

void positional_pid_init(positional_pid_t *pid,
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

void incremental_pid_init(incremental_pid_t *pid,
                          float kp,
                          float ki,
                          float kd,
                          float dt,
                          float out_min,
                          float out_max) {
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->dt = dt;
    pid->prev_error = 0.0f;
    pid->prev_prev_error = 0.0f;
    pid->output = 0.0f;
    pid->last_delta = 0.0f;
    pid->out_min = out_min;
    pid->out_max = out_max;
}

void plant_init(first_order_plant_t *plant, float gain, float time_constant) {
    plant->state = 0.0f;
    plant->gain = gain;
    plant->time_constant = time_constant;
}

float positional_pid_update(positional_pid_t *pid, float setpoint, float measurement) {
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

float incremental_pid_update(incremental_pid_t *pid, float setpoint, float measurement) {
    float error = setpoint - measurement;
    float delta_output = pid->kp * (error - pid->prev_error)
                       + pid->ki * error * pid->dt
                       + pid->kd * (error - 2.0f * pid->prev_error + pid->prev_prev_error) / pid->dt;

    pid->last_delta = delta_output;
    pid->output += delta_output;
    pid->output = clampf(pid->output, pid->out_min, pid->out_max);

    pid->prev_prev_error = pid->prev_error;
    pid->prev_error = error;
    return pid->output;
}

float plant_update(first_order_plant_t *plant, float control, float disturbance, float dt) {
    float rate = (-plant->state + plant->gain * control + disturbance) / plant->time_constant;
    plant->state += rate * dt;
    return plant->state;
}

void run_pid_comparison_demo(void) {
    const float dt = 0.1f;
    positional_pid_t positional_pid;
    incremental_pid_t incremental_pid;
    first_order_plant_t positional_plant;
    first_order_plant_t incremental_plant;

    positional_pid_init(&positional_pid, 1.2f, 0.35f, 0.08f, dt, -20.0f, 20.0f, 0.0f, 100.0f);
    incremental_pid_init(&incremental_pid, 1.2f, 0.35f, 0.08f, dt, 0.0f, 100.0f);
    plant_init(&positional_plant, 1.0f, 1.5f);
    plant_init(&incremental_plant, 1.0f, 1.5f);

    printf("=== PID 控制器对比示例 ===\n");
    printf("系统模型：一阶惯性对象，目标值在第 10 步从 0 切换到 10，第 35 步引入扰动。\n");
    printf("说明：当离散公式和参数等效时，两种 PID 的闭环响应通常会比较接近，差异主要在实现形式。\n\n");
    printf("%-4s %-8s %-12s %-12s %-12s %-12s %-12s\n",
           "步数", "目标值", "位置式输出", "位置式反馈", "增量值", "增量式输出", "增量式反馈");

    for (int step = 0; step < 60; ++step) {
        float setpoint = (step < 10) ? 0.0f : 10.0f;
        float disturbance = (step >= 35 && step < 45) ? -1.5f : 0.0f;
        float positional_output = positional_pid_update(&positional_pid, setpoint, positional_plant.state);
        float incremental_output = incremental_pid_update(&incremental_pid, setpoint, incremental_plant.state);
        float positional_feedback = plant_update(&positional_plant, positional_output, disturbance, dt);
        float incremental_feedback = plant_update(&incremental_plant, incremental_output, disturbance, dt);

        if (step < 15 || step % 5 == 0 || (step >= 34 && step <= 46)) {
            printf("%-4d %-8.2f %-12.2f %-12.2f %-12.2f %-12.2f %-12.2f\n",
                   step,
                   setpoint,
                   positional_output,
                   positional_feedback,
                   incremental_pid.last_delta,
                   incremental_output,
                   incremental_feedback);
        }
    }
}

int main(void) {
    run_pid_comparison_demo();
    return 0;
}
