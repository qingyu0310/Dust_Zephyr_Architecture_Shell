/**
 * @file log_process.hpp
 * @author qingyu
 * @brief [② 加工层] DUST_LOG 加工 — 日志入口 / 分频过滤 / 格式化 / 上色 / 帧包装
 * @version 0.2
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 * # 在四层里的位置
 *
 * ```text
 * ① 命令层 cmd/log_cmd      词法解析 + 查表
 * ② 加工层 process/         ← 本文件
 * ③ 队列层 queue/            记录队列 + 帧队列
 * ④ 发送层 transport/        DMA 泵
 * ```
 *
 * 加工层是**唯一持有四层实例**的地方（`dbg_` / `pump_` / `txq_` / `recq_` / `shell_own_`），
 * 也是「一条日志变成可发送文本」的全部工作所在：
 *
 * - 取通道策略（颜色 / 前缀 / 优先级）
 * - 分频过滤（DBG，`PassDivisor`）
 * - 同步段格式化 + 上色 + 帧包装（`Emit` 的 shell_own_ == false 分支）
 * - 异步段参数快照 + 唤醒 shell 线程（`Emit` 的 shell_own_ == true 分支）
 * - shell 线程侧展开一条记录（`ExpandOneRecord`）
 *
 * **禁用的依赖**：不许 include `log_cmd.hpp`（命令层是上层）。
 */

#pragma once

#ifdef CONFIG_DUST_CMD_SHELL_LOG

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <zephyr/kernel.h>
#include "stream.hpp"
#include "log_policy.hpp"
#include "log_debug.hpp"


namespace shell::logproc {

/**
 * @brief 初始化加工层持有的四层实例
 *
 * 清 DBG 注册表、发送泵、帧队列、记录队列。
 *
 * @return true 初始化成功
 */
bool Init();

/**
 * @brief 绑定发送通道（shell thread_init 调用）
 * @param s 已初始化的流（通常是 shell-uart）
 */
void BindOutputStream(Stream* s);

/**
 * @brief 标记 shell 线程已接管发送
 *
 * 之后日志走「参数快照入队 → shell 线程展开」的异步路径；
 * 之前走「vsnprintf + 调用点直发」的同步路径。
 */
void MarkShellThreadReady();

// ========== 对外日志入口（宏的目标）==========

/**
 * @brief 一次性日志（按通道策略决定颜色与前缀）
 * @param channel 日志通道
 * @param fmt     格式化串
 * @param ap      可变参数
 */
void Emit(LogChannel channel, const char* fmt, va_list ap);

/**
 * @brief 一次性日志，黑色
 * @param fmt 格式化串
 */
void Info(const char* fmt, ...);

/**
 * @brief 一次性日志，红色
 * @param fmt 格式化串
 */
void Error(const char* fmt, ...);

/**
 * @brief 一次性日志，绿色（状态正常）
 * @param fmt 格式化串
 */
void Ok(const char* fmt, ...);

/**
 * @brief 一次性日志，橘色
 * @param fmt 格式化串
 */
void Warning(const char* fmt, ...);

/**
 * @brief DBG 流式打印（仅当 e 是当前选中条目且通过分频才发）
 * @param e   DBG 条目（FindOrCreateDebugEntry 返回值，nullptr=池满静默）
 * @param fmt 格式化串
 */
void Debug(LogEntry* e, const char* fmt, ...);

/**
 * @brief 命令响应直发（不经过 log 过滤，带 \r\n，中档优先级）
 * @param text 响应文本
 */
void CommandLine(const char* text);

// ========== 加工原语 ==========

/**
 * @brief 分频过滤：本条 DBG 是否放行
 *
 * divisor<=1 时直接放行（短路，不碰 quota）；
 * 否则递减配额，配额用尽则放行并重填。
 *
 * @param e DBG 条目
 * @return true=放行；false=本次丢弃
 */
bool PassDivisor(LogEntry* e);

/**
 * @brief 帧包装：前缀 + 上色 + \r\n
 * @param policy 通道策略（取 prefix / color / ansi_color）
 * @param text   待包装的纯文本
 * @param out    输出缓冲
 * @param cap    输出缓冲容量
 * @return 写入字节数；<=0 表示失败
 */
int Wrap(const LogChannelPolicy& policy, const char* text, char* out, size_t cap);

/**
 * @brief 处理一条原始请求（shell 线程）：出队 → 格式化 → 包装 → 入帧
 * @return true 处理了一条；false 队列为空
 */
bool ExpandOneRecord();

// ========== 向命令层暴露的 DBG 操作 ==========

/**
 * @brief 按名字查 DBG 条目，首见创建（同名复用同一条目）
 * @param name DBG 名字（字符串字面量）
 * @return 条目指针；池满返回 nullptr
 */
LogEntry* FindOrCreateDebugEntry(const char* name);

/**
 * @brief 选中 DBG 条目并设置分频周期
 * @param name    DBG 名字
 * @param divisor 分频周期（1=不分频；0 按 1 处理）
 * @return true 选中成功；false 条目不存在
 */
bool SelectDebugEntry(const char* name, uint16_t divisor = 1);

/**
 * @brief 取消选中（代数递增作废在途）
 */
void DeselectDebugEntry();

/**
 * @brief 设置 DBG 输出模式
 * @param name DBG 名字
 * @param on   true=VOFA+ 纯文本；false=上色
 * @return true 设置成功；false 条目不存在
 */
bool SetDebugVofa(const char* name, bool on);

/**
 * @brief 遍历所有已注册 DBG 条目输出（名字 + 选中状态 + 模式 + 分频）
 */
void PrintLogList();

// ========== 发送层钩子（shell.cpp 直接调）==========

/**
 * @brief DMA 空闲则续发下一帧（shell 线程驱动）
 */
void SendNextFrameIfIdle();

/**
 * @brief TX_DONE 回调（UartDma tx_cb）：清标志并唤醒 shell 线程
 */
void HandleTxDone();

} // namespace shell::logproc

#endif // CONFIG_DUST_CMD_SHELL_LOG
