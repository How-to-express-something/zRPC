#include "coder.h"


#include<arpa/inet.h>
#include<cstring>
#include<zlib.h>
#include<iostream>
#include "crypto.h"
#include "compresser.h"



void WriteUint8(std::string& out, uint8_t value)
{
    out.push_back(static_cast<char>(value));
}

void WriteUint16(std::string& out, uint16_t value)
{
    uint16_t network_value = htons(value);
    out.append(reinterpret_cast<const char*>(&network_value), sizeof(network_value));
}


void WriteUint32(std::string& out, uint32_t value)
{
    uint32_t network_value = htonl(value);
    out.append(reinterpret_cast<const char*>(&network_value), sizeof(network_value));
}


void WriteInt32(std::string& out, int32_t value)
{
    uint32_t network_value = htonl(static_cast<uint32_t>(value));
    out.append(reinterpret_cast<const char*>(&network_value), sizeof(network_value));
}

void WriteUint64(std::string& out, uint64_t value)
{
    uint64_t network_value = htobe64(value);
    out.append(reinterpret_cast<const char*>(&network_value), sizeof(network_value));
}

void ReadUint8(const std::string& data, size_t offset, uint8_t& value)
{
    value = static_cast<uint8_t>(data[offset]);
}

void ReadUint16(const std::string& data, size_t offset, uint16_t& value)
{
    uint16_t network_value;
    std::memcpy(&network_value, data.data() + offset, sizeof(network_value));
    value = ntohs(network_value);
}

void ReadUint32(const std::string& data, size_t offset, uint32_t& value)
{
    uint32_t network_value;
    std::memcpy(&network_value, data.data() + offset, sizeof(network_value));
    value = ntohl(network_value);
}


void ReadInt32(const std::string& data, size_t offset, int32_t& value)
{
    uint32_t network_value;
    std::memcpy(&network_value, data.data() + offset, sizeof(network_value));
    value = static_cast<int32_t>(ntohl(network_value));
}

void ReadUint64(const std::string& data, size_t offset, uint64_t& value)
{
    uint64_t network_value;
    std::memcpy(&network_value, data.data() + offset, sizeof(network_value));
    value = be64toh(network_value);
}

bool ProcessEncrypt(const std::string& plaintext, std::string& ciphertext, EncryptionType type, const std::string& key, const std::string& iv, std::string* error)
{
    if (type == EncryptionType::None) {
        ciphertext = plaintext;  // 不加密，直接复制
        return true;  
    }

    if (!Encrypt(type, key, iv, plaintext, ciphertext, error)) {
        return false;  // 加密失败
    }
    return true;  // 返回true表示已加密
}

bool ProcessDecrypt(const std::string& ciphertext, std::string& plaintext, EncryptionType type, const std::string& key, const std::string& iv, std::string* error)
{
    if (type == EncryptionType::None) {
        plaintext = ciphertext;  // 不加密，直接复制
        return true;  
    }

    if (!Decrypt(type, key, iv, ciphertext, plaintext, error)) {
        return false;  // 解密失败
    }
    return true;  // 返回true表示已解密
}

uint32_t ComputeFrameCRC(const FrameHeader& header, const std::string& body) {
    uint32_t crc = crc32(0L, Z_NULL, 0);  // 初始值
    
    // 将 header 转为字节流
    crc = crc32(crc,
                reinterpret_cast<const unsigned char*>(&header),
                static_cast<unsigned int>(header.header_size));
    
    // 将 body 转为字节流
    crc = crc32(crc,
                reinterpret_cast<const unsigned char*>(body.data()),
                static_cast<unsigned int>(body.size()));
    
    return crc;
}

int MaybeCompress(const std::string& input, std::string& output, CompressionType type, int level, uint32_t threshold, std::string* error)
{
    if (input.size() < threshold) {
        output = input;  // 不压缩，直接复制
        return 0;  // 返回0表示未压缩
    }

    if (!Compress(type, input, output, level, error)) {
        return -1;  // 压缩失败
    }
    return 1;  // 返回1表示已压缩
}


bool EncodeRequest(const RpcRequest &request, const CodecOptions &options, std::string &frame, std::string *error)
{   
    FrameHeader header;
    header.message_type = MessageType::Request;
    header.serialization_type = request.serialization;
    header.encryption_type = request.encryption;
    header.request_id = request.request_id;


    std::string body;
    WriteUint16(body,request.service.size());
    WriteUint16(body,request.method.size());
    WriteUint16(body,request.route_key.size());
    body.append(request.service);
    body.append(request.method);
    body.append(request.route_key);
    body.append(request.payload);

    
    if(!ProcessEncrypt(body, body, request.encryption, options.aes_key, options.aes_iv, error))
    {
        return false;
    }

    int compress_result = MaybeCompress(body, body, request.compression, options.compression_level, options.compression_threshold, error);
    if (compress_result == -1) {
        return false;  // 压缩失败
    }
    else if(compress_result == 1) {
        header.compression_type = request.compression;  // 已压缩
    }
    else {
        header.compression_type = CompressionType::None;  // 未压缩
    }


    header.body_length = body.size();
    header.checksum = 0;
   
    header.checksum = ComputeFrameCRC(header, body);

    frame.clear();
    EncodeHeader(header, frame);
    frame.append(body);    
    return true;
}

bool EncodeResponse(const RpcResponse &response, const CodecOptions &options, std::string &frame, std::string *error)
{   

//     struct RpcResponse {
// 	uint64_t request_id{ 0 };                                 // ��Ӧ������ ID
// 	int32_t status_code{ 0 };                                 // ҵ��״̬�룬0 ��ʾ�ɹ�
// 	std::string error_message;                              // ����������ʧ��ʱ��
// 	std::string payload;                                    // ���л������Ӧ��
// 	SerializationType serialization{ SerializationType::Protobuf };  // ���л���ʽ
// 	CompressionType compression{ CompressionType::None };            // ѹ����ʽ
// 	EncryptionType encryption{ EncryptionType::None };              // ���ܷ�ʽ
// };
    FrameHeader header;
    header.message_type = MessageType::Response;
    header.serialization_type = response.serialization;
    header.encryption_type = response.encryption;
    header.request_id = response.request_id;


    std::string body;
    WriteInt32(body, response.status_code);
    WriteUint16(body, response.error_message.size());
    body.append(response.error_message);
    body.append(response.payload);

    
    if(!ProcessEncrypt(body, body, response.encryption, options.aes_key, options.aes_iv, error))
    {
        return false;
    }

    int compress_result = MaybeCompress(body, body, response.compression, options.compression_level, options.compression_threshold, error);
    if (compress_result == -1) {
        return false;  // 压缩失败
    }
    else if(compress_result == 1) {
        header.compression_type = response.compression;  // 已压缩
    }
    else {
        header.compression_type = CompressionType::None;  // 未压缩
    }


    header.body_length = body.size();
    header.checksum = 0;
    header.checksum = ComputeFrameCRC(header, body);

    frame.clear();
    EncodeHeader(header, frame);
    frame.append(body);    
    return true;

  
}




bool VerifyFrameCRC(FrameHeader& header, const std::string& body) {
    uint32_t frame_crc = header.checksum;
    header.checksum = 0;  // 计算 CRC 时，checksum 字段应视为 0
    uint32_t computed_crc = ComputeFrameCRC(header, body);
    return computed_crc == frame_crc;
}


bool DecodeRequest(const std::string& body, RpcRequest& request, const CodecOptions& options, std::string* error) {
    // 解析 service/method/route_key/payload 等字段
    // ... 解析逻辑
    std::string body_decoded;
    if(!ProcessDecrypt(body, body_decoded, request.encryption, options.aes_key, options.aes_iv, error))
    {   
        return false;  // 解密失败
    }
    if(!Decompress(request.compression, body_decoded, body_decoded, error))
    {
        return false;  // 解压失败
    }

    uint16_t service_len, method_len, route_key_len;
    size_t offset = 6;
    ReadUint16(body_decoded, 0, service_len);
    ReadUint16(body_decoded, 2, method_len);
    ReadUint16(body_decoded, 4, route_key_len);
    request.service.assign(body_decoded.data() + offset, service_len);
    offset += service_len;
    request.method.assign(body_decoded.data() + offset, method_len);
    offset += method_len;
    request.route_key.assign(body_decoded.data() + offset, route_key_len);
    offset += route_key_len;
    request.payload.assign(body_decoded.data() + offset, body_decoded.size() - offset);

    return true;
}

bool DecodeResponse(const std::string& body, RpcResponse& response, const CodecOptions& options, std::string* error) {
    // 解析 status_code/error_message/payload 等字段
    // ... 解析逻辑
    std::string body_decoded;
    if(!ProcessDecrypt(body, body_decoded, response.encryption, options.aes_key, options.aes_iv, error))
    {   
        return false;  // 解密失败
    }
    if(!Decompress(response.compression, body_decoded, body_decoded, error))
    {
        return false;  // 解压失败
    }

    uint16_t error_message_len;
    size_t offset = 6;
    ReadInt32(body_decoded, 0, response.status_code);
    ReadUint16(body_decoded, 4, error_message_len);
    response.error_message.assign(body_decoded.data() + offset, error_message_len);
    offset += error_message_len;
    response.payload.assign(body_decoded.data() + offset, body_decoded.size() - offset);

    return true;
}


bool VerifyAndDecodeFrame(FrameHeader& header, const std::string& body, DecodedFrame& decoded, const CodecOptions& options, std::string* error) {
    if (header.magic_number != 0x5250) {
        if (error) *error = "Invalid magic number";
        return false;  // 魔数不匹配
    }


    if (!VerifyFrameCRC(header, body)) {
        if (error) *error = "CRC check failed";
        return false;  // CRC校验失败
    }

    // 根据消息类型解析请求/响应/心跳
    decoded.type = header.message_type;
    if (decoded.type == MessageType::Heartbeat) {
       return true;  // 心跳没有 body，直接返回
    }
    else if(decoded.type == MessageType::Request) {
        // 解析请求
        // ... 解析 service/method/route_key/payload 等字段
        decoded.request.request_id = header.request_id;
        decoded.request.serialization = header.serialization_type;
        decoded.request.compression = header.compression_type;  
        decoded.request.encryption = header.encryption_type;
        return DecodeRequest(body, decoded.request, options, error);
    }
    else if(decoded.type == MessageType::Response) {
        // 解析响应
        // ... 解析 status_code/error_message/payload 等字段
        decoded.response.request_id = header.request_id;
        decoded.response.serialization = header.serialization_type;
        decoded.response.compression = header.compression_type;
        decoded.response.encryption = header.encryption_type;
        return DecodeResponse(body, decoded.response, options, error);
    }

    return true;
}   

bool DecodeFrame(std::string_view frame, const CodecOptions &options, DecodedFrame &decoded, std::string *error)
{
    return false;
}
