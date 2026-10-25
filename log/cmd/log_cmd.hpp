/**
 * @file log_cmd.hpp
 * @author qingyu
 * @brief [① 命令层] DUST_LOG 命令层 — log 子命令词法解析、查表匹配与处理
 * @version 0.2
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#ifdef CONFIG_DUST_CMD_SHELL_LOG

#include <cstdint>

namespace shell::logcmd {

/**
 * @brief log 命令入口（词法解析 + 查表匹配）
 * @param line 子命令参数（不含 "log" 前缀）
 */
void Process(uint8_t* line);

} // namespace shell::logcmd

#endif // CONFIG_DUST_CMD_SHELL_LOG
