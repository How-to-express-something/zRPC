#pragma once

#include <string>
#include <string_view>

#include "protocol.h"

//在cmake中判断是否支持zstd，编译时定义宏 HAS_ZSTD

// 按指定压缩算法压缩数据，返回压缩后结果
bool Compress(CompressionType type, std::string_view input, std::string& output, int level, std::string* error);

// 解压缩数据，自动扩容目标缓冲区
bool Decompress(CompressionType type, std::string_view input, std::string& output, std::string* error);

// 编译期/运行期判断是否支持 Zstd   （编译期通过宏，运行期通过函数，兼容不同环境）
bool HasZstd();

