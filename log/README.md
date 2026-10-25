# log/ — DUST_LOG 自研日志系统

`log/` 是一套**四层流水线**日志系统：一次性四色日志（INF/ERR/OK/WRN）+ DBG 可选择流式日志。

它回答的问题是：

```text
一条日志从「调用点」到「串口线上」，中间经过哪些关口，每一关由谁负责？
```

---

## 一、四层结构

```text
① 命令层  cmd/        log_cmd.hpp/.cpp        词法解析 + 查表匹配 + 子命令处理
② 加工层  process/    log_process.hpp/.cpp    日志入口 / 分频 / 格式化 / 上色 / 帧包装
③ 队列层  queue/      log_queue.hpp           记录队列（原始请求）+ 帧队列（三档优先级）
④ 发送层  transport/  log_transport.hpp       DMA 泵（Stream 绑定 / 空闲续发 / TX_DONE）
                       log_record.hpp / log_policy.hpp  ← ③ 的数据定义
```

**`log.hpp` / `log.cpp` 是门面**：只做四层串接 + 对外宏 + shell 线程装配入口，**不含实现**。

| 层 | 输入 | 输出 | 不管什么 |
|---|---|---|---|
| ① 命令层 | `log on imu 10` 的剩余行 | 调用加工层改注册表 / 打印 usage | 日志怎么发送 |
| ② 加工层 | `("x=%d", 1)` 的 fmt + 参数 | 已格式化 + 已上色的文本 | 队列怎么排、字节怎么发 |
| ③ 队列层 | 原始请求 / 已包装帧 | 可出队的数据 | 内容是什么、发往哪里 |
| ④ 发送层 | 一个 `TxFrame&` | 交给 `Stream::Send` 的字节 | 帧从哪来、优先级如何 |

---

## 二、单向流水线（硬约束）

```text
① 命令层  →  ② 加工层  →  ③ 队列层  →  ④ 发送层
```

**只允许「上层 → 紧邻下层」的单向调用。**

### 禁止

| 形态 | 例子 |
|---|---|
| **跳层** | ① 直接调 ③ / ④ |
| **反向** | ③ 回调 ②；④ 反向点 ② 的回调 |
| **同层互调** | `log_cmd.cpp` 里 `CmdOn` 调 `CmdList` |
| **越权** | 加工层以外的地方直接读写 `dbg_` / `recq_` / `txq_` / `pump_` |

### 唯一例外：④ → ② 的**异步唤醒**

```text
④ HandleTxDone()  --k_sem_give-->  shell 线程醒来
                                      ↓
                          ② ExpandOneRecord()
```

这是**信号量唤醒**，不是 ④ 调 ②：④ 只做 `k_sem_give`，谁醒来做什么由 shell 线程决定。**④ 不 include ② 的任何头文件。**

### 约束靠什么保证

**② 加工层是唯一持有四层实例的地方**：

```cpp
// process/log_process.cpp
DebugSourceRegistry dbg_  {};      // DBG 注册表
LogTransport        pump_ {};      // 发送泵
TxFrameQueue        txq_  {};      // 帧队列
LogRecordQueue      recq_ {};      // 记录队列
bool                shell_own_ = false;
```

命令层拿不到这些实例，只能通过 `logproc::` 的公开函数操作 → **越权无从下手**。

### 目录即约束

| 文件 | 允许 include | 禁止 include |
|---|---|---|
| `cmd/log_cmd.cpp` | `log_process.hpp` | `log_queue.hpp` / `log_transport.hpp` |
| `process/log_process.cpp` | `queue/*` / `transport/*` | `log_cmd.hpp` |
| `queue/*.hpp` | `log_record.hpp` / `log_policy.hpp` | `log_process.hpp` / `log_cmd.hpp` |
| `transport/log_transport.hpp` | `log_queue.hpp` / `stream.hpp` | `log_process.hpp` / `log_cmd.hpp` |

`cmd/` 里出现 `#include "log_queue.hpp"` = 跨了两个目录，一眼违例。

---

## 三、一条日志的完整流向

### 3.1 一次性日志（INF / ERR / OK / WRN）

```text
DUST_LOG_INF("x=%d", 1)
  → shell::logproc::Info(fmt, ...)          ② 加工层
  → Emit(LogChannel::Info, fmt, ap)
      │
      ├─ shell_own_ == false（boot 早期，同步直发）
      │     1. vsnprintf 展开
      │     2. Wrap(policy, text, out)  加前缀 + 上色 + \r\n
      │     3. QueueFrameAndSendNow → txq_ 入帧 → 立即 pump（不等 shell 线程）
      │
      └─ shell_own_ == true（运行期，异步入队）
            1. BuildRecordFromVaList  参数快照（不格式化！）
            2. QueueLogRecord → recq_ 入队
            3. k_sem_give 唤醒 shell 线程
                   ↓
            shell 线程：ExpandOneRecord()
                  1. recq_ 出队
                  2. FormatRecordToText  展开为纯文本
                  3. Wrap  加前缀 + 上色 + \r\n
                  4. QueueFrameForShellSend → txq_ 入帧 + give
                  5. SendNextFrameIfIdle → pump
```

### 3.2 DBG 流式日志

```text
DUST_LOG_DBG("imu", "roll=%f", r)
  → shell::logproc::Debug(FindOrCreateDebugEntry("imu"), fmt, ...)
      1. shell_own_ == false          → 直接返回（boot 早期无 DBG）
      2. e == nullptr                 → 静默（条目池满）
      3. e != dbg_.Active()           → 静默（未选中）
      4. PassDivisor(e) == false      → 静默（分频丢弃）★
      5. 之后同 3.1 的异步入队路径
```

**★ 分频过滤**：`log on imu 10` 表示每 10 条打印 1 条。用**递减配额**实现，不用取余：

```cpp
// LogEntry: atomic_t divisor;  atomic_t quota;
const atomic_val_t divisor = atomic_get(&e->divisor);
if (divisor <= 1) return true;                  // 不分频：短路
if (atomic_dec(&e->quota) > 0) return false;    // 还有配额：丢弃
atomic_set(&e->quota, divisor - 1);             // 配额用尽：放行并重填
```

- `Select` 时 `quota = 0` → **第一条立即可见**
- `quota` 值域锁在 `0 ~ divisor-1`，**永不溢出**
- `divisor` / `quota` 都是 `atomic_t`：任务写、ISR 读

### 3.3 命令响应

```text
log on imu 10
  → shell ProcessLine 查 .shell_cmd 段 → Shell::ProcessLine
  → Shell::ProcessLogCommand → shell::logcmd::Process
      → NextToken + DispatchSubCmd 查表 → CmdOn
          → logproc::SelectDebugEntry("imu", 10)   改注册表，不输出
          → logproc::CommandLine("log on: ok x10")
              → Wrap（Command 通道策略：无颜色、带 \r\n）
              → QueueFrameAndSendNow → txq_ → pump
```

命令响应**跳过 `recq_`**，直接 `txq_` + 直发（要立刻可见，不能等 shell 线程）。

---

## 四、文件清单

| 文件 | 层 | 行数 | 职责 |
|---|---|---|---|
| `log.hpp` | 门面 | 144 | 对外宏 + `Log` 类（纯转发）+ 层结构说明 |
| `log.cpp` | 门面 | 41 | `Init()` + `ProcessLogCommand()` |
| `cmd/log_cmd.hpp` | ① | 28 | 命令层声明 |
| `cmd/log_cmd.cpp` | ① | 141 | `DispatchSubCmd` 表 + 4 个子命令 |
| `process/log_process.hpp` | ② | 194 | 加工层声明 + 层职责说明 |
| `process/log_process.cpp` | ② | 363 | 四个实例 + 全部加工实现 |
| `process/log_debug.hpp` | ② | 160 | DBG 注册表（名字/选中/分频/quota） |
| `queue/log_queue.hpp` | ③ | 324 | `LogRecordQueue` + `TxFrameQueue` |
| `queue/log_record.hpp` | ③ | 208 | `LogRecord` + 参数快照 + 文本展开 |
| `queue/log_policy.hpp` | ③ | 120 | 通道策略表（颜色/前缀/优先级） |
| `transport/log_transport.hpp` | ④ | 79 | 发送泵 |

---

## 五、对外接口

### 5.1 日志宏（唯一的使用方式）

```cpp
DUST_LOG_INF("x=%d", 1);     // 黑，一次性
DUST_LOG_ERR("fail %d", e);  // 红
DUST_LOG_OK("done");         // 绿
DUST_LOG_WRN("retry %d", n); // 橙
DUST_LOG_DBG("imu", "roll=%f", r);   // 静默，log on imu 后打印
```

**注意**：宏直接落到 `shell::logproc::`，不经过 `Log` 类（避免可变参数二次转发丢失）。

### 5.2 shell 命令

```text
log list                列出所有 DBG 条目：名字 / 状态 / 模式 / 分频
log on <name> [N]       选中并可选分频（N=每 N 条打 1 条，默认 1）
log off                 取消选中
log mode <name> on|off  切换输出模式（on/vofa=纯文本，off/color=上色）
```

`log list` 输出：

```text
imu             [ON]  [color]  x10     ← 选中，上色，每 10 条打 1 条
remote          [off] [vofa]   x1      ← 未选中，纯文本，不分频
```

### 5.3 装配（shell 线程）

```cpp
// framework/shell/shell.cpp
static UartDma rx {};
cfg.base_cfg.tx_cb = shell::Log::HandleTxDone;   // TX 完成回调
rx.Init(DEVICE_DT_GET(DT_ALIAS(shell_uart)), cfg);
shell::Log::BindOutputStream(&rx);
shell::Log::MarkShellThreadReady();              // 自此切到异步路径
```

shell 线程循环：

```cpp
shell::Log::SendNextFrameIfIdle();               // DMA 空闲则续发
while (shell::Log::FormatOnePendingRecord()) {   // 展开积压的原始请求
    shell::Log::SendNextFrameIfIdle();
}
```

---

## 六、三档优先级

| 档 | 谁用 | 行为 |
|---|---|---|
| `Event`（0） | INF/ERR/OK/WRN | 插队头，**永不挤** |
| `Cmd`（1） | 命令响应 | 插事件后、DBG 前 |
| `Dbg`（2） | DBG 流式 | 排队尾，**先被挤** |

帧池 8 × 128B。池满时：`Event` 挤掉最低档的 `Dbg`；`Dbg` 自己来了池满就直接丢。

---

## 七、容量与边界

| 项 | 值 |
|---|---|
| 帧池 | 8 帧 × 128 B |
| 单帧有效载荷 | ≤ 127 B（`kTxMaxLen` 截断） |
| 建议内容长度 | ≤ 98 B（含前缀 + 颜色 + `\r\n` 最长 29 B） |
| 记录队列深度 | 8 |
| DBG 条目上限 | 64 |
| 参数快照槽 | 4（`%f` 占 2 槽） |
| 稳态吞吐 | < 92160 B/s（921600 波特）时无限闭环 |

**超限行为**：内容超 127 B 截尾；记录队列满则丢；参数超 4 槽则后续参数不记录（`BuildRecordFromVaList` 仍消费以防失步）。

---

## 八、调试要点

### 波特率必须与 overlay 一致

`project/boards/*/*/*.overlay` 里 shell-uart 的 `current-speed` 决定线上速率。**收到零散单字节 / 乱码 = 波特率错**，不是协议问题。

### "长度对但内容全 0" = D-Cache 问题

`UartDma` 的 TX/RX 缓冲若被 CPU 写进 D-Cache 没写回，DMA 读到旧值。**HPM 平台上与 PMP guard 冲突，只能 `CONFIG_DCACHE=n`**（详见 `doc/UART缓冲nocache化规划.md`）。

### DBG 没输出，按顺序查

1. `log list` 里有没有这个名字（没有 = 代码里没调过 `DUST_LOG_DBG`）
2. `[ON]` 还是 `[off]`
3. 分频 `xN` 是不是设太大
4. `log mode` 是不是切到了 `vofa`（纯文本，VOFA+ 才能解析）

---

## 九、扩展方式

### 加一条 DBG 流

```cpp
DUST_LOG_DBG("myname", "v0=%d,v1=%d", a, b);   // 一行，无需注册
```

首次调用时 `FindOrCreateDebugEntry` 自动创建条目（上限 64）。

### 加一个 log 子命令

1. `cmd/log_cmd.cpp` 里写一个 `static void CmdXxx(uint8_t* args)`
2. `kSubCmds[]` 表里加一行 `{ "xxx", CmdXxx }`
3. 要输出就调 `logproc::CommandLine(...)`，**不许碰 `dbg_` / `txq_`**

### 加一个日志通道（如 `WRN` 加到 `Debug`）

只在 `queue/log_policy.hpp` 的 `kLogChannelPolicies[]` 里加一行，上色/前缀/优先级自动生效。
