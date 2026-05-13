cmake_minimum_required(VERSION 3.10)
project(RPCServer)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置编译选项 - 方便调试
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -Wall")
set(CMAKE_BUILD_TYPE Debug)

# 查找 pkg-config - 关键修改
find_package(PkgConfig REQUIRED)
if(PKG_CONFIG_FOUND)
    message(STATUS "PkgConfig found, using it")
else()
    message(FATAL_ERROR "PkgConfig not found")
endif()

# 现在可以使用 pkg_check_modules
pkg_check_modules(LIBEVENT REQUIRED libevent)
pkg_check_modules(LIBEVENT_PTHREADS REQUIRED libevent_pthreads)

# 查找protobuf
find_package(Protobuf REQUIRED)

# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBEVENT_INCLUDE_DIRS}
    ${Protobuf_INCLUDE_DIRS}
)

# 源文件
set(SOURCE_FILES
    log.cpp
    main.cpp
    protocol.cpp
    SafeQueue.cpp
    ThreadPool.cpp
)

# 创建可执行文件
add_executable(RPCServer ${SOURCE_FILES})

# 链接库
target_link_libraries(RPCServer
    ${LIBEVENT_LIBRARIES}
    ${LIBEVENT_PTHREADS_LIBRARIES}
    ${Protobuf_LIBRARIES}
    pthread
)

# 设置输出目录
set_target_properties(RPCServer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/bin/x64/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/bin/x64/Release
)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")