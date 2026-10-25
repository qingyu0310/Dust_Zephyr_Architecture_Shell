# shell 层：控制台底座 + 子命令分层（boot / log / var / thread）

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/boot
    ${CMAKE_CURRENT_SOURCE_DIR}/log
    ${CMAKE_CURRENT_SOURCE_DIR}/log/cmd
    ${CMAKE_CURRENT_SOURCE_DIR}/log/process
    ${CMAKE_CURRENT_SOURCE_DIR}/log/queue
    ${CMAKE_CURRENT_SOURCE_DIR}/log/transport
    ${CMAKE_CURRENT_SOURCE_DIR}/var
    ${CMAKE_CURRENT_SOURCE_DIR}/thread
)

if(CONFIG_DUST_CMD_SHELL)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/shell.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/boot/boot_cmd.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/thread/thread_cmd.cpp
    )
endif()
if(CONFIG_DUST_CMD_SHELL_VAR)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/var/var.cpp)
endif()
if(CONFIG_DUST_CMD_SHELL_LOG)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/log/log.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/log/process/log_process.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/log/cmd/log_cmd.cpp
    )
endif()
