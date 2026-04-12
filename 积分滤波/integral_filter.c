#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

// 积分滤波器结构体
typedef struct {
    float sum;        // 积分和
    float time_constant;  // 时间常数
    float last_input; // 上一次输入值
    float last_output; // 上一次输出值
    uint32_t last_time; // 上一次采样时间
    uint8_t initialized; // 初始化标志
} integral_filter_t;

// 初始化积分滤波器
void integral_filter_init(integral_filter_t *filter, float time_constant) {
    filter->sum = 0.0f;
    filter->time_constant = time_constant;
    filter->last_input = 0.0f;
    filter->last_output = 0.0f;
    filter->last_time = 0;
    filter->initialized = 0;
}

// 积分滤波处理函数
float integral_filter_update(integral_filter_t *filter, float input, uint32_t current_time) {
    if (!filter->initialized) {
        // 首次初始化
        filter->last_input = input;
        filter->last_time = current_time;
        filter->initialized = 1;
        return 0.0f;
    }
    
    // 计算时间差
    uint32_t delta_time = current_time - filter->last_time;
    if (delta_time == 0) {
        return filter->last_output;
    }
    
    // 转换为秒
    float dt = delta_time / 1000.0f; // 假设时间单位为毫秒
    
    // 计算积分增量（梯形积分）
    float integral_increment = (input + filter->last_input) * dt / 2.0f;
    
    // 更新积分和
    filter->sum += integral_increment;
    
    // 应用时间常数（可选，用于限制积分增长）
    if (filter->time_constant > 0) {
        filter->sum = filter->sum * expf(-dt / filter->time_constant);
    }
    
    // 更新状态
    filter->last_input = input;
    filter->last_time = current_time;
    filter->last_output = filter->sum;
    
    return filter->sum;
}

// 重置积分滤波器
void integral_filter_reset(integral_filter_t *filter) {
    filter->sum = 0.0f;
    filter->last_input = 0.0f;
    filter->last_output = 0.0f;
    filter->last_time = 0;
    filter->initialized = 0;
}

// 获取当前积分值
float integral_filter_get_value(integral_filter_t *filter) {
    return filter->sum;
}

// 示例1：使用积分滤波检测信号变化
void example_signal_detection(void) {
    printf("=== 示例1：信号变化检测 ===\n");
    
    integral_filter_t filter;
    integral_filter_init(&filter, 1.0f); // 1秒时间常数
    
    // 模拟信号输入
    float input_signals[] = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  // 初始稳定状态
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f,  // 信号变化
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f   // 信号恢复
    };
    
    uint32_t time = 0;
    for (int i = 0; i < sizeof(input_signals) / sizeof(float); i++) {
        float output = integral_filter_update(&filter, input_signals[i], time);
        printf("时间: %3dms, 输入: %.2f, 积分输出: %.2f\n", time, input_signals[i], output);
        time += 100; // 100ms采样间隔
    }
    
    integral_filter_reset(&filter);
}

// 示例2：使用积分滤波计算平均值
void example_average_calculation(void) {
    printf("\n=== 示例2：平均值计算 ===\n");
    
    integral_filter_t filter;
    integral_filter_init(&filter, 0.0f); // 无时间常数，纯积分
    
    // 模拟传感器数据
    float sensor_data[] = {
        25.1f, 25.3f, 24.9f, 25.0f, 25.2f,
        25.5f, 25.4f, 25.2f, 25.3f, 25.1f
    };
    
    uint32_t time = 0;
    int sample_count = 0;
    
    for (int i = 0; i < sizeof(sensor_data) / sizeof(float); i++) {
        integral_filter_update(&filter, sensor_data[i], time);
        sample_count++;
        time += 100;
    }
    
    float sum = integral_filter_get_value(&filter);
    float average = sum / (sample_count * 0.1f); // 0.1是采样间隔（秒）
    
    printf("采样次数: %d\n", sample_count);
    printf("积分和: %.2f\n", sum);
    printf("平均值: %.2f\n", average);
    
    integral_filter_reset(&filter);
}

// 示例3：使用积分滤波检测电路故障
void example_fault_detection(void) {
    printf("\n=== 示例3：电路故障检测 ===\n");
    
    integral_filter_t filter;
    integral_filter_init(&filter, 2.0f); // 2秒时间常数
    
    // 模拟电流信号
    float current_signals[] = {
        0.5f, 0.5f, 0.5f, 0.5f, 0.5f,  // 正常电流
        2.0f, 2.0f, 2.0f, 2.0f, 2.0f,  // 过电流
        0.5f, 0.5f, 0.5f, 0.5f, 0.5f   // 恢复正常
    };
    
    uint32_t time = 0;
    float threshold = 1.0f; // 故障阈值
    
    for (int i = 0; i < sizeof(current_signals) / sizeof(float); i++) {
        float output = integral_filter_update(&filter, current_signals[i] - 0.5f, time); // 减去正常电流
        
        printf("时间: %3dms, 电流: %.2fA, 积分输出: %.2f", time, current_signals[i], output);
        
        if (output > threshold) {
            printf(" -> 检测到故障！");
        }
        printf("\n");
        
        time += 100; // 100ms采样间隔
    }
    
    integral_filter_reset(&filter);
}

// 主函数
int main(void) {
    printf("=== 积分滤波器示例 ===\n\n");
    
    example_signal_detection();
    example_average_calculation();
    example_fault_detection();
    
    printf("\n=== 示例完成 ===\n");
    
    return 0;
}