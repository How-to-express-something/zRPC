#pragma once

#include <string>
#include <string_view>

#include "protocol.h"


struct CodecOptions {
    std::string aes_key;                      // AES 密钥（支持 128/192/256 bit）
    std::string aes_iv;                       // AES 初始化向量
    uint32_t compression_threshold{1024};     // 触发压缩的最小负载（字节）
    int compression_level{3};                 // 压缩等级（传递给 zlib/zstd）
};

struct DecodedFrame {
    MessageType type{MessageType::Request};  // 帧类型，决定解析对象
    RpcRequest request;                       // 解析后的请求
    RpcResponse response;                     // 解析后的响应/心跳
};

// 将 RPC 请求编码为完整帧（含帧头与校验）
bool EncodeRequest(const RpcRequest& request, const CodecOptions& options, std::string& frame, std::string* error);

// 将 RPC 响应编码为完整帧
bool EncodeResponse(const RpcResponse& response, const CodecOptions& options, std::string& frame, std::string* error);




// 解码并校验帧，输出结构化结果   自动识别请求/响应/心跳并解析对应内容（所以要返回结构体）
bool VerifyAndDecodeFrame(FrameHeader& header, const std::string& body, DecodedFrame& decoded, const CodecOptions& options, std::string* error);


