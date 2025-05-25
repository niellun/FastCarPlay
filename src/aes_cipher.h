#ifndef SRC_AES_CIPHER
#define SRC_AES_CIPHER

#include <array>
#include <cstdint>
#include <string>
#include <memory>

class AESCipher
{
public:
    static constexpr size_t keyLength = 16;

    AESCipher(const std::string &base_key);
    ~AESCipher() = default;

    bool Encrypt(uint8_t *data, uint16_t length) const;
    bool Decrypt(uint8_t *data, uint16_t length) const;

    uint32_t Seed() const { return _seed; }
    const std::string& Key() const { return _baseKey; }

private:
    std::string _baseKey;
    uint32_t _seed;
    std::array<uint8_t, keyLength> _encKey;
    std::array<uint8_t, keyLength> _initVec;
};

#endif /* SRC_AES_CIPHER */
