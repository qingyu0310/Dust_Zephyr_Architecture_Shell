/**
 * @file var.cpp
 * @author qingyu
 * @brief var 命令实现 — var list/get/set（输出走 DUST_LOG 命令响应）
 * @version 0.4
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "var.hpp"
#include "log.hpp"
#include "shell_cmd.hpp"
#include "token.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma message "Compiling Cmd/Shell/Var"

extern const shell::Entry __shell_var_start[];
extern const shell::Entry __shell_var_end[];

namespace shell {

/**
 * @brief 遍历链接段查找匹配名称的注册条目
 * @param name 变量名称字符串
 * @return 匹配的条目指针，未匹配返回 nullptr
 */
const Entry *Var::Find(const char *name)
{
    size_t n = __shell_var_end - __shell_var_start;

    for (size_t i = 0; i < n; i++)
    {
        const Entry *e = &__shell_var_start[i];

        if (std::strcmp(e->name, name) == 0) return e;
    }

    return nullptr;
}

/**
 * @brief 输出变量名、类型和当前值
 * @param e 待输出的条目
 */
void Var::PrintVar(const Entry &e)
{
    switch (e.type)
    {
        case VarType::Uint8:   DUST_LOG_RAW("  %s (u8)    = %u",   e.name, *static_cast<const uint8_t*>(e.ptr));   break;
        case VarType::Int8:    DUST_LOG_RAW("  %s (i8)    = %d",   e.name, *static_cast<const int8_t*>(e.ptr));    break;
        case VarType::Uint16:  DUST_LOG_RAW("  %s (u16)   = %u",   e.name, *static_cast<const uint16_t*>(e.ptr));  break;
        case VarType::Int16:   DUST_LOG_RAW("  %s (i16)   = %d",   e.name, *static_cast<const int16_t*>(e.ptr));   break;
        case VarType::Uint32:  DUST_LOG_RAW("  %s (u32)   = %u",   e.name, *static_cast<const uint32_t*>(e.ptr));  break;
        case VarType::Int32:   DUST_LOG_RAW("  %s (i32)   = %d",   e.name, *static_cast<const int32_t*>(e.ptr));   break;
        case VarType::Uint64:  DUST_LOG_RAW("  %s (u64)   = %llu", e.name, *static_cast<const uint64_t*>(e.ptr));  break;
        case VarType::Int64:   DUST_LOG_RAW("  %s (i64)   = %lld", e.name, *static_cast<const int64_t*>(e.ptr));   break;
        case VarType::Float:   DUST_LOG_RAW("  %s (float) = %f",   e.name, static_cast<double>(*static_cast<const float*>(e.ptr)));   break;
        case VarType::Double:  DUST_LOG_RAW("  %s (double)= %lf",  e.name, *static_cast<const double*>(e.ptr));    break;
        case VarType::Bool:    DUST_LOG_RAW("  %s (bool)  = %s",   e.name, *static_cast<const bool*>(e.ptr) ? "true" : "false");       break;
    }
}

/**
 * @brief 输出变量名和值（单行）
 * @param e 待输出的条目
 */
void Var::PrintValueOnly(const Entry &e)
{
    switch (e.type)
    {
        case VarType::Uint8:   DUST_LOG_RAW("%s = %u",   e.name, *static_cast<const uint8_t*>(e.ptr));   break;
        case VarType::Int8:    DUST_LOG_RAW("%s = %d",   e.name, *static_cast<const int8_t*>(e.ptr));    break;
        case VarType::Uint16:  DUST_LOG_RAW("%s = %u",   e.name, *static_cast<const uint16_t*>(e.ptr));  break;
        case VarType::Int16:   DUST_LOG_RAW("%s = %d",   e.name, *static_cast<const int16_t*>(e.ptr));   break;
        case VarType::Uint32:  DUST_LOG_RAW("%s = %u",   e.name, *static_cast<const uint32_t*>(e.ptr));  break;
        case VarType::Int32:   DUST_LOG_RAW("%s = %d",   e.name, *static_cast<const int32_t*>(e.ptr));   break;
        case VarType::Uint64:  DUST_LOG_RAW("%s = %llu", e.name, *static_cast<const uint64_t*>(e.ptr));  break;
        case VarType::Int64:   DUST_LOG_RAW("%s = %lld", e.name, *static_cast<const int64_t*>(e.ptr));   break;
        case VarType::Float:   DUST_LOG_RAW("%s = %f",   e.name, static_cast<double>(*static_cast<const float*>(e.ptr)));   break;
        case VarType::Double:  DUST_LOG_RAW("%s = %lf",  e.name, *static_cast<const double*>(e.ptr));    break;
        case VarType::Bool:    DUST_LOG_RAW("%s = %s",   e.name, *static_cast<const bool*>(e.ptr) ? "true" : "false");       break;
    }
}

/**
 * @brief 遍历链接段，输出所有注册变量
 */
void Var::CmdList()
{
    size_t n = __shell_var_end - __shell_var_start;

    if (n == 0) { DUST_LOG_RAW("(none)"); return; }

    for (size_t i = 0; i < n; i++)
    {
        PrintVar(__shell_var_start[i]);
    }

    DUST_LOG_RAW("--- %zu variables ---", n);
}

/**
 * @brief 按名称查找并输出变量值
 * @param name 变量名称
 */
void Var::CmdGet(const char *name)
{
    const Entry *e = Find(name);

    if (e == nullptr)
    {
        DUST_LOG_RAW("not found: %s", name);
        return;
    }

    PrintValueOnly(*e);
}

/**
 * @brief 按类型解析值字符串并写入变量
 * @param name 变量名称
 * @param val  值字符串（整数/浮点数/true/false）
 */
void Var::CmdSet(const char *name, const char *val)
{
    const Entry *e = Find(name);

    if (e == nullptr)
    {
        DUST_LOG_RAW("not found: %s", name);
        return;
    }

    switch (e->type)
    {
        case VarType::Uint8:
        case VarType::Uint16:
        case VarType::Uint32:
        case VarType::Uint64:
        {
            char *end = nullptr;
            unsigned long long v = std::strtoull(val, &end, 0);
            if (*end != '\0') { DUST_LOG_RAW("bad value"); return; }
            switch (e->type)
            {
                case VarType::Uint8:   *static_cast<uint8_t*> (e->ptr) = static_cast<uint8_t> (v); break;
                case VarType::Uint16:  *static_cast<uint16_t*>(e->ptr) = static_cast<uint16_t>(v); break;
                case VarType::Uint32:  *static_cast<uint32_t*>(e->ptr) = static_cast<uint32_t>(v); break;
                case VarType::Uint64:  *static_cast<uint64_t*>(e->ptr) = v; break;
                default: break;
            }
            break;
        }
        case VarType::Int8:
        case VarType::Int16:
        case VarType::Int32:
        case VarType::Int64:
        {
            char *end = nullptr;
            long long v = std::strtoll(val, &end, 0);
            if (*end != '\0') { DUST_LOG_RAW("bad value"); return; }
            switch (e->type)
            {
                case VarType::Int8:   *static_cast<int8_t*> (e->ptr) = static_cast<int8_t> (v); break;
                case VarType::Int16:  *static_cast<int16_t*>(e->ptr) = static_cast<int16_t>(v); break;
                case VarType::Int32:  *static_cast<int32_t*>(e->ptr) = static_cast<int32_t>(v); break;
                case VarType::Int64:  *static_cast<int64_t*>(e->ptr) = v; break;
                default: break;
            }
            break;
        }
        case VarType::Float:
        {
            char *end = nullptr;
            float v = std::strtof(val, &end);
            if (*end != '\0') { DUST_LOG_RAW("bad value"); return; }
            *static_cast<float*>(e->ptr) = v;
            break;
        }
        case VarType::Double:
        {
            char *end = nullptr;
            double v = std::strtod(val, &end);
            if (*end != '\0') { DUST_LOG_RAW("bad value"); return; }
            *static_cast<double*>(e->ptr) = v;
            break;
        }
        case VarType::Bool:
        {
            bool v;
            if (std::strcmp(val, "true") == 0 || std::strcmp(val, "1") == 0)
                v = true;
            else if (std::strcmp(val, "false") == 0 || std::strcmp(val, "0") == 0)
                v = false;
            else { DUST_LOG_RAW("bad value"); return; }
            *static_cast<bool*>(e->ptr) = v;
            break;
        }
    }

    DUST_LOG_RAW("ok");
}

/**
 * @brief var 命令入口（var list/get/set）
 * @param line 子命令参数（不含 "var" 前缀）
 */
void Var::Process(uint8_t *line)
{
    static constexpr SubCmdEntry kSubCmds[] = {
        { "list", Var::CmdListSub },
        { "get",  Var::CmdGetSub  },
        { "set",  Var::CmdSetSub  },
    };

    DispatchSubCmd(line, kSubCmds, sizeof(kSubCmds) / sizeof(kSubCmds[0]), &Var::CmdUsage);
}

/**
 * @brief var list：遍历 .shell_var 段输出
 * @param args 未使用
 */
void Var::CmdListSub(uint8_t* args)
{
    (void)args;
    CmdList();
}

/**
 * @brief var get <name>：输出单变量值
 * @param args 剩余行（"<name>"）
 */
void Var::CmdGetSub(uint8_t* args)
{
    const char* name = nullptr;
    NextToken(args, name);

    if (*name == '\0') {
        DUST_LOG_RAW("usage: var get <name>");
        return;
    }

    CmdGet(name);
}

/**
 * @brief var set <name> <val>：解析并写值
 * @param args 剩余行（"<name> <val>"）
 */
void Var::CmdSetSub(uint8_t* args)
{
    const char* name = nullptr;
    uint8_t* rest = NextToken(args, name);

    if (*name == '\0' || *rest == '\0') {
        DUST_LOG_RAW("usage: var set <name> <value>");
        return;
    }

    CmdSet(name, reinterpret_cast<const char*>(rest));
}

/**
 * @brief 未命中子命令：打印 usage
 * @param args 未使用
 */
void Var::CmdUsage(uint8_t* args)
{
    (void)args;
    DUST_LOG_RAW("var list|get <name>|set <name> <val>");
}

REGISTER_SHELL_CMD("var", "var list|get|set", Var::Process);

} // namespace shell
