#include "../PrivacyCrypto.h"
#include <iostream>
#include <cstdlib>
void check(bool b) { if (!b) std::exit(1); }
int main() {
    std::string salt, key, wrong, packed, decoded;
    check(PrivacyCrypto::randomBytes(salt, 16));
    check(PrivacyCrypto::deriveKey("correct", salt, 300000, key));
    check(PrivacyCrypto::deriveKey("wrong", salt, 300000, wrong));
    std::string text("private\0text", 12);
    check(PrivacyCrypto::encrypt(text, key, packed));
    check(PrivacyCrypto::decrypt(packed, key, decoded) && decoded == text);
    check(!PrivacyCrypto::decrypt(packed, wrong, decoded));
    packed.back() ^= 1;
    check(!PrivacyCrypto::decrypt(packed, key, decoded));
    check(!PrivacyCrypto::decrypt("short", key, decoded));
    check(PrivacyCrypto::encrypt("", key, packed));
    check(PrivacyCrypto::decrypt(packed, key, decoded) && decoded.empty());
    PrivacyCrypto::wipe(key); check(key.empty());
    std::cout << "[PASS] AES-GCM roundtrip, wrong key, tampering, truncation, empty data, wipe\n";
}
