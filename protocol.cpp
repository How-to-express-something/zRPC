#include "protocol.h"


#include<arpa/inet.h>
#include<cstring>


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

void ReadUint64(const std::string& data, size_t offset, uint64_t& value)
{
    uint64_t network_value;
    std::memcpy(&network_value, data.data() + offset, sizeof(network_value));
    value = be64toh(network_value);
}

bool EncodeHeader(const FrameHeader& header, std::string& out)
{   
    out.clear();
    out.reserve(FrameHeader::header_size);

	WriteUint16(out,header.magic_number);
	WriteUint8(out,header.version);
	WriteUint8(out,header.flags);
	WriteUint8(out, static_cast<uint8_t>(header.message_type));
	WriteUint8(out, static_cast<uint8_t>(header.compression_type));
	WriteUint8(out, static_cast<uint8_t>(header.encryption_type));
    WriteUint8(out, static_cast<uint8_t>(header.serialization_type));

	WriteUint64(out, header.request_id);
	WriteUint32(out, header.body_length);
	WriteUint32(out, header.checksum);


    WriteUint32(out, header.reserved);
	
    return out.size() == FrameHeader::header_size;
}

bool DecodeHeader(const std::string& data, FrameHeader& header)
{
    if(data.size() < FrameHeader::header_size)
    {
        return false; // 数据长度不足
	}

    size_t offset = 0;
    ReadUint16(data, offset, header.magic_number);
    offset += sizeof(header.magic_number);
    ReadUint8(data, offset, header.version);
    offset += sizeof(header.version);
    ReadUint8(data, offset, header.flags);
    offset += sizeof(header.flags);
    ReadUint8(data, offset, reinterpret_cast<uint8_t&>(header.message_type));
    offset += sizeof(header.message_type);
    ReadUint8(data, offset, reinterpret_cast<uint8_t&>(header.compression_type));
    offset += sizeof(header.compression_type);
    ReadUint8(data, offset, reinterpret_cast<uint8_t&>(header.encryption_type));
    offset += sizeof(header.encryption_type);
    ReadUint8(data, offset, reinterpret_cast<uint8_t&>(header.serialization_type));
    offset += sizeof(header.serialization_type);
    ReadUint64(data, offset, header.request_id);
    offset += sizeof(header.request_id);
    ReadUint32(data, offset, header.body_length);
    offset += sizeof(header.body_length);
    ReadUint32(data, offset, header.checksum);
    offset += sizeof(header.checksum);
    ReadUint32(data, offset, header.reserved);
    offset += sizeof(header.reserved);

    return true;
}