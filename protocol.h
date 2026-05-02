#pragma once

#include<cstdint>
#include<string>


enum class MessageType : uint8_t
{
	Heartbeat = 0,
	Request = 1,
	Response = 2,
};

enum class SerializationType : uint8_t
{
	Json = 0,
	Protobuf = 1,
};

enum class CompressionType : uint8_t
{
	None = 0,
	Zlib = 1,
	Zstd = 2,
};

enum class EncryptionType : uint8_t
{
	None = 0,
	AES = 1,
};


struct FrameHeader
{	
	uint16_t magic_number{0x5250}; // 4字节魔数
	uint8_t version{ 1 }; // 1字节版本号
	uint8_t flags{ 0 }; // 1字节标志位，包含消息类型、序列化类型、压缩类型和加密类型等信息
	MessageType message_type;
	SerializationType serialization_type;
	CompressionType compression_type;
	EncryptionType encryption_type;
	uint64_t request_id;
	uint32_t body_length;
	uint32_t checksum; //CRC32校验码
	uint32_t reserved{ 0 };

	static constexpr size_t header_size = sizeof(magic_number) + sizeof(version) + sizeof(flags) + sizeof(message_type) + sizeof(serialization_type) +
		sizeof(compression_type) + sizeof(encryption_type) + sizeof(request_id) + sizeof(body_length) + sizeof(checksum) + sizeof(reserved);//(28)
};


struct RpcRequest {
	uint64_t request_id{ 0 };                                 // 请求 ID
	std::string service;                                    // 服务名
	std::string method;                                     // 方法名
	std::string route_key;                                  // 路由键（可选）
	std::string payload;                                    // 序列化后的请求体
	uint32_t timeout_ms{ 3000 };                              // 超时毫秒数
	SerializationType serialization{ SerializationType::Protobuf };  // 序列化方式
	CompressionType compression{ CompressionType::None };            // 压缩方式
	EncryptionType encryption{ EncryptionType::None };              // 加密方式
};

// 响应载荷结构
struct RpcResponse {
	uint64_t request_id{ 0 };                                 // 对应的请求 ID
	int32_t status_code{ 0 };                                 // 业务状态码，0 表示成功
	std::string error_message;                              // 错误描述（失败时）
	std::string payload;                                    // 序列化后的响应体
	SerializationType serialization{ SerializationType::Protobuf };  // 序列化方式
	CompressionType compression{ CompressionType::None };            // 压缩方式
	EncryptionType encryption{ EncryptionType::None };              // 加密方式
};

// 将帧头编码成网络字节序缓冲区
bool EncodeHeader(const FrameHeader& header, std::string& out);

// 从网络缓冲区解码帧头
bool DecodeHeader(const std::string& data, FrameHeader& header);