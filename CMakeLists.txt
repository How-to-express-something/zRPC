cmake_minimum_required(VERSION 3.10)
project(RPCServer)

# 设置C++标准（Abseil 和 Protobuf 都需要 C++17）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置编译选项
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -Wall")
set(CMAKE_BUILD_TYPE Debug)

# ========== 关键修改 1：让 CMake 能搜索到 /usr/local 下的库 ==========
# 你的 Abseil 和 Protobuf 很可能安装在 /usr/local，确保 CMake 知道这个路径
list(APPEND CMAKE_PREFIX_PATH /usr/local)

# ========== 关键修改 2：找到 Abseil ==========
find_package(absl REQUIRED CONFIG)
message(STATUS "Found Abseil: ${absl_VERSION}")

# ========== 关键修改 3：告知 Protobuf 使用系统的 Abseil ==========
# 这一句必须在 find_package(Protobuf) 之前
set(protobuf_ABSL_PROVIDER "package" CACHE STRING "Use system Abseil")

# ========== 查找其他依赖 ==========
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent)
pkg_check_modules(LIBEVENT_PTHREADS REQUIRED libevent_pthreads)

# 查找 Protobuf（此时它会去检查系统 Abseil 是否可用）
find_package(Protobuf REQUIRED CONFIG)
message(STATUS "Found Protobuf: ${Protobuf_VERSION}")

# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBEVENT_INCLUDE_DIRS}
    # 使用现代目标后，Protobuf 的头文件路径会自动添加，这里可以省略
)

# 源文件
set(SOURCE_FILES
    log.cpp
    main.cpp
    protocol.cpp
    safe_queue.cpp
    thread_pool.cpp
    rpcserver.cpp
)

# 创建可执行文件
add_executable(RPCServer ${SOURCE_FILES})

# ========== 关键修改 4：用目标方式链接，自动传递所有依赖 ==========
target_link_libraries(RPCServer
    protobuf::libprotobuf        # 代替 ${Protobuf_LIBRARIES}，它会带上 Abseil 等依赖
    ${LIBEVENT_LIBRARIES}
    ${LIBEVENT_PTHREADS_LIBRARIES}
    # 额外补充几个常用的 Abseil 组件（防止某些符号找不到）
    absl::strings
    absl::any
    absl::log
    absl::hash
    absl::status
    pthread
)

# 设置输出目录
set_target_properties(RPCServer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/bin/x64/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/bin/x64/Release
)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")