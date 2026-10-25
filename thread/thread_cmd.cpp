/**
 * @file thread_cmd.cpp
 * @author qingyu
 * @brief thread 命令实现 — list/show/pause/resume/reset（输出走 DUST_LOG 命令响应）
 * @version 0.5
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "thread_cmd.hpp"
#include "thread.hpp"
#include "thread_registry.hpp"
#include "shell_cmd.hpp"
#include "token.hpp"
#include "log.hpp"
#include <cstdio>
#include <cstring>

#pragma message "Compiling Cmd/Shell/Thread_cmd"

namespace shell {

/**
 * @brief 线程状态字符串
 * @param s 统计快照
 * @return 固定字符串
 */
static const char* StateName(const ThreadStats& s)
{
    if (!s.running) return "dead";
    if (s.paused)   return "paused";
    return "running";
}

/**
 * @brief thread list：遍历注册表，每个线程一行
 *
 * 已退出的线程会从注册表移除，不会出现在列表里。
 */
void ThreadCmd::CmdList()
{
    Log::SendCommandLine("THREAD          STATE     RUN      RETRY   DROP    STACK");

    const uint8_t count = thread_registry::Count();
    for (uint8_t i = 0; i < count; ++i)
    {
        ThreadBase* t = thread_registry::At(i);
        if (t == nullptr) continue;

        ThreadStats s {};
        if (!t->GetStats(s)) continue;

        char line[128];
        snprintf(line, sizeof(line), "%-15s %-9s %-8u %-7u %-7u %u", s.name, StateName(s), (unsigned)s.run_count, (unsigned)s.retry_count, (unsigned)s.drop_count, (unsigned)s.stack_unused);
        Log::SendCommandLine(line);
    }

    char tail[32];
    snprintf(tail, sizeof(tail), "--- %u threads ---", (unsigned)count);
    Log::SendCommandLine(tail);
}

/**
 * @brief thread show：单线程详细统计
 * @param name 线程名
 */
void ThreadCmd::CmdShow(const char* name)
{
    if (name == nullptr || *name == '\0') {
        Log::SendCommandLine("usage: thread show <name>");
        return;
    }

    ThreadBase* t = thread_registry::Find(name);
    if (t == nullptr) {
        Log::SendCommandLine("thread show: not found");
        return;
    }

    ThreadStats s {};
    if (!t->GetStats(s)) {
        Log::SendCommandLine("thread show: not started");
        return;
    }

    char line[128];

    snprintf(line, sizeof(line), "%s:", s.name);
    Log::SendCommandLine(line);

    snprintf(line, sizeof(line), "  state        %s", StateName(s));
    Log::SendCommandLine(line);

    snprintf(line, sizeof(line), "  run          %u", (unsigned)s.run_count);
    Log::SendCommandLine(line);

    snprintf(line, sizeof(line), "  retry        %u", (unsigned)s.retry_count);
    Log::SendCommandLine(line);

    snprintf(line, sizeof(line), "  drop         %u", (unsigned)s.drop_count);
    Log::SendCommandLine(line);

    snprintf(line, sizeof(line), "  last         %u.%03u ms",
             (unsigned)(s.last_run_ns / 1000000ULL),
             (unsigned)((s.last_run_ns / 1000ULL) % 1000ULL));
    Log::SendCommandLine(line);

    snprintf(line, sizeof(line), "  max          %u.%03u ms",
             (unsigned)(s.max_run_ns / 1000000ULL),
             (unsigned)((s.max_run_ns / 1000ULL) % 1000ULL));
    Log::SendCommandLine(line);

    snprintf(line, sizeof(line), "  total        %u.%03u ms",
             (unsigned)(s.run_ns / 1000000ULL),
             (unsigned)((s.run_ns / 1000ULL) % 1000ULL));
    Log::SendCommandLine(line);

    snprintf(line, sizeof(line), "  stack        %u B unused", (unsigned)s.stack_unused);
    Log::SendCommandLine(line);
}

/**
 * @brief thread pause：暂停线程（保留全部现场）
 * @param name 线程名
 */
void ThreadCmd::CmdPause(const char* name)
{
    if (name == nullptr || *name == '\0') {
        Log::SendCommandLine("usage: thread pause <name>");
        return;
    }

    ThreadBase* t = thread_registry::Find(name);
    if (t == nullptr) {
        Log::SendCommandLine("thread pause: not found");
        return;
    }

    t->Pause();
    Log::SendCommandLine("thread pause: ok");
}

/**
 * @brief thread resume：恢复被暂停的线程
 * @param name 线程名
 */
void ThreadCmd::CmdResume(const char* name)
{
    if (name == nullptr || *name == '\0') {
        Log::SendCommandLine("usage: thread resume <name>");
        return;
    }

    ThreadBase* t = thread_registry::Find(name);
    if (t == nullptr) {
        Log::SendCommandLine("thread resume: not found");
        return;
    }

    t->Resume();
    Log::SendCommandLine("thread resume: ok");
}

/**
 * @brief thread exit：终止线程（走 ExitHook → 注销注册表 → 打日志 → k_thread_abort）
 *
 * 带保留标记的线程（shell）拒绝终止。
 *
 * @param name 线程名
 */
void ThreadCmd::CmdExit(const char* name)
{
    if (name == nullptr || *name == '\0') {
        Log::SendCommandLine("usage: thread exit <name>");
        return;
    }

    ThreadBase* t = thread_registry::Find(name);
    if (t == nullptr) {
        Log::SendCommandLine("thread exit: not found");
        return;
    }

    if (t->KeepAlive()) {
        Log::SendCommandLine("thread exit: reserved, refused");
        return;
    }

    t->Exit();
    Log::SendCommandLine("thread exit: ok");
}

/**
 * @brief thread reset：清零线程计数器
 * @param name 线程名
 */
void ThreadCmd::CmdReset(const char* name)
{
    if (name == nullptr || *name == '\0') {
        Log::SendCommandLine("usage: thread reset <name>");
        return;
    }

    ThreadBase* t = thread_registry::Find(name);
    if (t == nullptr) {
        Log::SendCommandLine("thread reset: not found");
        return;
    }

    t->ResetStats();
    Log::SendCommandLine("thread reset: ok");
}

/**
 * @brief thread 命令入口
 * @param line 子命令参数（不含 "thread" 前缀）
 */
void ThreadCmd::Process(uint8_t* line)
{
    static constexpr SubCmdEntry kSubCmds[] = {
        { "list",   ThreadCmd::CmdListSub   },
        { "show",   ThreadCmd::CmdShowSub   },
        { "pause",  ThreadCmd::CmdPauseSub  },
        { "resume", ThreadCmd::CmdResumeSub },
        { "exit",   ThreadCmd::CmdExitSub   },
        { "reset",  ThreadCmd::CmdResetSub  },
    };

    DispatchSubCmd(line, kSubCmds, sizeof(kSubCmds) / sizeof(kSubCmds[0]), &ThreadCmd::CmdUsage);
}

/**
 * @brief thread list：遍历注册表，每个线程一行
 * @param args 未使用
 */
void ThreadCmd::CmdListSub(uint8_t* args)
{
    (void)args;
    CmdList();
}

/**
 * @brief thread show <name>：单线程详细统计
 * @param args 剩余行（"<name>"）
 */
void ThreadCmd::CmdShowSub(uint8_t* args)
{
    CmdShow(reinterpret_cast<const char*>(args));
}

/**
 * @brief thread pause <name>：暂停线程
 * @param args 剩余行（"<name>"）
 */
void ThreadCmd::CmdPauseSub(uint8_t* args)
{
    CmdPause(reinterpret_cast<const char*>(args));
}

/**
 * @brief thread resume <name>：恢复线程
 * @param args 剩余行（"<name>"）
 */
void ThreadCmd::CmdResumeSub(uint8_t* args)
{
    CmdResume(reinterpret_cast<const char*>(args));
}

/**
 * @brief thread exit <name>：终止线程
 * @param args 剩余行（"<name>"）
 */
void ThreadCmd::CmdExitSub(uint8_t* args)
{
    CmdExit(reinterpret_cast<const char*>(args));
}

/**
 * @brief thread reset <name>：清零线程计数器
 * @param args 剩余行（"<name>"）
 */
void ThreadCmd::CmdResetSub(uint8_t* args)
{
    CmdReset(reinterpret_cast<const char*>(args));
}

/**
 * @brief 未命中子命令：打印 usage
 * @param args 未使用
 */
void ThreadCmd::CmdUsage(uint8_t* args)
{
    (void)args;
    Log::SendCommandLine("thread list|show|pause|resume|exit|reset <name>");
}

REGISTER_SHELL_CMD("thread", "thread list|show|pause|resume|exit|reset", ThreadCmd::Process, thread);

} // namespace shell
