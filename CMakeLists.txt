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

find_package(ZLIB REQUIRED)
message(STATUS "Found ZLIB: ${ZLIB_VERSION}")

# ===== 新增 1：查找 OpenSSL（必需） =====
find_package(OpenSSL REQUIRED)
message(STATUS "Found OpenSSL: ${OPENSSL_VERSION}")

# ===== 新增 2：可选查找 zstd，如果找到则定义 HAS_ZSTD 宏 =====
find_package(zstd QUIET CONFIG)   # 优先用 CONFIG 模式寻找官方 zstdConfig.cmake
if(NOT zstd_FOUND)
    # 如果没找到，尝试用 pkg-config 再找一次（很多发行版会提供 .pc 文件）
    pkg_check_modules(zstd IMPORTED_TARGET libzstd)
endif()
if(zstd_FOUND)
    message(STATUS "Found zstd: enabling HAS_ZSTD")
else()
    message(STATUS "zstd not found - compression support disabled")
endif()

# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBEVENT_INCLUDE_DIRS}
)

# 源文件
set(COMMON_SOURCES
    coder.cpp
    serialzation.cpp
    log.cpp
    protocol.cpp
    safe_queue.cpp
    thread_pool.cpp
    rpcserver.cpp
    rpcclient.cpp
    person.pb.cc
    compresser.cpp
    crypto.cpp
)

set(DEMO_SOURCES
    main.cpp
)

set(BENCH_SOURCES
    bench.cpp
)

# 创建可执行文件
add_executable(RPCServer ${COMMON_SOURCES} ${DEMO_SOURCES})
add_executable(Benchmark ${COMMON_SOURCES} ${BENCH_SOURCES})

# ========== 关键修改 4：用目标方式链接，自动传递所有依赖 ==========
function(link_common_libs TARGET_NAME)
    target_link_libraries(${TARGET_NAME}
        protobuf::libprotobuf
        ${LIBEVENT_LIBRARIES}
        ${LIBEVENT_PTHREADS_LIBRARIES}
        absl::strings
        absl::any
        absl::log
        absl::hash
        absl::status
        ZLIB::ZLIB
        OpenSSL::SSL
        OpenSSL::Crypto
        pthread
    )
    if(zstd_FOUND)
        if(TARGET PkgConfig::zstd)
            target_link_libraries(${TARGET_NAME} PkgConfig::zstd)
        elseif(TARGET zstd::libzstd)
            target_link_libraries(${TARGET_NAME} zstd::libzstd)
        else()
            target_link_libraries(${TARGET_NAME} ${zstd_LIBRARIES})
            target_include_directories(${TARGET_NAME} PRIVATE ${zstd_INCLUDE_DIRS})
        endif()
        target_compile_definitions(${TARGET_NAME} PRIVATE HAS_ZSTD)
    endif()
endfunction()

link_common_libs(RPCServer)
link_common_libs(Benchmark)

# 设置输出目录
set_target_properties(RPCServer Benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/bin/x64/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/bin/x64/Release
)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")