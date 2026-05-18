#include "crypto.h"



#include <openssl/evp.h>
#include <vector>

constexpr size_t kGcmTagSize = 16;

// 根据密钥长度选择对应 AES-GCM 算法
const EVP_CIPHER* SelectCipher(size_t key_size) {
    switch (key_size) {
    case 16:
        return EVP_aes_128_gcm();
    case 24:
        return EVP_aes_192_gcm();
    case 32:
        return EVP_aes_256_gcm();
    default:
        return nullptr;
    }
}

// 执行 AES-GCM 加密，输出密文 + tag
bool EncryptAesGcm(const std::string& key, std::string_view iv, std::string_view plaintext, std::string& ciphertext,
                   std::string* error) {
    const EVP_CIPHER* cipher = SelectCipher(key.size());
    if (cipher == nullptr) {
        if (error != nullptr) {
            *error = "unsupported AES-GCM key size";
        }
        return false;
    }
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) {
        if (error != nullptr) {
            *error = "failed to allocate cipher ctx";
        }
        return false;
    }

    bool ok = true;
    int len = 0;
    std::vector<unsigned char> out(plaintext.size() + kGcmTagSize + EVP_CIPHER_block_size(cipher));
    do {
        if (EVP_EncryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) != 1) {
            ok = false;
            break;
        }
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) != 1) {
            ok = false;
            break;
        }
        if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.data()),
                               reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
            ok = false;
            break;
        }
        if (EVP_EncryptUpdate(ctx, out.data(), &len, reinterpret_cast<const unsigned char*>(plaintext.data()),
                              static_cast<int>(plaintext.size())) != 1) {
            ok = false;
            break;
        }
        int total = len;
        if (EVP_EncryptFinal_ex(ctx, out.data() + total, &len) != 1) {
            ok = false;
            break;
        }
        total += len;
        unsigned char tag[kGcmTagSize];
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kGcmTagSize, tag) != 1) {
            ok = false;
            break;
        }
        ciphertext.assign(reinterpret_cast<char*>(out.data()), static_cast<size_t>(total));
        ciphertext.append(reinterpret_cast<const char*>(tag), kGcmTagSize);
    } while (false);

    if (!ok && error != nullptr) {
        *error = "aes-gcm encrypt failed";
    }
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

// 执行 AES-GCM 解密，校验 tag
bool DecryptAesGcm(const std::string& key, std::string_view iv, std::string_view ciphertext, std::string& plaintext,
                   std::string* error) {
    if (ciphertext.size() < kGcmTagSize) {
        if (error != nullptr) {
            *error = "ciphertext too short";
        }
        return false;
    }
    const EVP_CIPHER* cipher = SelectCipher(key.size());
    if (cipher == nullptr) {
        if (error != nullptr) {
            *error = "unsupported AES-GCM key size";
        }
        return false;
    }

    const size_t body_size = ciphertext.size() - kGcmTagSize;
    const unsigned char* tag = reinterpret_cast<const unsigned char*>(ciphertext.data() + body_size);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) {
        if (error != nullptr) {
            *error = "failed to allocate cipher ctx";
        }
        return false;
    }

    bool ok = true;
    int len = 0;
    std::vector<unsigned char> out(body_size + EVP_CIPHER_block_size(cipher));
    do {
        if (EVP_DecryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) != 1) {
            ok = false;
            break;
        }
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) != 1) {
            ok = false;
            break;
        }
        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.data()),
                               reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
            ok = false;
            break;
        }
        if (EVP_DecryptUpdate(ctx, out.data(), &len, reinterpret_cast<const unsigned char*>(ciphertext.data()),
                              static_cast<int>(body_size)) != 1) {
            ok = false;
            break;
        }
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kGcmTagSize, const_cast<unsigned char*>(tag)) != 1) {
            ok = false;
            break;
        }
        int total = len;
        if (EVP_DecryptFinal_ex(ctx, out.data() + total, &len) != 1) {
            ok = false;
            break;
        }
        total += len;
        plaintext.assign(reinterpret_cast<char*>(out.data()), static_cast<size_t>(total));
    } while (false);

    if (!ok && error != nullptr) {
        *error = "aes-gcm decrypt failed";
    }
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}



// CryptoProvider 的加密包装
bool CryptoProvider::Encrypt(std::string_view plaintext, std::string_view iv, std::string& ciphertext, std::string* error) const {
    return EncryptAesGcm(key_, iv, plaintext, ciphertext, error);
}

// CryptoProvider 的解密包装
bool CryptoProvider::Decrypt(std::string_view ciphertext, std::string_view iv, std::string& plaintext, std::string* error) const {
    return DecryptAesGcm(key_, iv, ciphertext, plaintext, error);
}

// 统一加密入口，根据 EncryptionType 分发
bool Encrypt(EncryptionType type, std::string_view key, std::string_view iv, std::string_view plaintext, std::string& ciphertext,
             std::string* error) {
    switch (type) {
    case EncryptionType::None:
        ciphertext.assign(plaintext.data(), plaintext.size());
        return true;
    case EncryptionType::AES:
        return EncryptAesGcm(std::string(key), iv, plaintext, ciphertext, error);
    default:
        if (error != nullptr) {
            *error = "unknown encryption type";
        }
        return false;
    }
}

// 统一解密入口
bool Decrypt(EncryptionType type, std::string_view key, std::string_view iv, std::string_view ciphertext, std::string& plaintext,
             std::string* error) {
    switch (type) {
    case EncryptionType::None:
        plaintext.assign(ciphertext.data(), ciphertext.size());
        return true;
    case EncryptionType::AES:
        return DecryptAesGcm(std::string(key), iv, ciphertext, plaintext, error);
    default:
        if (error != nullptr) {
            *error = "unknown encryption type";
        }
        return false;
    }

             }

