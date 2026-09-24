cmake_minimum_required(VERSION 3.20)
project(JFGRuntimeQueues LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)
set(JFG_RUNTIME "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/N64ModernRuntime")
# The headless queue adapter needs only these unmodified runtime components.
# Rendering, timers, audio and thread creation are not initialized here.
add_library(jfg_queues SHARED
    queues.cpp
    "${JFG_RUNTIME}/ultramodern/src/mesgqueue.cpp"
    "${JFG_RUNTIME}/ultramodern/src/threadqueue.cpp"
    "${JFG_RUNTIME}/ultramodern/src/scheduling.cpp"
    "${JFG_RUNTIME}/ultramodern/src/threads.cpp")
target_include_directories(jfg_queues PRIVATE
    "${JFG_RUNTIME}/ultramodern/include"
    "${JFG_RUNTIME}/thirdparty"
    "${JFG_RUNTIME}/thirdparty/concurrentqueue")
target_link_libraries(jfg_queues PRIVATE Threads::Threads)
if(WIN32)
    set_target_properties(jfg_queues PROPERTIES PREFIX "")
    target_compile_definitions(jfg_queues PRIVATE NOMINMAX)
    if(MINGW)
        target_link_options(jfg_queues PRIVATE -static-libstdc++ -static-libgcc)
        if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
            # Upstream spells Windows.h as on Windows; LLVM-MinGW ships the
            # lowercase name on a case-sensitive Linux filesystem.
            file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/compat")
            file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/compat/Windows.h" "#include <windows.h>\n")
            target_include_directories(jfg_queues PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/compat")
        endif()
    endif()
endif()
if(NOT MSVC)
    target_compile_options(jfg_queues PRIVATE -fvisibility=hidden -fno-strict-aliasing)
endif()
