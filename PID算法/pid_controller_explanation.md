# PID 算法在嵌入式系统中的应用

## 什么是 PID 算法？

PID（Proportional-Integral-Derivative，比例-积分-微分）是一种经典闭环控制算法。它会根据目标值与实际反馈值之间的误差，持续调整控制输出，使系统尽快、稳定地逼近目标值。

在嵌入式系统中，PID 常见于：

- 温度控制
- 电机速度控制
- 舵机或云台位置控制
- 压力、流量、液位调节
- 电源、电流、电压闭环控制

## PID 三个部分分别做什么？

设误差为：

```text
e(k) = r(k) - y(k)
```

其中：

- `r(k)` 是目标值
- `y(k)` 是当前测量值
- `e(k)` 是当前误差

PID 由三部分组成：

### 1. 比例项 P

```text
P = Kp * e(k)
```

- 作用：误差一出现就立刻响应
- 特点：`Kp` 越大，响应越快，但过大容易震荡

### 2. 积分项 I

```text
I = Ki * Σ(e(k) * Ts)
```

- 作用：累积历史误差，消除稳态误差
- 特点：`Ki` 太大会导致积分饱和、超调增大、恢复变慢

### 3. 微分项 D

```text
D = Kd * (e(k) - e(k-1)) / Ts
```

- 作用：根据误差变化趋势提前制动
- 特点：可以抑制超调、改善动态性能，但对噪声敏感

## 什么是位置式 PID？

位置式 PID 直接计算当前时刻的“控制量绝对值”：

```text
u(k) = Kp * e(k)
     + Ki * Σ(e(i) * Ts)
     + Kd * (e(k) - e(k-1)) / Ts
```

这里的 `u(k)` 是当前控制输出本身，例如：

- 当前 PWM 占空比
- 当前阀门开度
- 当前电流给定值

### 位置式 PID 的特点

#### 优点

1. 形式直观，容易理解和调试
2. 输出值直接可用，适合多数基础闭环控制
3. 便于分析比例、积分、微分各自的贡献

#### 缺点

1. 积分项容易累积过大，出现积分饱和
2. 手动/自动切换时，若状态处理不好，输出容易跳变
3. 对执行器限幅后，如果没有抗积分饱和措施，恢复可能较慢

### 适用场景

- 温控系统
- 液位控制
- 对输出绝对值更关心的控制系统
- 便于教学和初学者理解的场景

## 什么是增量式 PID？

增量式 PID 不直接求控制量本身，而是先求“本次应该改多少”：

```text
Δu(k) = Kp * (e(k) - e(k-1))
      + Ki * e(k) * Ts
      + Kd * (e(k) - 2e(k-1) + e(k-2)) / Ts
```

然后更新输出：

```text
u(k) = u(k-1) + Δu(k)
```

也就是说，增量式 PID 更关心“输出需要增加多少或减少多少”。

### 增量式 PID 的特点

#### 优点

1. 每次只调整输出增量，适合步进式调节执行器
2. 手动/自动切换更容易做得平滑
3. 对某些控制对象来说，输出变化更柔和

#### 缺点

1. 必须依赖上一次输出和前两次误差
2. 如果输出状态丢失，控制器就失去了连续性
3. 理解门槛略高，调试时不如位置式直观

### 适用场景

- 电机速度控制
- 需要输出平滑变化的执行器调节
- 数字控制器中希望减少“控制突变”的系统

## 位置式 PID 与增量式 PID 的对比

| 对比项 | 位置式 PID | 增量式 PID |
| --- | --- | --- |
| 输出含义 | 当前时刻的绝对输出值 | 当前输出相对上一次的变化量 |
| 需要保存的状态 | 积分项、上次误差 | 上次输出、前两次误差 |
| 直观程度 | 更直观 | 略抽象 |
| 手动切自动 | 容易产生跳变 | 更容易平滑切换 |
| 积分饱和风险 | 更明显 | 也存在，但表现形式不同 |
| 常见应用 | 温度、位置、液位 | 速度、流量、连续调节 |

一个很容易混淆的点是：位置式 PID 里的“位置式”并不等于“位置控制”，它只是指算法直接算出当前输出值。
在离散公式选取一致、参数相同的情况下，这两种 PID 的闭环响应往往会非常接近，主要区别在于内部状态保存方式和工程实现体验。

## 示例代码结构分析

本目录中的 `pid_controller.c` 同时实现了这两种 PID，并做了一个简单的一阶对象仿真。

### 1. 位置式 PID 数据结构

```c
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
```

这里重点保存了：

- 当前积分累计值 `integral`
- 上一次误差 `prev_error`
- 积分限幅和输出限幅

### 2. 增量式 PID 数据结构

```c
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
```

这里重点保存了：

- 上一次误差 `prev_error`
- 上上次误差 `prev_prev_error`
- 上一次输出 `output`
- 便于调试观察的本次增量 `last_delta`

### 3. 位置式 PID 核心更新逻辑

```c
float positional_pid_update(positional_pid_t *pid, float setpoint, float measurement) {
    float error = setpoint - measurement;
    float derivative = (error - pid->prev_error) / pid->dt;

    pid->integral += error * pid->dt;
    pid->integral = clampf(pid->integral, pid->integral_min, pid->integral_max);

    float output = pid->kp * error
                 + pid->ki * pid->integral
                 + pid->kd * derivative;

    pid->prev_error = error;
    return clampf(output, pid->out_min, pid->out_max);
}
```

这个实现里加入了积分限幅，用来做最基本的抗积分饱和处理。

### 4. 增量式 PID 核心更新逻辑

```c
float incremental_pid_update(incremental_pid_t *pid, float setpoint, float measurement) {
    float error = setpoint - measurement;
    float delta_output = pid->kp * (error - pid->prev_error)
                       + pid->ki * error * pid->dt
                       + pid->kd * (error - 2.0f * pid->prev_error + pid->prev_prev_error) / pid->dt;

    pid->output += delta_output;
    pid->output = clampf(pid->output, pid->out_min, pid->out_max);

    pid->prev_prev_error = pid->prev_error;
    pid->prev_error = error;
    return pid->output;
}
```

这个版本体现了增量式 PID 的核心特点：先算增量，再叠加到当前输出上。

## 嵌入式实现时要注意什么？

### 1. 固定采样周期

PID 非常依赖采样周期 `Ts`。如果控制循环忽快忽慢，积分和微分项就会失真。实际工程中应优先保证：

- 定时器中断周期固定
- 任务调度周期稳定
- `dt` 与真实采样间隔一致

### 2. 输出限幅

实际执行器通常都有边界，例如：

- PWM 占空比 `0% ~ 100%`
- DAC 输出 `0 ~ 4095`
- 阀门开度 `0% ~ 100%`

所以 PID 输出必须做限幅。

### 3. 抗积分饱和

当输出已经打满，但误差还在持续累积时，积分项会越来越大，导致系统恢复很慢。常用方法有：

- 直接限制积分项上下限
- 输出饱和时暂停积分
- 使用积分分离，仅在误差较小时启用积分

### 4. 微分项滤波

微分会放大高频噪声，所以工程中经常：

- 对反馈先滤波
- 对微分项加低通滤波
- 只对测量值做微分，减小设定值突变带来的冲击

### 5. 浮点与定点

如果 MCU 没有 FPU，或者性能预算很紧，可以改用定点数实现 PID，但要特别注意：

- 缩放因子选择
- 溢出保护
- 参数量化误差

## 学习和调参建议

一个简单实用的入门顺序是：

1. 先只开 `P`，观察系统是否能快速接近目标
2. 再加少量 `I`，消除稳态误差
3. 最后再加 `D`，抑制超调和振荡

调参时可以重点观察：

- 上升速度是否足够
- 超调是否过大
- 稳态误差是否存在
- 扰动后恢复是否够快
- 控制输出是否抖动

## 建议的学习顺序

如果你刚开始接触 PID，可以按下面顺序学习：

1. 先理解误差 `e = 目标值 - 反馈值`
2. 再理解 P、I、D 三项分别解决什么问题
3. 先掌握位置式 PID
4. 再理解增量式 PID 为什么是“算变化量”
5. 最后结合具体对象做仿真和调参

## 总结

PID 是嵌入式控制里最经典、最实用的算法之一。

- 位置式 PID：直接算当前输出，直观、常用、好理解
- 增量式 PID：计算输出改变量，适合平滑调节和数字控制实现

学习 PID 的关键不只是记公式，更重要的是理解：

- 误差是怎么产生的
- 三个环节分别在解决什么问题
- 控制器输出为什么会变大、变小或震荡

把这些和实际对象的响应过程联系起来，你会更快真正掌握 PID。
