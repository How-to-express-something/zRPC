// main.cpp
#include <iostream>
#include "person.pb.h"      // 生成的 protobuf 消息类
#include "serialzation.h"   // 你的序列化框架

int main() {
    // ---- 构造一条消息 ----
    Person original;
    original.set_name("Alice");
    original.set_age(30);
    original.add_emails("alice@example.com");
    original.add_emails("alice@work.org");

    // ---- 测试 Protobuf 二进制序列化 ----
    std::string bin_out;
    std::string error;
    bool ok = SerializeMessage(original, SerializationType::Protobuf, bin_out, &error);
    if (!ok) {
        std::cerr << "Binary serialize failed: " << error << std::endl;
        return 1;
    }
    std::cout << "Binary serialized (" << bin_out.size() << " bytes)" << std::endl;

    // Protobuf 反序列化
    Person restored_from_bin;
    ok = DeserializeMessage(bin_out, SerializationType::Protobuf, restored_from_bin, &error);
    if (!ok) {
        std::cerr << "Binary deserialize failed: " << error << std::endl;
        return 1;
    }
    std::cout << "From binary -> name: " << restored_from_bin.name()
              << ", age: " << restored_from_bin.age()
              << ", emails count: " << restored_from_bin.emails_size() << std::endl;

    // ---- 测试 JSON 序列化 ----
    std::string json_out;
    ok = SerializeMessage(original, SerializationType::Json, json_out, &error);
    if (!ok) {
        std::cerr << "JSON serialize failed: " << error << std::endl;
        return 1;
    }
    std::cout << "\nJSON serialized:\n" << json_out << std::endl;

    // JSON 反序列化
    Person restored_from_json;
    ok = DeserializeMessage(json_out, SerializationType::Json, restored_from_json, &error);
    if (!ok) {
        std::cerr << "JSON deserialize failed: " << error << std::endl;
        return 1;
    }
    std::cout << "From JSON -> name: " << restored_from_json.name()
              << ", age: " << restored_from_json.age()
              << ", emails count: " << restored_from_json.emails_size() << std::endl;

    // ---- 验证数据一致性 ----
    if (original.name() == restored_from_bin.name() &&
        original.age() == restored_from_bin.age() &&
        original.emails_size() == restored_from_bin.emails_size() &&
        original.name() == restored_from_json.name() &&
        original.age() == restored_from_json.age() &&
        original.emails_size() == restored_from_json.emails_size()) {
        std::cout << "\nAll tests passed!" << std::endl;
    } else {
        std::cerr << "\nTest failed: data mismatch." << std::endl;
        return 1;
    }

    return 0;
}