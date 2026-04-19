# 统一心跳在嵌入式系统中的应用

## 什么是统一心跳？

统一心跳可以理解成系统里的“公共时钟节拍”。

最常见的做法是：

- 用一个定时器每 `1ms` 产生一次中断
- 在中断里把系统时间 `tick` 加一
- 所有周期任务都基于这个 `tick` 判断自己是否到期

这个统一的时间基准，就叫统一心跳。

它的核心目的不是“让所有任务都在中断里跑”，而是：

- 给系统提供统一的时间参考
- 让任务释放时刻稳定
- 让主循环按节拍执行任务

## 为什么要有统一心跳？

如果没有统一心跳，很多任务会写成这样：

```c
while (1) {
    pid_step();
    alarm_step();
    modem_step();
}
```

这种写法的问题是：

- PID 什么时候执行，取决于前面代码跑了多久
- 报警扫描周期不固定
- 4G 任务一旦变慢，其他任务节拍全部被拖乱

统一心跳的价值就在于：

- `PID` 应该 10ms 一次，就稳定释放 10ms 一次
- `报警` 应该 20ms 一次，就稳定释放 20ms 一次
- `LED` 应该 100ms 一次，就稳定释放 100ms 一次

这样任务的“该什么时候执行”就不再受主循环路径影响。

## 统一心跳的基本结构

一个典型的统一心跳结构通常分成两部分。

### 1. 定时中断

定时中断里做很少的事：

- 系统 tick 计数加一
- 释放周期任务
- 记录事件
- 做极少量快速保护

### 2. 主循环

主循环里做真正的任务执行：

- 如果 PID 到期，就执行一次 PID
- 如果报警任务到期，就扫描一次报警
- 如果 4G 状态机到了调度时机，就推进一步

所以更准确的理解是：

- 中断负责“报时和唤醒”
- 主循环负责“干活”

## 统一心跳不等于所有任务 1ms 都跑

这是一个很容易误解的点。

比如统一心跳是 `1ms`，不代表：

- PID 每 1ms 跑一次
- 报警每 1ms 跑一次
- LED 每 1ms 翻转一次

真正的含义是：

- 系统拥有 `1ms` 分辨率的公共时间基准
- 各任务按自己的周期被释放

例如：

- PID：`10ms`
- 报警：`20ms`
- LED：`100ms`
- 参数保存：`1000ms`

## 周期任务如何基于统一心跳释放？

最常见的写法是给每个任务维护这些字段：

```c
typedef struct {
    uint32_t period_ms;
    uint32_t next_release_ms;
    uint8_t ready;
    uint32_t missed_releases;
} periodic_task_t;
```

字段含义：

- `period_ms`：任务周期
- `next_release_ms`：下一次应该被释放的时刻
- `ready`：当前是否已就绪，等待主循环执行
- `missed_releases`：如果主循环来不及执行，统计丢了几次

每次 `tick` 到来时，中断里检查：

```c
if (system_tick_ms >= task->next_release_ms) {
    task->ready = 1;
    task->next_release_ms += task->period_ms;
}
```

这样，任务就会按固定节拍被释放。

## 为什么统一心跳对 PID 很重要？

因为 PID 很依赖稳定的采样周期和控制周期。

如果本来设计的是：

- 每 `10ms` 采样一次
- 每 `10ms` 计算一次 PID

但实际运行时变成：

- 有时 10ms
- 有时 16ms
- 有时 28ms

那么：

- 积分项会累积失真
- 微分项会抖动变大
- 控制器行为会变得不稳定

所以在没有 RTOS 的项目里，统一心跳往往是保证 PID 周期稳定的基础设施。

## 统一心跳和状态机是什么关系？

统一心跳本身不等于状态机，但它经常和状态机一起工作。

比如 4G/HTTP 任务：

- 统一心跳负责决定“现在该不该轮到 4G 任务”
- 状态机负责决定“4G 任务这一次该做哪一步”

也就是说：

- 统一心跳解决“什么时候做”
- 状态机解决“这次做什么”

这两个是互补关系。

## 统一心跳示例代码讲解

本目录下的 `unified_tick_scheduler.c` 做了一件很专注的事：

- 用 `1ms` 心跳释放多个不同周期的任务
- 演示主循环如何按优先级处理任务
- 演示周期任务和事件任务如何协作

### 示例里的任务分配

- `control_task`：10ms 一次
- `alarm_task`：20ms 一次
- `led_task`：100ms 一次
- `modem_task`：20ms 一次，同时还会响应 UART 事件

### 核心释放逻辑

```c
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
```

这个写法的意义是：

- 到期就释放任务
- 如果上一次还没跑完，又到了下一次释放点，就统计一次 `missed_releases`
- 不盲目补做所有历史周期，而是保留“最新需要执行的一次”

这在控制类系统里很实用。

### 中断里做什么

```c
static void systick_isr_1ms(void) {
    g_system_tick_ms++;

    simulate_external_events();

    release_task(&g_control_task, g_system_tick_ms);
    release_task(&g_alarm_task, g_system_tick_ms);
    release_task(&g_led_task, g_system_tick_ms);
    release_task(&g_modem_task, g_system_tick_ms);
}
```

这里做的事情非常克制：

- 维护系统时间
- 释放周期任务
- 模拟外部事件

没有把真正的业务逻辑塞进中断里。

### 主循环里做什么

```c
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

    if ((g_modem_task.ready != 0U) || (g_uart_rx_event != 0U) || (g_upload_request != 0U)) {
        modem_task_step();
    }
}
```

这个顺序其实就是“裸机优先级”：

- 控制最高
- 报警其次
- LED 再后
- 4G 状态机最低

## 统一心跳实现时要注意什么？

### 1. 心跳周期要合理

心跳不能太慢，否则分辨率不够。

例如：

- 如果 PID 要 5ms 一次，而你的心跳是 10ms，那就不够用了

常见选择：

- `1ms`：最通用
- `5ms`：任务节拍要求没那么高时也够用

### 2. 中断里不要塞太多逻辑

统一心跳中断的职责是建立时间基准，不是把整个系统逻辑都搬进去。

更好的原则是：

- 中断里只做短小、确定、快速的事
- 主循环里做真正任务

### 3. 任务函数必须非阻塞

如果任务内部存在：

- `delay_ms()`
- `while (!ok) { }`
- 长时间等待串口返回

那么统一心跳再漂亮，也会被阻塞任务破坏。

### 4. 要关注任务超期

如果一个 10ms 任务经常在下一次释放时还没执行完，说明：

- 任务太重
- 优先级设计有问题
- 或者系统资源已经不足

这个时候 `missed_releases` 就很有价值。

## 统一心跳适合哪些场景？

非常适合：

- 无 RTOS 的裸机项目
- 有多个不同周期任务的项目
- 需要稳定控制周期的 PID 项目
- 同时有控制、报警、通信的混合型项目

## 总结

统一心跳是裸机系统里非常重要的基础设计。

它解决的不是“业务逻辑本身”，而是一个更底层的问题：

- 任务什么时候应该被稳定地执行

你可以把它理解成整个系统的时间骨架。

有了统一心跳之后：

- PID 周期更稳定
- 报警节拍更清晰
- 状态机更容易推进
- 整个系统更容易扩展和维护

如果你后面继续学习“软件定时器”“裸机调度器”“多周期任务系统”，统一心跳就是最好的起点之一。
