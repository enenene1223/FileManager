#include "AiCredentialStore.h"
#include <utility>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#endif

AiCredentialStore::AiCredentialStore(QString target) : target_(std::move(target)) {}

bool AiCredentialStore::validKey(const QByteArray& key) {
    if (key.isEmpty() || key.size() > 2048) return false;
    for (auto c : key) if (static_cast<unsigned char>(c) < 33 || static_cast<unsigned char>(c) > 126) return false;
    return true;
}

void AiCredentialStore::wipe(QByteArray& key) {
#ifdef Q_OS_WIN
    if (!key.isEmpty()) SecureZeroMemory(key.data(), static_cast<SIZE_T>(key.size()));
#else
    volatile char* bytes = key.data(); for (qsizetype i = 0; i < key.size(); ++i) bytes[i] = 0;
#endif
    key.clear();
}

QByteArray AiCredentialStore::savedKey(QString* error) const {
    if (error) error->clear();
#ifdef Q_OS_WIN
    PCREDENTIALW credential = nullptr; const auto target = target_.toStdWString();
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        auto code = GetLastError();
        if (code != ERROR_NOT_FOUND && error) *error = QString("无法读取 Windows 安全存储（错误码 %1）。").arg(code);
        return {};
    }
    QByteArray result(reinterpret_cast<const char*>(credential->CredentialBlob), static_cast<qsizetype>(credential->CredentialBlobSize));
    if (credential->CredentialBlobSize) SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
    if (!validKey(result)) { wipe(result); if (error) *error = "已保存的密钥格式无效，请重新保存。"; }
    return result;
#else
    if (error) *error = "当前平台不支持 Windows 凭据管理器，可使用环境变量。";
    return {};
#endif
}

QByteArray AiCredentialStore::effectiveKey(QString* source, QString* error) const {
    if (source) source->clear();
    auto key = savedKey(error);
    if (!key.isEmpty()) { if (source) *source = "Windows 凭据管理器"; return key; }
    auto environment = qgetenv("DEEPSEEK_API_KEY").trimmed();
    if (!environment.isEmpty() && !validKey(environment)) { wipe(environment); if (error) *error = "环境变量中的密钥格式无效。"; return {}; }
    if (!environment.isEmpty() && source) *source = "Windows 环境变量";
    return environment;
}

bool AiCredentialStore::save(const QByteArray& key, QString* error) const {
    if (error) error->clear();
    if (!validKey(key)) { if (error) *error = "请输入有效密钥，不能包含空格、换行或非 ASCII 字符。"; return false; }
#ifdef Q_OS_WIN
    auto target = target_.toStdWString(); wchar_t user[] = L"DeepSeek API";
    CREDENTIALW credential{}; credential.Type = CRED_TYPE_GENERIC; credential.TargetName = target.data();
    credential.CredentialBlobSize = static_cast<DWORD>(key.size()); credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(key.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE; credential.UserName = user;
    if (CredWriteW(&credential, 0)) return true;
    if (error) *error = QString("保存到 Windows 凭据管理器失败（错误码 %1），未保存为普通文件。").arg(GetLastError());
#else
    if (error) *error = "当前平台不支持安全保存，可使用环境变量。";
#endif
    return false;
}

bool AiCredentialStore::clear(QString* error) const {
    if (error) error->clear();
#ifdef Q_OS_WIN
    auto target = target_.toStdWString();
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) return true;
    auto code = GetLastError(); if (code == ERROR_NOT_FOUND) return true;
    if (error) *error = QString("清除本机密钥失败（错误码 %1）。").arg(code);
#else
    if (error) *error = "当前平台不支持 Windows 凭据管理器。";
#endif
    return false;
}
