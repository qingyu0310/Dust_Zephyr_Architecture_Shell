/**
 * @file log_cmd.cpp
 * @author qingyu
 * @brief [① 命令层] DUST_LOG 命令层实现 — log list/on/off/mode
 * @version 0.3
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "log_cmd.hpp"
#include "log_process.hpp"
#include "shell_cmd.hpp"
#include "token.hpp"
#include <cstdio>
#include <cstring>

#pragma message "Compiling Cmd/Shell/Log/Cmd"

namespace shell::logcmd {

/**
 * @brief log list：遍历所有已注册 DBG 条目输出
 * @param args 未使用
 */
static void CmdList(uint8_t* args)
{
    (void)args;
    logproc::PrintLogList();
}

/**
 * @brief log on <name> [N]：选中 DBG 条目，可选分频周期
 * @param args 剩余行（"<name> [N]"）
 */
static void CmdOn(uint8_t* args)
{
    const char* name = nullptr;
    uint8_t* rest = NextToken(args, name);

    if (*name == '\0') {
        logproc::CommandLine("usage: log on <name> [divisor]");
        return;
    }

    // 可选分频：手写十进制解析（picolibc strtoul 多位数有 bug）
    uint16_t divisor = 1;
    if (*rest != '\0')
    {
        uint32_t v = 0;
        for (const uint8_t* p = rest; *p >= '0' && *p <= '9'; ++p)
        {
            v = v * 10u + static_cast<uint32_t>(*p - '0');
            if (v > 0xFFFFu) { v = 0xFFFFu; break; }
        }
        divisor = (v == 0) ? 1 : static_cast<uint16_t>(v);   // 0 或非数字 → 不分频
    }

    if (logproc::SelectDebugEntry(name, divisor))
    {
        char ack[32];
        snprintf(ack, sizeof(ack), "log on: ok x%u", (unsigned)divisor);
        logproc::CommandLine(ack);
    }
    else
    {
        logproc::CommandLine("log on: not found");
    }
}

/**
 * @brief log off：取消选中（代数递增作废在途）
 * @param args 未使用
 */
static void CmdOff(uint8_t* args)
{
    (void)args;
    logproc::DeselectDebugEntry();
    logproc::CommandLine("log off: ok");
}

/**
 * @brief log mode <name> on|off：设置输出模式（on/vofa=纯文本，off/color=上色）
 * @param args 剩余行（"<name> <on|off>"）
 */
static void CmdMode(uint8_t* args)
{
    const char* name = nullptr;
    uint8_t* rest = NextToken(args, name);

    if (*name == '\0') {
        logproc::CommandLine("log mode <name> on|off");
        return;
    }

    const char* mode = reinterpret_cast<const char*>(rest);

    if (std::strcmp(mode, "on") == 0 || std::strcmp(mode, "vofa") == 0)
    {
        logproc::CommandLine(logproc::SetDebugVofa(name, true) ? "log mode: vofa" : "log mode: not found");
    }
    else if (std::strcmp(mode, "off") == 0 || std::strcmp(mode, "color") == 0)
    {
        logproc::CommandLine(logproc::SetDebugVofa(name, false) ? "log mode: color" : "log mode: not found");
    }
    else
    {
        logproc::CommandLine("log mode <name> on|off");
    }
}

/**
 * @brief 未命中子命令：打印 usage
 * @param args 未使用
 */
static void CmdUsage(uint8_t* args)
{
    (void)args;
    logproc::CommandLine("log list|on <name> [N]|off|mode <name> on|off");
}

/**
 * @brief log 命令入口（词法解析 + 查表匹配）
 * @param line 子命令参数（不含 "log" 前缀）
 */
void Process(uint8_t* line)
{
    static constexpr SubCmdEntry kSubCmds[] = {
        { "list", CmdList  },
        { "on",   CmdOn    },
        { "off",  CmdOff   },
        { "mode", CmdMode  },
    };

    DispatchSubCmd(line, kSubCmds, sizeof(kSubCmds) / sizeof(kSubCmds[0]), &CmdUsage);
}

} // namespace shell::logcmd

REGISTER_SHELL_CMD("log", "log list|on <name> [N]|off|mode", ::shell::logcmd::Process);
