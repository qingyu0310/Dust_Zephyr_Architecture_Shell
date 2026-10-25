/**
 * @file thread_cmd.hpp
 * @author qingyu
 * @brief thread 命令 — 线程运行统计查看与管理
 * @version 0.3
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#ifdef CONFIG_DUST_CMD_SHELL

#include <cstdint>

namespace shell {

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

    static void CmdListSub(uint8_t* args);     // 分派入口：thread list
    static void CmdShowSub(uint8_t* args);     // 分派入口：thread show <name>
    static void CmdPauseSub(uint8_t* args);    // 分派入口：thread pause <name>
    static void CmdResumeSub(uint8_t* args);   // 分派入口：thread resume <name>
    static void CmdExitSub(uint8_t* args);     // 分派入口：thread exit <name>
    static void CmdResetSub(uint8_t* args);    // 分派入口：thread reset <name>
    static void CmdUsage(uint8_t* args);       // 未命中子命令：打印 usage
};

} // namespace shell

#endif
