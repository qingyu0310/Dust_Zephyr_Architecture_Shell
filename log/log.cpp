/**
 * @file log.cpp
 * @author qingyu
 * @brief [门面] DUST_LOG 门面实现 — 只做四层组装
 * @version 0.2
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "log.hpp"
#include "log_process.hpp"
#include "log_cmd.hpp"

#pragma message "Compiling Cmd/Shell/Log"

namespace shell {

/**
 * @brief 初始化日志系统：重置加工层持有的四层实例
 *
 * DBG 条目由 FindOrCreateDebugEntry 运行时创建，无链接段遍历。
 *
 * @return true 初始化成功
 */
bool Log::Init()
{
    return logproc::Init();
}

/**
 * @brief log 命令入口：转发到命令层
 * @param line 子命令参数（不含 "log" 前缀）
 */
void Log::ProcessLogCommand(uint8_t* line)
{
    logcmd::Process(line);
}

} // namespace shell
