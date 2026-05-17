#include "serialzation.h"

const Serializer *GetSerializer(SerializationType type)
{   

    static ProtobufSerializer protobuf_serializer;
    static JsonSerializer json_serializer;
    switch (type)
    {
    case SerializationType::Protobuf:
        return &protobuf_serializer;
    case SerializationType::Json:
        return &json_serializer;
    default:
        return nullptr;
    }
}

bool SerializeMessage(const google::protobuf::Message &message, SerializationType type, std::string &out, std::string *error)
{
    const Serializer *serializer = GetSerializer(type);
    if (!serializer)
    {   
        error->assign("Unsupported serialization type");
        return false;
    }
    return serializer->Serialize(message, out, error);
}

bool DeserializeMessage(const std::string &data, SerializationType type, google::protobuf::Message &message, std::string *error)
{
    const Serializer *serializer = GetSerializer(type);
    if (!serializer)
    {
        error->assign("Unsupported serialization type");
        return false;
    }
    return serializer->Deserialize(data, message, error);
}

bool ProtobufSerializer::Serialize(const google::protobuf::Message &message, std::string &out, std::string *error) const
{
    if(!message.SerializeToString(&out))
    {
        error->assign("Failed to serialize message");
        return false;
    }
    return true;
}

bool ProtobufSerializer::Deserialize(const std::string &data, google::protobuf::Message &message, std::string *error) const
{
    if (!message.ParseFromString(data))
    {
        error->assign("Failed to deserialize message");
        return false;
    }
    return true;
}

bool JsonSerializer::Serialize(const google::protobuf::Message &message, std::string &out, std::string *error) const
{   
    google::protobuf::util::JsonPrintOptions opts;
    opts.preserve_proto_field_names = true;    // 保留原始字段名
    auto status = google::protobuf::util::MessageToJsonString(message, &out, opts);
    if(!status.ok())
    {
       error->assign("Failed to serialize message to JSON: " + status.ToString());
                return false;
    }
   
    return true;
}

bool JsonSerializer::Deserialize(const std::string &data, google::protobuf::Message &message, std::string *error) const
{
    auto status = google::protobuf::util::JsonStringToMessage(data, &message);
    if(!status.ok())
    {
        error->assign("Failed to deserialize message from JSON: " + status.ToString());
        return false;
    }
    return true;
}
