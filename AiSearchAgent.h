#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QPointer>
#include <QTimer>
#include <functional>
class QNetworkAccessManager;
class QNetworkReply;

// Immutable metadata only. This class has no filesystem, file-content or write API.
class AiSearchAgent : public QObject {
public:
    explicit AiSearchAgent(QJsonArray snapshot, QNetworkAccessManager* network, QObject* parent = nullptr);
    ~AiSearchAgent() override;
    std::function<void(QString)> progress;
    std::function<void(QString, QJsonArray)> completed;
    std::function<void(QString)> failed;
    void start(const QString& question, QByteArray key, const QString& model);
    void cancel();
    QJsonObject executeTool(const QString& name, const QJsonObject& args);
    static QJsonArray tools();
private:
    QJsonArray snapshot_, messages_;
    QSet<QString> observed_;
    QNetworkAccessManager* network_;
    QPointer<QNetworkReply> reply_;
    QTimer deadline_;
    QByteArray key_, response_;
    QString model_;
    int rounds_ = 0, calls_ = 0;
    bool running_ = false, oversized_ = false, queried_ = false;
    void request();
    void consume(const QJsonObject& message);
    void fail(const QString& message);
    void clearSecret();
};
