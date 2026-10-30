/**
 * @file log.hpp
 * @author qingyu
 * @brief [门面] DUST_LOG 组装层 — 四层串接 + 对外宏与装配入口
 * @version 1.3
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 * # 四层结构
 *
 * ```text
 * ① 命令层  cmd/       log_cmd.hpp/.cpp     词法解析 + 查表匹配 + 子命令处理
 * ② 加工层  process/   log_process.hpp/.cpp 日志入口 / 分频 / 格式化 / 上色 / 帧包装
 * ③ 队列层  queue/     log_queue.hpp        记录队列（原始请求）+ 帧队列（三档优先级）
 * ④ 发送层  transport/ log_transport.hpp    DMA 泵（Stream 绑定 / 空闲续发 / TX_DONE）
 * ```
 *
 * **单向流水线**：① → ② → ③ → ④，禁止跳层、反向、同层互调。
 * ②加工层是唯一持有四层实例（`dbg_`/`pump_`/`txq_`/`recq_`）的地方，
 * 因此命令层拿不到实例，越权无从下手。
 *
 * # 本文件职责
 *
 * 只做两件事：
 * 1. 定义对外宏（`DUST_LOG_INF` / `DUST_LOG_DBG` …），宏直接落到加工层；
 * 2. 提供 `Log` 类，把 shell 线程需要的装配与驱动入口转发给加工层。
 *
 * **不含任何实现**——实现分别在 `log.cpp`（仅组装）、`cmd/`、`process/`、`queue/`、`transport/`。
 *
 * # 一次日志的流向
 *
 * ```text
 * DUST_LOG_INF("x=%d", 1)
 *   → logproc::Info  → Emit(Info, ...)
 *       ├ shell_own_ == false（boot 早期）：vsnprintf → Wrap → txq_ → 立即 pump
 *       └ shell_own_ == true （运行期）  ：参数快照 → recq_ → 唤醒 shell
 *                                          → ExpandOneRecord → FormatRecordToText
 *                                          → Wrap → txq_ → pump
 *
 * DUST_LOG_DBG("imu", ...)  多一步：PassDivisor 分频过滤（每 N 条放行 1 条）
 * ```
 */

#pragma once

#ifdef CONFIG_DUST_CMD_SHELL_LOG

#include <cstdint>
#include <zephyr/kernel.h>
#include "stream.hpp"
#include "log_process.hpp" // ② 加工层：四层实例 + 全部日志入口

namespace shell {

/**
 * @brief DUST_LOG 门面（纯转发，无状态）
 *
 * 一次性四色日志（INF/ERR/OK/WRN）调用即打印，无名字无选择；
 * DBG 流式日志带名字、默认静默，`log on <name> [N]` 选中后按分频打印。
 * 所有发送统一走三档优先级帧队列（事件 > 命令响应 > DBG），帧池 8×128B。
 *
 * 容量（实测 2026-08-06）：8 帧池 → 同一时间最多连发 9 条（第 10 条起丢）；
 * 稳态平均速率 < 92160 B/s（921600 波特）时无限闭环。
 */
class Log
{
public:
    /**
     * @brief 初始化：重置加工层持有的四层实例
     * @return true 初始化成功
     */
    static bool Init();

    /**
     * @brief 绑定发送通道（shell thread_init 调用）
     * @param s 已初始化的流（通常是 shell-uart）
     */
    static void BindOutputStream(Stream* s)  { logproc::BindOutputStream(s); }

    /**
     * @brief 标记 shell 线程已接管发送
     *
     * 之后日志走「参数快照入队 → shell 线程展开」的异步路径；
     * 之前走「vsnprintf + 调用点直发」的同步路径。
     */
    static void MarkShellThreadReady()       { logproc::MarkShellThreadReady(); }

    /**
     * @brief DMA 空闲则续发下一帧（shell 线程每轮驱动）
     */
    static void SendNextFrameIfIdle()        { logproc::SendNextFrameIfIdle(); }

    /**
     * @brief TX_DONE 回调（注册为 UartDma 的 tx_cb）
     */
    static void HandleTxDone()               { logproc::HandleTxDone(); }

    /**
     * @brief 命令响应直发（不经过 log 过滤，带 \r\n，中档优先级）
     * @param text 响应文本
     */
    static void SendCommandLine(const char* text) { logproc::CommandLine(text); }

    /**
     * @brief 处理一条原始请求（shell 线程循环调用）
     * @return true 处理了一条；false 队列为空
     */
    static bool FormatOnePendingRecord()     { return logproc::ExpandOneRecord(); }

    /**
     * @brief log 命令入口（转发到 ① 命令层）
     * @param line 子命令参数（不含 "log" 前缀）
     */
    static void ProcessLogCommand(uint8_t* line);
};

} // namespace shell

// ========== 开：真实实现（宏直接落到 ② 加工层，不经门面，避免可变参数二次转发）==========

// DBG：流式调试日志（默认静默，log on <name> [N] 选中后按分频打印）
// 名字是字符串字面量，运行时 FindOrCreateDebugEntry 创建/复用条目，无需 DEFINE/链接段
#define DUST_LOG_DBG(name_, ...) ::shell::logproc::Debug(::shell::logproc::FindOrCreateDebugEntry(name_), ##__VA_ARGS__)

// 一次性四色（调用即打，无名字，用法与 LOG_INF 一致；[等级] 前缀由通道策略补）
// 每帧字节开销（TrueColor 38;2;R;G;B）：[inf]前缀(5) + 颜色(最长18 \x1b[38;2;246;167;83m) + 复位 \x1b[0m(4) + \r\n(2) = 最长29B；
// 帧上限 127B（kTxMaxLen 截断）→ 内容建议 ≤98B，超出截尾
#define DUST_LOG_INF(...) ::shell::logproc::Info(__VA_ARGS__)
#define DUST_LOG_ERR(...) ::shell::logproc::Error(__VA_ARGS__)
#define DUST_LOG_OK(...)  ::shell::logproc::Ok(__VA_ARGS__)
#define DUST_LOG_WRN(...) ::shell::logproc::Warning(__VA_ARGS__)

// 直发：格式化后直接写串口，不进队列、不受 log 过滤
#define DUST_LOG_RAW(...) ::shell::logproc::Raw(__VA_ARGS__)

#else

// ========== 关：空宏（调用点零开销，编译不报错）==========

#define DUST_LOG_DBG(...)
#define DUST_LOG_INF(...)
#define DUST_LOG_ERR(...)
#define DUST_LOG_OK(...)
#define DUST_LOG_WRN(...)
#define DUST_LOG_RAW(...)

#endif
