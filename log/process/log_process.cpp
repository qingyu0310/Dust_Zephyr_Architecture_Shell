/**
 * @file log_process.cpp
 * @author qingyu
 * @brief [② 加工层] DUST_LOG 加工层实现 — 日志入口 / 分频过滤 / 格式化 / 上色 / 帧包装
 * @version 0.2
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "log_process.hpp"
#include "log_record.hpp"
#include <cstdio>
#include <cstring>
#include "log_queue.hpp"
#include "log_transport.hpp"

#pragma message "Compiling Cmd/Shell/Log/Process"

namespace shell::logproc {

DebugSourceRegistry dbg_     {};            // DBG 源注册表（名字/选中/遍历）
LogTransport        pump_    {};            // 发送泵（通道/发送中标志/续发）
TxFrameQueue        txq_     {};            // 发送帧池 + 三档优先级队列
LogRecordQueue      recq_    {};            // 原始请求池 + FIFO 队列
bool                shell_own_ = false;     // shell 线程已接管发送（Task 首行置 true，boot 早期为 false 直发）

/**
 * @brief 初始化加工层持有的四层实例
 * @return true 初始化成功
 */
bool Init()
{
    dbg_.Reset();
    pump_.Reset();
    txq_.ResetFrameQueue();
    recq_.ResetRecordQueue();
    return true;
}

/**
 * @brief 绑定发送通道
 * @param s 已初始化的流
 */
void BindOutputStream(Stream* s)
{
    pump_.Bind(s);
}

/**
 * @brief 标记 shell 线程已接管发送
 */
void MarkShellThreadReady()
{
    shell_own_ = true;
}

/**
 * @brief 分频过滤：本条 DBG 是否放行
 * @param e DBG 条目
 * @return true=放行；false=本次丢弃
 */
bool PassDivisor(LogEntry* e)
{
    const atomic_val_t divisor = atomic_get(&e->divisor);
    if (divisor <= 1) return true;                  				 // 不分频：短路，不碰 quota

    if (atomic_dec(&e->quota) > 0) return false;    			// 还有配额：丢弃
    atomic_set(&e->quota, divisor - 1);               // 配额用尽：放行并重填
    return true;
}

/**
 * @brief 帧包装：前缀 + 上色 + \r\n
 * @param policy 通道策略
 * @param text   待包装的纯文本
 * @param out    输出缓冲
 * @param cap    输出缓冲容量
 * @return 写入字节数；<=0 表示失败
 */
int Wrap(const LogChannelPolicy& policy, const char* text, char* out, size_t cap)
{
    if (!policy.ansi_color) {
        return snprintf(out, cap, "%s%s\r\n", policy.prefix, text);
    }

    const uint32_t rgb = static_cast<uint32_t>(policy.color);
    return snprintf(out, cap, "\x1b[38;2;%d;%d;%dm%s%s\x1b[0m\r\n", (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, policy.prefix, text);
}

/**
 * @brief 直发原语：入帧池 + 立即提交发送（调用点直发）
 *
 * 入队成功后若 DMA 空闲，调用点直接弹帧提交发送（帧即取即还），
 * 不依赖 shell 线程泵。用于命令响应 / boot 早期同步段直发路径。
 *
 * @param data  发送内容（已格式化，可能含 ANSI 颜色）
 * @param len   数据长度
 * @param prio  优先级档位
 * @param epoch DBG 选择代数（非 DBG 传 0）
 */
static void QueueFrameAndSendNow(const char* data, int len, TxPriority prio, uint16_t epoch)
{
    unsigned key = irq_lock();            // 并发保护

    if (txq_.PushFrameLocked(data, len, prio, epoch, !k_is_in_isr()))    		  // 入帧池：截断 + 取帧 + 按档插队
    {
        pump_.PumpTxQueue(txq_, dbg_.ActiveEpoch());                     // DMA 空闲 → 调用点直发
    }

    irq_unlock(key);
}

/**
 * @brief 异步原语：入帧池 + give（shell 线程泵发送）
 *
 * @param data  发送内容（已格式化，可能含 ANSI 颜色）
 * @param len   数据长度
 * @param prio  优先级档位
 * @param epoch DBG 选择代数（非 DBG 传 0）
 */
static void QueueFrameForShellSend(const char* data, int len, TxPriority prio, uint16_t epoch)
{
    unsigned key = irq_lock();            // 并发保护

    if (txq_.PushFrameLocked(data, len, prio, epoch, !k_is_in_isr()))
    {
        if (!pump_.Sending() && pump_.Channel() != nullptr) k_sem_give(&pump_.Channel()->sem_);
    }

    irq_unlock(key);
}

/**
 * @brief 入队日志原始请求（异步段调用点，任务或 ISR 上下文）
 *
 * @param fmt     格式串（静态字面量）
 * @param channel 日志通道（取策略填 color/prio）
 * @param epoch   DBG 选择代数（非 DBG 传 0）
 * @param ansi    是否上 ANSI 颜色
 * @param args    参数槽
 * @param nargs   槽数
 */
static void QueueLogRecord(const char* fmt, LogChannel channel, uint16_t epoch, bool ansi, const uint32_t* args, uint8_t nargs)
{
    const LogChannelPolicy& policy = GetLogChannelPolicy(channel);
    unsigned key = irq_lock();                										// 并发保护
    (void)recq_.PushLogRecordLocked(fmt, channel, policy.color, policy.priority, epoch, ansi, args, nargs);
    irq_unlock(key);
}

/**
 * @brief 唤醒 shell 线程（若 DMA 空闲且通道已绑定）
 */
static void WakeShell()
{
    unsigned key = irq_lock();
    if (!pump_.Sending() && pump_.Channel() != nullptr) k_sem_give(&pump_.Channel()->sem_);
    irq_unlock(key);
}

/**
 * @brief 一次性日志（按通道策略决定颜色与前缀）
 * @param channel 日志通道
 * @param fmt     格式化串
 * @param ap      可变参数
 */
void Emit(LogChannel channel, const char* fmt, va_list ap)
{
    const LogChannelPolicy& policy = GetLogChannelPolicy(channel);

    if (!shell_own_)                                                        // 同步段（boot 早期）：调用点直发
    {
        char buf[kLogBufSize];
        vsnprintf(buf, sizeof(buf), fmt, ap);                               // picolibc 支持 %f（prj.conf 已开）

        char out[kLogBufSize + kColorOutExtra];
        int n = Wrap(policy, buf, out, sizeof(out));
        if (n > 0) QueueFrameAndSendNow(out, n, policy.priority, 0);
        return;
    }

    // 异步段（shell 接管后）：参数快照入队，调用点不格式化
    uint32_t args[kMaxLogArgs];
    uint8_t  nargs = 0;
    BuildRecordFromVaList(fmt, ap, args, &nargs);                           // 在 va_end 前快照

    QueueLogRecord(fmt, channel, 0, policy.ansi_color, args, nargs);        // 入原始请求队列
    WakeShell();                                                            // 唤醒 shell 去 ExpandOneRecord
}

/**
 * @brief 一次性日志，黑色
 */
void Info(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    Emit(LogChannel::Info, fmt, ap);
    va_end(ap);
}

/**
 * @brief 一次性日志，红色
 */
void Error(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    Emit(LogChannel::Error, fmt, ap);
    va_end(ap);
}

/**
 * @brief 一次性日志，绿色（状态正常）
 */
void Ok(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    Emit(LogChannel::Ok, fmt, ap);
    va_end(ap);
}

/**
 * @brief 一次性日志，橘色
 */
void Warning(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    Emit(LogChannel::Warning, fmt, ap);
    va_end(ap);
}

/**
 * @brief DBG 流式打印（仅当 e 是当前选中条目且通过分频才发）
 * @param e   DBG 条目（nullptr=池满静默）
 * @param fmt 格式化串
 */
void Debug(LogEntry* e, const char* fmt, ...)
{
    if (!shell_own_) return;                    // boot 早期无 DBG，直接返回
    if (e == nullptr) return;                   // 池满：静默丢弃
    if (e != dbg_.Active()) return;             // 未选中，静默
    if (!PassDivisor(e)) return;                // 分频：本次丢弃

    va_list ap;
    va_start(ap, fmt);

    uint32_t args[kMaxLogArgs];
    uint8_t  nargs = 0;
    BuildRecordFromVaList(fmt, ap, args, &nargs);
    va_end(ap);

    QueueLogRecord(fmt, LogChannel::Debug, dbg_.ActiveEpoch(), !e->vofa, args, nargs);
    WakeShell();
}

/**
 * @brief 格式化后直接写串口，不进队列、不受 log 过滤
 *
 * 用于 HAL/驱动层这种拿不到 Log 门面、又需要立即出日志的位置。
 * 走 Command 通道的策略（带 \r\n），与命令响应同一优先级。
 */
void Raw(const char* fmt, ...)
{
    char text[kLogBufSize];
    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);

    if (n <= 0) return;
    CommandLine(text);
}

/**
 * @brief 命令响应直发（不经过 log 过滤，带 \r\n，中档）
 * @param text 响应文本
 */
void CommandLine(const char* text)
{
    const LogChannelPolicy& policy = GetLogChannelPolicy(LogChannel::Command);
    char out[kLogBufSize + kLineOutExtra];

    int n = Wrap(policy, text, out, sizeof(out));
    if (n > 0) QueueFrameAndSendNow(out, n, policy.priority, 0);    // 命令响应：调用点直发
}

/**
 * @brief 处理一条原始请求（shell 线程）：出队 → 格式化 → 包装 → 入帧
 * @return true 处理了一条；false 队列为空
 */
bool ExpandOneRecord()
{
    unsigned key = irq_lock();
    LogRecord* r = recq_.PopNextRecordLocked(dbg_.ActiveEpoch());
    irq_unlock(key);

    if (r == nullptr) return false;

    char text[kLogBufSize];
    FormatRecordToText(*r, text, sizeof(text));                 // 记录展开为纯文本

    const LogChannelPolicy& policy = GetLogChannelPolicy(r->channel);
    char out[kLogBufSize + kColorOutExtra];

    int n = Wrap(policy, text, out, sizeof(out));
    if (n > 0) QueueFrameForShellSend(out, n, r->prio, r->epoch);

    key = irq_lock();
    recq_.ReleaseRecordLocked(r);
    irq_unlock(key);
    return true;
}

/**
 * @brief DMA 空闲则续发下一帧（shell 线程驱动）
 * 
 */
void SendNextFrameIfIdle()
{
    unsigned key = irq_lock();
    pump_.PumpTxQueue(txq_, dbg_.ActiveEpoch());   // 弹队头（作废帧跳过回收）→ 提交发送
    irq_unlock(key);
}

/**
 * @brief TX_DONE 回调（UartDma tx_cb）
 * 
 */
void HandleTxDone()
{
    pump_.HandleTxDone();
}

/**
 * @brief 按名字查 DBG 条目，首见创建
 * 
 * @param name 
 * @return LogEntry* 
 */
LogEntry* FindOrCreateDebugEntry(const char* name)
{
    return dbg_.FindOrCreate(name);
}

/**
 * @brief 选中 DBG 条目并设置分频周期
 * @param name    DBG 名字
 * @param divisor 分频周期（1=不分频；0 按 1 处理）
 * @return true 选中成功；false 条目不存在
 */
bool SelectDebugEntry(const char* name, uint16_t divisor)
{
    return dbg_.Select(name, divisor);             // 代数递增：在途旧 DBG 出队时作废
}

/**
 * @brief 取消选中（代数递增作废在途）
 * 
 */
void DeselectDebugEntry()
{
    dbg_.Deselect();
}

/**
 * @brief 设置 DBG 输出模式
 * @param name DBG 名字
 * @param on   true=VOFA+ 纯文本；false=上色
 * @return true 设置成功；false 条目不存在
 */
bool SetDebugVofa(const char* name, bool on)
{
    return dbg_.SetVofa(name, on);
}

/**
 * @brief 遍历所有已注册 DBG 条目输出（名字 + 选中状态 + 输出模式 + 分频）
 */
void PrintLogList()
{
    const LogEntry* active = dbg_.Active();
    const LogEntry* e = dbg_.First();

    if (e == nullptr) { CommandLine("(none)"); return; }

    for (; e != nullptr; e = dbg_.Next(e))
    {
        char line[160];
        snprintf(line, sizeof(line), "%-15s %-5s %-8s x%u", e->name, (e == active) ? "[ON]" : "[off]", e->vofa ? "[vofa]" : "[color]", (unsigned)atomic_get(&e->divisor));
        CommandLine(line);
    }
}

} // namespace shell::logproc
