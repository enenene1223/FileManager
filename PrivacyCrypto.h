#ifndef PRIVACYCRYPTO_H
#define PRIVACYCRYPTO_H

#include <cstddef>
#include <string>

// OpenSSL-backed AES-256-GCM. All inputs/outputs are raw bytes in std::string.
// E1 disk representation is Base64(nonce[12] || ciphertext || tag[16]).
namespace PrivacyCrypto {
constexpr int kIterations = 300000;
constexpr std::size_t kSaltSize = 16;
constexpr std::size_t kKeySize = 32;
constexpr std::size_t kNonceSize = 12;
constexpr std::size_t kTagSize = 16;

bool randomBytes(std::string& result, std::size_t length);
bool deriveKey(const std::string& passwordUtf8,
               const std::string& salt, int iterations,
               std::string& resultKey);
bool encrypt(const std::string& plaintext, const std::string& key,
             std::string& packed);
bool decrypt(const std::string& packed, const std::string& key,
             std::string& plaintext);
void wipe(std::string& sensitive);
}

#endif // PRIVACYCRYPTO_H
