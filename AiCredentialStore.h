#pragma once
#include <QByteArray>
#include <QString>

class AiCredentialStore {
public:
    explicit AiCredentialStore(QString target = QStringLiteral("FileManager/DeepSeek/APIKey/v1"));
    QByteArray savedKey(QString* error = nullptr) const;
    QByteArray effectiveKey(QString* source = nullptr, QString* error = nullptr) const;
    bool save(const QByteArray& key, QString* error = nullptr) const;
    bool clear(QString* error = nullptr) const;
    static bool validKey(const QByteArray& key);
    static void wipe(QByteArray& key);
private:
    QString target_;
};
