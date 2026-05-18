#pragma once

#include <string>
#include <string_view>

#include "protocol.h"



class CryptoProvider {
public:
    // 创建加解密提供者，内部保存对称密钥
    explicit CryptoProvider(std::string key) : key_(std::move(key)) {}

    // 使用 AES-GCM 加密
    bool Encrypt(std::string_view plaintext, std::string_view iv, std::string& ciphertext, std::string* error) const;

    // 使用 AES-GCM 解密
    bool Decrypt(std::string_view ciphertext, std::string_view iv, std::string& plaintext, std::string* error) const;

private:
    std::string key_;  // 对称密钥
};

// 根据类型选择加密算法，加密明文
bool Encrypt(EncryptionType type, std::string_view key, std::string_view iv, std::string_view plaintext, std::string& ciphertext,
             std::string* error);

// 根据类型选择解密算法，解密密文
bool Decrypt(EncryptionType type, std::string_view key, std::string_view iv, std::string_view ciphertext, std::string& plaintext,
             std::string* error);