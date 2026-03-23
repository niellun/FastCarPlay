#include "aes_cipher.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <stdexcept>

AESCipher::AESCipher(const std::string &baseKey)
    : _baseKey(baseKey)
{
    std::srand(std::time(nullptr));
    _seed = static_cast<uint32_t>(std::rand());

    if (_baseKey.size() != keyLength)
    {
        throw std::invalid_argument("Base key must be exactly 16 bytes");
    }

    for (uint8_t i = 0; i < keyLength; ++i)
    {
        _encKey[i] = static_cast<uint8_t>(_baseKey[(_seed + i) % keyLength]);
    }

    _initVec.fill(0);
    _initVec[1] = static_cast<uint8_t>(_seed);
    _initVec[4] = static_cast<uint8_t>(_seed >> 8);
    _initVec[9] = static_cast<uint8_t>(_seed >> 16);
    _initVec[12] = static_cast<uint8_t>(_seed >> 24);
}

Status AESCipher::Encrypt(uint8_t *data, uint32_t length) const
{
    if (!data || length == 0)
        return Status::Error("Empty data");

    auto ctx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx)
        return Status::Error("Failed to create cipher context");

    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_cfb(), nullptr, _encKey.data(), _initVec.data()) != 1)
        return Status::Error("Encryption initialization failed");

    std::unique_ptr<uint8_t[]> temp(new uint8_t[length + AES_BLOCK_SIZE]);
    int out_len = 0;
    if (EVP_EncryptUpdate(ctx.get(), temp.get(), &out_len, data, length) != 1)
        return Status::Error("Encryption failed during update");

    int final_len = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), temp.get() + out_len, &final_len) != 1)
        return Status::Error("Encryption failed during final");

    std::copy_n(temp.get(), length, data);
    return Status::Success();
}

Status AESCipher::Decrypt(uint8_t *data, uint32_t length) const
{
    if (!data || length == 0)
        return Status::Error("Empty data");

    auto ctx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx)
        return Status::Error("Failed to create cipher context");

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_cfb(), nullptr, _encKey.data(), _initVec.data()) != 1)
        return Status::Error(" Decryption initialization failed");

    std::unique_ptr<uint8_t[]> temp(new uint8_t[length + AES_BLOCK_SIZE]);
    int out_len = 0;
    if (EVP_DecryptUpdate(ctx.get(), temp.get(), &out_len, data, length) != 1)
        return Status::Error("Decryption failed during update");

    int final_len = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), temp.get() + out_len, &final_len) != 1)
        return Status::Error("Decryption failed during final");

    std::copy_n(temp.get(), length, data);
    return Status::Success();
}
