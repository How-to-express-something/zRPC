#pragma once


#include<google/protobuf/message.h>
#include<google/protobuf/util/json_util.h>
#include "protocol.h"


//编写序列化器（策略模式） 有两种序列化方式：Json和Protobuf
class Serializer
{
public:
    virtual ~Serializer() = default;
    virtual bool Serialize(const google::protobuf::Message& message, std::string& out, std::string* error) const = 0;
    virtual bool Deserialize(const std::string& data, google::protobuf::Message& message, std::string* error) const = 0;
};

//protobuf 
class ProtobufSerializer : public Serializer
{
public:
    bool Serialize(const google::protobuf::Message& message, std::string& out, std::string* error) const override;
    bool Deserialize(const std::string& data, google::protobuf::Message& message, std::string* error) const override;
};


//json
class JsonSerializer : public Serializer
{
public:
    bool Serialize(const google::protobuf::Message& message, std::string& out, std::string* error) const override;
    bool Deserialize(const std::string& data, google::protobuf::Message& message, std::string* error) const override;

};


const Serializer* GetSerializer(SerializationType type);


// 统一接口
bool SerializeMessage(const google::protobuf::Message& message, SerializationType type, std::string& out, std::string* error);

bool DeserializeMessage(const std::string& data, SerializationType type, google::protobuf::Message& message, std::string* error);