/**
 * @file thread_cmd.hpp
 * @author qingyu
 * @brief thread 命令 — 线程运行统计查看与管理
 * @version 0.2
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#ifdef CONFIG_DUST_CMD_SHELL

#include <cstdint>

namespace debug {

/**
 * @brief thread 命令（list/show/pause/resume/exit/reset）
 *
 * 数据来源 thread_registry：线程在 Start 时登记、Exit 时移除，
 * 因此 list 只反映存活线程，已退出的不再出现。
 */
class ThreadCmd final
{
public:
    static void Process(uint8_t* line);        // thread 命令入口

private:
    static void CmdList();                     // thread list：遍历注册表输出统计
    static void CmdShow(const char* name);     // thread show：单线程详细统计
    static void CmdPause(const char* name);    // thread pause：暂停线程
    static void CmdResume(const char* name);   // thread resume：恢复线程
    static void CmdExit(const char* name);     // thread exit：终止线程
    static void CmdReset(const char* name);    // thread reset：清零计数器
};

} // namespace debug

#endif
