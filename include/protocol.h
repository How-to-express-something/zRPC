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
	uint16_t magic_number{0x5250}; // 4�ֽ�ħ��
	uint8_t version{ 1 }; // 1�ֽڰ汾��
	uint8_t flags{ 0 }; // 1�ֽڱ�־λ��������Ϣ���͡����л����͡�ѹ�����ͺͼ������͵���Ϣ
	MessageType message_type;
	SerializationType serialization_type;
	CompressionType compression_type;
	EncryptionType encryption_type;
	uint64_t request_id;
	uint32_t body_length;
	uint32_t checksum; //CRC32У����
	uint32_t reserved{ 0 };

	static constexpr size_t header_size = sizeof(magic_number) + sizeof(version) + sizeof(flags) + sizeof(message_type) + sizeof(serialization_type) +
		sizeof(compression_type) + sizeof(encryption_type) + sizeof(request_id) + sizeof(body_length) + sizeof(checksum) + sizeof(reserved);//(28)
};


struct RpcRequest {
	uint64_t request_id{ 0 };                                 // ���� ID
	std::string service;                                    // ������
	std::string method;                                     // ������
	std::string route_key;                                  // ·�ɼ�����ѡ��
	std::string payload;                                    // ���л����������
	uint32_t timeout_ms{ 3000 };                              // ��ʱ������
	SerializationType serialization{ SerializationType::Protobuf };  // ���л���ʽ
	CompressionType compression{ CompressionType::None };            // ѹ����ʽ
	EncryptionType encryption{ EncryptionType::None};              // ���ܷ�ʽ
};

// ��Ӧ�غɽṹ
struct RpcResponse {
	uint64_t request_id{ 0 };                                 // ��Ӧ������ ID
	int32_t status_code{ 0 };                                 // ҵ��״̬�룬0 ��ʾ�ɹ�
	std::string error_message;                              // ����������ʧ��ʱ��
	std::string payload;                                    // ���л������Ӧ��
	SerializationType serialization{ SerializationType::Protobuf };  // ���л���ʽ
	CompressionType compression{ CompressionType::None };            // ѹ����ʽ
	EncryptionType encryption{ EncryptionType::None };              // ���ܷ�ʽ
};

// ��֡ͷ����������ֽ��򻺳���
bool EncodeHeader(const FrameHeader& header, std::string& out);

// �����绺��������֡ͷ
bool DecodeHeader(const std::string& data, FrameHeader& header);