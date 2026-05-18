#include "compresser.h"
#include <zlib.h>

#include <string>

#if HAS_ZSTD
#include <zstd.h>
#endif





// 构造简单的错误信息，包含算法前缀与错误码
std::string MakeError(const char* prefix, int code) {
    return std::string(prefix) + " error code: " + std::to_string(code);
}


// 根据压缩算法执行压缩，支持 zlib/zstd，kNone 时直接拷贝
bool Compress(CompressionType type, std::string_view input, std::string& output, int level, std::string* error) {
    switch (type) {
    case CompressionType::None:
        output.assign(input.data(), input.size());
        return true;
    case CompressionType::Zlib: {
        uLongf dest_len = compressBound(static_cast<uLong>(input.size()));
        output.resize(dest_len);
        int ret = compress2(reinterpret_cast<Bytef*>(&output[0]), &dest_len, reinterpret_cast<const Bytef*>(input.data()),
                            static_cast<uLong>(input.size()), level);
        if (ret != Z_OK) {
            if (error != nullptr) {
                *error = MakeError("zlib compress", ret);
            }
            return false;
        }
        output.resize(dest_len);
        return true;
    }
    case CompressionType::Zstd: {
#if     HAS_ZSTD
        size_t bound = ZSTD_compressBound(input.size());
        output.resize(bound);
        size_t ret = ZSTD_compress(output.data(), bound, input.data(), input.size(), level);
        if (ZSTD_isError(ret)) {
            if (error != nullptr) {
                *error = ZSTD_getErrorName(ret);
            }
            return false;
        }
        output.resize(ret);
        return true;
#else
        if (error != nullptr) {
            *error = "zstd not available";
        }
        return false;
#endif
    }
    default:
        if (error != nullptr) {
            *error = "unknown compression type";
        }
        return false;
    }
}

// 根据压缩算法解压缩，动态扩大缓冲区确保解码成功
bool Decompress(CompressionType type, std::string_view input, std::string& output, std::string* error) {
    switch (type) {
    case CompressionType::None:
        output.assign(input.data(), input.size());
        return true;
    case CompressionType::Zlib: {
        uLongf dest_len = static_cast<uLongf>(input.size() * 4 + 64);
        output.resize(dest_len);
        int ret = Z_BUF_ERROR;
        while (true) {
            ret = uncompress(reinterpret_cast<Bytef*>(&output[0]), &dest_len, reinterpret_cast<const Bytef*>(input.data()),
                             static_cast<uLong>(input.size()));
            if (ret == Z_BUF_ERROR) {
                dest_len *= 2;
                output.resize(dest_len);
                continue;
            }
            break;
        }
        if (ret != Z_OK) {
            if (error != nullptr) {
                *error = MakeError("zlib decompress", ret);
            }
            return false;
        }
        output.resize(dest_len);
        return true;
    }
    case CompressionType::Zstd:     {
#if HAS_ZSTD
        unsigned long long const r_size = ZSTD_getFrameContentSize(input.data(), input.size());
        if (r_size == ZSTD_CONTENTSIZE_ERROR || r_size == ZSTD_CONTENTSIZE_UNKNOWN) {
            if (error != nullptr) {
                *error = "zstd frame size unknown";
            }
            return false;
        }
        output.resize(static_cast<size_t>(r_size));
        size_t const d_size = ZSTD_decompress(output.data(), output.size(), input.data(), input.size());
        if (ZSTD_isError(d_size)) {
            if (error != nullptr) {
                *error = ZSTD_getErrorName(d_size);
            }
            return false;
        }
        output.resize(d_size);
        return true;
#else
        if (error != nullptr) {
            *error = "zstd not available";
        }
        return false;
#endif
    }
    default:
        if (error != nullptr) {
            *error = "unknown compression type";
        }
        return false;
    }
}

// 判断 Zstd 能力是否可用（取决于编译配置）
bool HasZstd() {
#if HAS_ZSTD
    return HAS_ZSTD != 0;
#else
    return false;
#endif
}

