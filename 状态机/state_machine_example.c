#include <stdio.h>
#include <stdint.h>

// 状态定义
typedef enum {
    STATE_IDLE,
    STATE_INIT,
    STATE_RUNNING,
    STATE_PAUSED,
    STATE_ERROR,
    STATE_MAX
} system_state_t;

// 事件定义
typedef enum {
    EVENT_START,
    EVENT_PAUSE,
    EVENT_RESUME,
    EVENT_STOP,
    EVENT_ERROR_DETECTED,
    EVENT_ERROR_RESOLVED,
    EVENT_MAX
} system_event_t;

// 状态转换表
typedef struct {
    system_state_t current_state;
    system_event_t event;
    system_state_t next_state;
    void (*action)(void);
} state_transition_t;

// 全局状态变量
system_state_t current_state = STATE_IDLE;

// 动作函数声明
void action_idle_to_init(void);
void action_init_to_running(void);
void action_running_to_paused(void);
void action_paused_to_running(void);
void action_any_to_error(void);
void action_error_to_idle(void);
void action_running_to_idle(void);
void action_paused_to_idle(void);

// 状态转换表定义
state_transition_t state_transitions[] = {
    // 当前状态       事件               下一状态        动作函数
    { STATE_IDLE,     EVENT_START,       STATE_INIT,     action_idle_to_init },
    { STATE_INIT,     EVENT_START,       STATE_RUNNING,  action_init_to_running },
    { STATE_RUNNING,  EVENT_PAUSE,       STATE_PAUSED,   action_running_to_paused },
    { STATE_PAUSED,   EVENT_RESUME,      STATE_RUNNING,  action_paused_to_running },
    { STATE_RUNNING,  EVENT_STOP,        STATE_IDLE,     action_running_to_idle },
    { STATE_PAUSED,   EVENT_STOP,        STATE_IDLE,     action_paused_to_idle },
    { STATE_IDLE,     EVENT_ERROR_DETECTED, STATE_ERROR,  action_any_to_error },
    { STATE_INIT,     EVENT_ERROR_DETECTED, STATE_ERROR,  action_any_to_error },
    { STATE_RUNNING,  EVENT_ERROR_DETECTED, STATE_ERROR,  action_any_to_error },
    { STATE_PAUSED,   EVENT_ERROR_DETECTED, STATE_ERROR,  action_any_to_error },
    { STATE_ERROR,    EVENT_ERROR_RESOLVED, STATE_IDLE,   action_error_to_idle },
};

// 动作函数实现
void action_idle_to_init(void) {
    printf("[ACTION] 从IDLE状态进入INIT状态，初始化系统...\n");
    // 这里可以添加实际的初始化代码
}

void action_init_to_running(void) {
    printf("[ACTION] 从INIT状态进入RUNNING状态，系统开始运行...\n");
    // 这里可以添加实际的启动代码
}

void action_running_to_paused(void) {
    printf("[ACTION] 从RUNNING状态进入PAUSED状态，系统暂停...\n");
    // 这里可以添加实际的暂停代码
}

void action_paused_to_running(void) {
    printf("[ACTION] 从PAUSED状态进入RUNNING状态，系统恢复运行...\n");
    // 这里可以添加实际的恢复代码
}

void action_any_to_error(void) {
    printf("[ACTION] 进入ERROR状态，处理错误...\n");
    // 这里可以添加实际的错误处理代码
}

void action_error_to_idle(void) {
    printf("[ACTION] 从ERROR状态进入IDLE状态，错误已解决...\n");
    // 这里可以添加实际的错误解决代码
}

void action_running_to_idle(void) {
    printf("[ACTION] 从RUNNING状态进入IDLE状态，系统停止...\n");
    // 这里可以添加实际的停止代码
}

void action_paused_to_idle(void) {
    printf("[ACTION] 从PAUSED状态进入IDLE状态，系统停止...\n");
    // 这里可以添加实际的停止代码
}

// 状态机处理函数
void state_machine_handle_event(system_event_t event) {
    uint8_t i;
    uint8_t transition_found = 0;
    
    // 遍历状态转换表，查找匹配的转换
    for (i = 0; i < sizeof(state_transitions) / sizeof(state_transition_t); i++) {
        if (state_transitions[i].current_state == current_state && 
            state_transitions[i].event == event) {
            
            // 执行动作函数
            if (state_transitions[i].action != NULL) {
                state_transitions[i].action();
            }
            
            // 更新当前状态
            current_state = state_transitions[i].next_state;
            printf("[STATE] 当前状态: %d\n", current_state);
            transition_found = 1;
            break;
        }
    }
    
    if (!transition_found) {
        printf("[ERROR] 无效的状态转换: 当前状态=%d, 事件=%d\n", current_state, event);
    }
}

// 状态名称获取函数
const char* get_state_name(system_state_t state) {
    switch (state) {
        case STATE_IDLE: return "IDLE";
        case STATE_INIT: return "INIT";
        case STATE_RUNNING: return "RUNNING";
        case STATE_PAUSED: return "PAUSED";
        case STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

// 事件名称获取函数
const char* get_event_name(system_event_t event) {
    switch (event) {
        case EVENT_START: return "START";
        case EVENT_PAUSE: return "PAUSE";
        case EVENT_RESUME: return "RESUME";
        case EVENT_STOP: return "STOP";
        case EVENT_ERROR_DETECTED: return "ERROR_DETECTED";
        case EVENT_ERROR_RESOLVED: return "ERROR_RESOLVED";
        default: return "UNKNOWN";
    }
}

// 主函数，用于测试状态机
int main(void) {
    printf("=== 嵌入式系统状态机示例 ===\n");
    printf("初始状态: %s\n", get_state_name(current_state));
    
    // 测试状态转换
    printf("\n1. 发送 START 事件...\n");
    state_machine_handle_event(EVENT_START);
    
    printf("\n2. 发送 START 事件...\n");
    state_machine_handle_event(EVENT_START);
    
    printf("\n3. 发送 PAUSE 事件...\n");
    state_machine_handle_event(EVENT_PAUSE);
    
    printf("\n4. 发送 RESUME 事件...\n");
    state_machine_handle_event(EVENT_RESUME);
    
    printf("\n5. 发送 ERROR_DETECTED 事件...\n");
    state_machine_handle_event(EVENT_ERROR_DETECTED);
    
    printf("\n6. 发送 ERROR_RESOLVED 事件...\n");
    state_machine_handle_event(EVENT_ERROR_RESOLVED);
    
    printf("\n7. 发送 START 事件...\n");
    state_machine_handle_event(EVENT_START);
    
    printf("\n8. 发送 STOP 事件...\n");
    state_machine_handle_event(EVENT_STOP);
    
    printf("\n=== 状态机测试完成 ===\n");
    
    return 0;
}