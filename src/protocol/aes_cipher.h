#ifndef SRC_AES_CIPHER
#define SRC_AES_CIPHER

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

class AESCipher
{
public:
    static constexpr size_t keyLength = 16;

    AESCipher(const std::string &base_key);
    ~AESCipher() = default;

    bool encrypt(uint8_t *data, uint32_t length, char *err) const;
    bool decrypt(uint8_t *data, uint32_t length, char *err) const;

    uint32_t seed() const { return _seed; }
    const std::string &key() const { return _baseKey; }

    static bool error(char *error, const char *message)
    {
        if (error)
            strcpy(error, message);
        return false;
    }

private:
    std::string _baseKey;
    uint32_t _seed;
    std::array<uint8_t, keyLength> _encKey;
    std::array<uint8_t, keyLength> _initVec;
};

#endif /* SRC_AES_CIPHER */
