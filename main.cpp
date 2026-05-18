#include "coder.h"
#include "protocol.h"
#include <cassert>
#include <iostream>

// ==================== 桩函数（后续替换为实际实现） ====================



// EncodeHeader 和 DecodeHeader 已在 protocol 中实现，这里无需提供桩

int main() {
    // 1. 构造 RPC 请求
    RpcRequest req;
    req.request_id = 123456789;
    req.service = "Calculator";
    req.method = "Add";
    req.route_key = "user_42";
    req.payload = "protobuf_binary_data";
    req.timeout_ms = 5000;
    req.serialization = SerializationType::Protobuf;
    req.compression = CompressionType::None;
    req.encryption = EncryptionType::None;

    // 2. 编码请求
    CodecOptions options;
    std::string frame;
    std::string error;
    bool ok = EncodeRequest(req, options, frame, &error);
    assert(ok && "EncodeRequest failed");
    std::cout << "✓ EncodeRequest succeeded, frame size = " << frame.size() << std::endl;

    // 3. 用 DecodeHeader 解析头部，得到 body
    if (frame.size() < FrameHeader::header_size) {
        std::cerr << "Frame too small" << std::endl;
        return 1;
    }
    FrameHeader header;
    std::string header_part = frame.substr(0, FrameHeader::header_size);
    if (!DecodeHeader(header_part, header)) {
        std::cerr << "DecodeHeader failed" << std::endl;
        return 1;
    }
    std::string body = frame.substr(FrameHeader::header_size);

    // 4. 解码并校验帧
    DecodedFrame decoded;
    std::string decode_error;
    ok = VerifyAndDecodeFrame(header, body, decoded, options, &decode_error);
    std::cerr << "Decode error (if any): " << decode_error << std::endl;
    assert(ok && "VerifyAndDecodeFrame failed");
    std::cout << "✓ Decode succeeded, type = " << static_cast<int>(decoded.type) << std::endl;

    // 5. 验证请求字段
    assert(decoded.type == MessageType::Request);
    assert(decoded.request.request_id == req.request_id);
    assert(decoded.request.service == req.service);
    assert(decoded.request.method == req.method);
    assert(decoded.request.route_key == req.route_key);
    assert(decoded.request.payload == req.payload);
    assert(decoded.request.serialization == req.serialization);
    assert(decoded.request.compression == req.compression);
    assert(decoded.request.encryption == req.encryption);
    std::cout << "✓ All request fields match" << std::endl;

    // 6. 测试响应编解码
    RpcResponse resp;
    resp.request_id = 123456789;
    resp.status_code = 0;
    resp.error_message = "";
    resp.payload = "result_data";
    resp.serialization = SerializationType::Protobuf;
    resp.compression = CompressionType::None;
    resp.encryption = EncryptionType::None;

    ok = EncodeResponse(resp, options, frame, &error);
    assert(ok && "EncodeResponse failed");
    std::cout << "✓ EncodeResponse succeeded, frame size = " << frame.size() << std::endl;

    header_part = frame.substr(0, FrameHeader::header_size);
    if (!DecodeHeader(header_part, header)) {
        std::cerr << "DecodeHeader failed for response" << std::endl;
        return 1;
    }
    body = frame.substr(FrameHeader::header_size);

    DecodedFrame decoded_resp;
    ok = VerifyAndDecodeFrame(header, body, decoded_resp, options, &decode_error);
    assert(ok && "Decode response failed");
    assert(decoded_resp.type == MessageType::Response);
    assert(decoded_resp.response.request_id == resp.request_id);
    assert(decoded_resp.response.status_code == resp.status_code);
    assert(decoded_resp.response.payload == resp.payload);
    std::cout << "✓ All response fields match" << std::endl;

    std::cout << "\n🎉 All tests passed!" << std::endl;
    return 0;
}