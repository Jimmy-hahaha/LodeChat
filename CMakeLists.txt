cmake_minimum_required(VERSION 3.20)

project(llm-chat
    VERSION 1.0.0
    DESCRIPTION "A minimal LLM chat client"
    LANGUAGES CXX
)

# ───────── Global settings ─────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# ───────── Options ─────────
set(LLMCHAT_LINKER "auto" CACHE STRING
    "Linker backend: auto | mold | default")
set_property(CACHE LLMCHAT_LINKER PROPERTY STRINGS auto mold default)

option(LLMCHAT_USE_PCH "Enable precompiled headers" ON)

# ───────── Linker selection (mold with graceful fallback) ─────────
set(_llmchat_use_mold FALSE)

if(LLMCHAT_LINKER STREQUAL "mold")
    find_program(MOLD_EXECUTABLE mold)
    if(NOT MOLD_EXECUTABLE)
        message(FATAL_ERROR
            "LLMCHAT_LINKER=mold requested, but mold was not found. "
            "Install mold or configure with -DLLMCHAT_LINKER=default.")
    endif()
    set(_llmchat_use_mold TRUE)
    message(STATUS "Linker: mold (forced) -> ${MOLD_EXECUTABLE}")

elseif(LLMCHAT_LINKER STREQUAL "auto")
    if(UNIX AND NOT APPLE)
        find_program(MOLD_EXECUTABLE mold)
        if(MOLD_EXECUTABLE)
            set(_llmchat_use_mold TRUE)
            message(STATUS "Linker: mold (auto)   -> ${MOLD_EXECUTABLE}")
        else()
            message(STATUS "Linker: system default (mold not found)")
        endif()
    else()
        message(STATUS "Linker: system default (mold unsupported on this platform)")
    endif()

elseif(LLMCHAT_LINKER STREQUAL "default")
    message(STATUS "Linker: system default (requested)")

else()
    message(FATAL_ERROR
        "Unknown LLMCHAT_LINKER='${LLMCHAT_LINKER}'. "
        "Valid values: auto | mold | default.")
endif()

if(_llmchat_use_mold)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.29")
        set(CMAKE_LINKER_TYPE MOLD)
    else()
        add_link_options("-fuse-ld=mold")
    endif()
endif()

# ───────── Target ─────────
add_executable(LodeChat
    src/main.cpp
)

add_executable(config_tool
    src/config_tool.cpp
)

target_include_directories(LodeChat
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(config_tool
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ───────── Precompiled headers (opt-in) ─────────
if(LLMCHAT_USE_PCH)
    target_precompile_headers(LodeChat
        PRIVATE
            <iostream>
            <string>
            <string_view>
            <vector>
            <memory>
            <unordered_map>
            <optional>
            <functional>
    )
    message(STATUS "PCH:     enabled")
else()
    message(STATUS "PCH:     disabled")
endif()

# ───────── Dependencies ─────────
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(fmt CONFIG QUIET)

target_link_libraries(LodeChat
    PRIVATE
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto
        $<$<BOOL:${fmt_FOUND}>:fmt::fmt>
)

# config_tool 只用 json + spdlog（header-only），不需要 OpenSSL
target_link_libraries(config_tool
    PRIVATE
        Threads::Threads
        $<$<BOOL:${fmt_FOUND}>:fmt::fmt>
)

# ───────── Optional: GNU readline (line editing, history, completion) ─────────
if(WIN32)
    message(STATUS "readline: not used on Windows (built-in line editor)")
else()
    find_path(READLINE_INCLUDE_DIR readline/readline.h)
    find_library(READLINE_LIBRARY NAMES readline)
    if(READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
        target_include_directories(LodeChat PRIVATE ${READLINE_INCLUDE_DIR})
        target_link_libraries(LodeChat PRIVATE ${READLINE_LIBRARY})
        target_compile_definitions(LodeChat PRIVATE LODECHAT_HAVE_READLINE=1)
        message(STATUS "readline: enabled   -> ${READLINE_LIBRARY}")
    else()
        message(STATUS "readline: not found, falling back to std::getline")
    endif()
endif()
