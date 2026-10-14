#include "PrivacyCrypto.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <climits>
#include <memory>

namespace PrivacyCrypto {

void wipe(std::string& sensitive)
{
    if (!sensitive.empty())
        OPENSSL_cleanse(&sensitive[0], sensitive.size());
    sensitive.clear();
}

bool randomBytes(std::string& result, std::size_t length)
{
    if (length > static_cast<std::size_t>(INT_MAX)) return false;
    result.resize(length);
    if (length && RAND_bytes(reinterpret_cast<unsigned char*>(&result[0]),
                             static_cast<int>(length)) != 1) {
        wipe(result);
        return false;
    }
    return true;
}

bool deriveKey(const std::string& passwordUtf8,
               const std::string& salt, int iterations,
               std::string& resultKey)
{
    if (salt.size() != kSaltSize || iterations < 100000 ||
        iterations > 10000000 || passwordUtf8.size() > INT_MAX) return false;
    std::string derived(kKeySize, '\0');
    const int ok = PKCS5_PBKDF2_HMAC(passwordUtf8.data(),
        static_cast<int>(passwordUtf8.size()),
        reinterpret_cast<const unsigned char*>(salt.data()),
        static_cast<int>(salt.size()), iterations, EVP_sha256(),
        static_cast<int>(kKeySize),
        reinterpret_cast<unsigned char*>(&derived[0]));
    if (ok != 1) { wipe(derived); return false; }
    wipe(resultKey);
    resultKey.swap(derived);
    return true;
}

bool encrypt(const std::string& plaintext, const std::string& key,
             std::string& packed)
{
    if (key.size() != kKeySize || plaintext.size() > INT_MAX) return false;
    std::string nonce;
    if (!randomBytes(nonce, kNonceSize)) return false;
    using Ctx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
    Ctx ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx) return false;
    std::string ciphertext(plaintext.size() + kTagSize, '\0');
    int bytes = 0, finalBytes = 0;
    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr,
                           nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN,
                            static_cast<int>(kNonceSize), nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr,
            reinterpret_cast<const unsigned char*>(key.data()),
            reinterpret_cast<const unsigned char*>(nonce.data())) != 1 ||
        EVP_EncryptUpdate(ctx.get(),
            reinterpret_cast<unsigned char*>(&ciphertext[0]), &bytes,
            reinterpret_cast<const unsigned char*>(plaintext.data()),
            static_cast<int>(plaintext.size())) != 1 ||
        EVP_EncryptFinal_ex(ctx.get(),
            reinterpret_cast<unsigned char*>(&ciphertext[0]) + bytes,
            &finalBytes) != 1)
        return false;
    ciphertext.resize(static_cast<std::size_t>(bytes + finalBytes));
    unsigned char tag[kTagSize] = {};
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG,
                            static_cast<int>(kTagSize), tag) != 1)
        return false;
    std::string output;
    output.reserve(kNonceSize + ciphertext.size() + kTagSize);
    output += nonce;
    output += ciphertext;
    output.append(reinterpret_cast<const char*>(tag), kTagSize);
    packed.swap(output);
    return true;
}

bool decrypt(const std::string& packed, const std::string& key,
             std::string& plaintext)
{
    if (key.size() != kKeySize || packed.size() < kNonceSize + kTagSize ||
        packed.size() - kNonceSize - kTagSize > INT_MAX) return false;
    const std::size_t cipherLen = packed.size() - kNonceSize - kTagSize;
    const unsigned char* raw = reinterpret_cast<const unsigned char*>(packed.data());
    using Ctx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
    Ctx ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx) return false;
    std::string output(cipherLen + kTagSize, '\0');
    int bytes = 0, finalBytes = 0;
    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr,
                           nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN,
                            static_cast<int>(kNonceSize), nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr,
            reinterpret_cast<const unsigned char*>(key.data()), raw) != 1 ||
        EVP_DecryptUpdate(ctx.get(),
            reinterpret_cast<unsigned char*>(&output[0]), &bytes,
            raw + kNonceSize, static_cast<int>(cipherLen)) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG,
            static_cast<int>(kTagSize),
            const_cast<unsigned char*>(raw + kNonceSize + cipherLen)) != 1 ||
        EVP_DecryptFinal_ex(ctx.get(),
            reinterpret_cast<unsigned char*>(&output[0]) + bytes,
            &finalBytes) != 1) {
        wipe(output);
        return false;
    }
    output.resize(static_cast<std::size_t>(bytes + finalBytes));
    plaintext.swap(output);
    return true;
}
}
