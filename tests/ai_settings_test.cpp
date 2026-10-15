#include "AiCredentialStore.h"
#include "AiSettingsDialog.h"
#include "AppTheme.h"
#include "AiTextSummary.h"
#include <QApplication>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QUuid>
#include <QFontDatabase>
#include <QProcess>
#include <QCryptographicHash>
#include <iostream>
#include <cstdlib>

static void check(bool b, const char* message) { if (!b) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); } }
static QString cleanupTarget;
class FakeReply : public QNetworkReply {
    QByteArray body_; qint64 offset_ = 0; bool done_ = false;
    void complete(NetworkError error) {
        if (done_) return; done_ = true;
        if (error != NoError) setError(error, "test response intentionally omitted");
        setFinished(true); emit readyRead(); emit finished();
    }
public:
    FakeReply(const QNetworkRequest& req, int code, QByteArray body, NetworkError error, QObject* parent) : QNetworkReply(parent), body_(std::move(body)) {
        setRequest(req); setUrl(req.url()); setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, code); open(QIODevice::ReadOnly);
        QTimer::singleShot(10, this, [this, error] { complete(error); });
    }
    void abort() override { complete(OperationCanceledError); }
    qint64 bytesAvailable() const override { return body_.size() - offset_ + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char* data, qint64 max) override {
        auto count = qMin<qint64>(max, body_.size() - offset_); if (!count) return -1;
        memcpy(data, body_.constData() + offset_, static_cast<size_t>(count)); offset_ += count; return count;
    }
};
class FakeNetwork : public QNetworkAccessManager {
public:
    int code = 200; QByteArray body = "{\"data\":[{\"id\":\"deepseek-chat\"}]}";
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QByteArray expected; int calls = 0;
protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& req, QIODevice* data) override {
        check(operation == GetOperation && !data, "test sends GET without file data");
        check(req.url() == QUrl("https://api.deepseek.com/models"), "fixed HTTPS model-list endpoint");
        check(req.rawHeader("Authorization") == "Bearer " + expected, "correct credential source");
        check(req.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy, "test does not forward key through redirects");
        ++calls; return new FakeReply(req, code, body, error, this);
    }
};
#include "agent_search_cases.h"
static void waitFor(QPushButton* button) {
    QElapsedTimer timer; timer.start();
    while (!button->isEnabled() && timer.elapsed() < 2000) { QApplication::processEvents(); QThread::msleep(1); }
    check(button->isEnabled(), "async connection test completes");
}

static void summaryCases() {
    const auto empty = AiSummaryInput::prepare("空白.txt", "\n \t");
    check(empty.excerpt.trimmed().isEmpty(), "blank summary source recognised without request");
    const auto shortText = AiSummaryInput::prepare("点名记录.txt", "到课3人。");
    check(shortText.included == shortText.total && shortText.coverage().contains("全文"), "short summary declares full coverage");
    const auto unicode = AiSummaryInput::prepare("长文.txt", QString::fromUtf8("😀").repeated(12001));
    check(unicode.total == 12001 && unicode.included == 12000 && unicode.excerpt.toUcs4().size() == 12000
          && unicode.coverage().contains("后续内容未处理"), "summary Unicode-safe limit and honest partial coverage");
    QJsonObject result{{"summary", "记录本次到课情况。"}, {"points", QJsonArray{"到课3人"}}, {"keywords", QJsonArray{"点名", "出勤"}}, {"extra", ""}};
    QString formatted;
    check(ai_format_summary(shortText, result, &formatted) && formatted.contains("文件：点名记录.txt") && formatted.contains("一句话概括")
          && formatted.contains("主要内容") && formatted.contains("关键词：点名、出勤") && !formatted.contains("其他明确信息"), "structured summary with source and no forced tasks");
    result["extra"] = "明确到课人数为3人";
    check(ai_format_summary(unicode, result, &formatted) && formatted.contains("后续内容未处理") && formatted.contains("其他明确信息"), "partial coverage cannot be hidden by model output");
    result["points"] = QJsonArray{17};
    check(!ai_format_summary(shortText, result, &formatted) && formatted.isEmpty(), "invalid summary clears copyable result");
    result["points"] = QJsonArray{}; result["keywords"] = QJsonArray{}; result["extra"] = "";
    check(ai_format_summary(shortText, result, &formatted), "minimal short-text summary does not invent extra points");
    result["summary"] = ""; check(!ai_format_summary(shortText, result, &formatted), "empty AI summary rejected");
}
static int summaryLiveSmoke() {
    AiCredentialStore store; auto key = store.effectiveKey();
    if (key.isEmpty()) { std::cout << "[SKIP] No key; summary live test not sent.\n"; return 0; }
    const auto source = AiSummaryInput::prepare("虚构点名记录.txt", "本次课程应到4人，实到3人，1人请假。未提供具体日期和人员姓名。");
    QNetworkAccessManager network; QNetworkRequest request(QUrl("https://api.deepseek.com/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"); request.setRawHeader("Authorization", "Bearer " + key);
    request.setTransferTimeout(20000); request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QJsonObject body{{"model", QString::fromUtf8(qgetenv("DEEPSEEK_MODEL").isEmpty() ? QByteArray("deepseek-chat") : qgetenv("DEEPSEEK_MODEL"))},
        {"stream", false}, {"max_tokens", 1500}, {"temperature", 0.2}, {"response_format", QJsonObject{{"type", "json_object"}}},
        {"messages", QJsonArray{QJsonObject{{"role", "system"}, {"content", ai_summary_instructions()}},
            QJsonObject{{"role", "user"}, {"content", "文件名：" + source.name + "\n覆盖范围：" + source.coverage() + "\n正文：\n" + source.excerpt}}}}};
    auto reply = network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)); AiCredentialStore::wipe(key);
    QEventLoop loop; QTimer timer; timer.setSingleShot(true); timer.start(20000);
    QObject::connect(&timer, &QTimer::timeout, reply, [reply] { reply->setProperty("timeout", true); reply->abort(); });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit); loop.exec(); timer.stop();
    const auto failure = ai_chat_error(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->error(), reply->property("timeout").toBool());
    if (!failure.isEmpty()) { std::cout << "[FAIL] " << failure.toStdString() << '\n'; return 1; }
    const auto choices = QJsonDocument::fromJson(reply->readAll()).object().value("choices").toArray();
    const auto text = choices.isEmpty() ? QString() : choices[0].toObject().value("message").toObject().value("content").toString();
    QString formatted;
    if (choices.isEmpty() || choices[0].toObject().value("finish_reason") == "length" || !ai_format_summary(source, QJsonDocument::fromJson(text.toUtf8()).object(), &formatted)) {
        std::cout << "[FAIL] Live synthetic summary format invalid.\n"; return 1;
    }
    std::cout << "[PASS] Live synthetic TXT summary returned valid structure and local coverage notice.\n"; return 0;
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    auto args = app.arguments();
    if (args.size() == 4 && args[1] == "--read-credential") {
        AiCredentialStore store(args[2]); auto value = store.savedKey();
        check(!value.isEmpty() && QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex()) == args[3], "credential survives a new process");
        AiCredentialStore::wipe(value); return 0;
    }
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc"); app.setFont(QFont("Microsoft YaHei", 10)); app.setStyleSheet(app_theme());
    if (args.contains("--summary-live-smoke")) return summaryLiveSmoke();
    if (args.contains("--agent-live-smoke")) return agentLiveSmoke();
    summaryCases();
    agentSearchCases();
    const QString target = "FileManager/Tests/AiSettings/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    AiCredentialStore store(target); QString error, source;
    cleanupTarget = target;
    std::atexit([] { if (!cleanupTarget.isEmpty()) AiCredentialStore(cleanupTarget).clear(); });
    qunsetenv("DEEPSEEK_API_KEY");
    check(store.savedKey(&error).isEmpty(), "unique test credential initially absent");
    QByteArray key = "test-only-" + QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
    QByteArray env = "test-only-env-" + QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
    check(!AiCredentialStore::validKey("invalid key") && !AiCredentialStore::validKey("bad\nkey") && !AiCredentialStore::validKey(QByteArray(2050, 'x')), "invalid/header-injection input rejected");
    if (!store.save(key, &error)) { std::cerr << error.toStdString() << '\n'; check(false, "Windows Credential Manager save"); }
    check(store.savedKey() == key, "secure storage roundtrip");
    QProcess child; child.start(QCoreApplication::applicationFilePath(), {"--read-credential", target, QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex())});
    check(child.waitForFinished(10000) && child.exitCode() == 0, "restart reads saved credential");
    qputenv("DEEPSEEK_API_KEY", env);
    check(store.effectiveKey(&source) == key && source == "Windows 凭据管理器", "saved key has priority");
    FakeNetwork network; network.expected = key;
    AiSettingsDialog dialog(nullptr, store, &network); dialog.show(); QApplication::processEvents();
    auto edit = dialog.findChild<QLineEdit*>("api_key_edit"); auto result = dialog.findChild<QLabel*>("ai_connection_result");
    auto test = dialog.findChild<QPushButton*>("test_ai_connection");
    check(edit && edit->echoMode() == QLineEdit::Password && edit->text().isEmpty(), "saved key never prefilled or revealed");
    test->click(); waitFor(test); check(result->text().contains("连接成功"), "successful connection message");
    network.code = 401; network.body = key; network.error = QNetworkReply::AuthenticationRequiredError;
    test->click(); waitFor(test); check(result->text().contains("认证失败") && !result->text().contains(QString::fromUtf8(key)), "auth failure redacts response");
    network.code = 429; test->click(); waitFor(test); check(result->text().contains("过于频繁"), "rate limit handling");
    network.code = 503; test->click(); waitFor(test); check(result->text().contains("暂时不可用"), "server failure handling");
    network.code = 0; network.error = QNetworkReply::TimeoutError; test->click(); waitFor(test); check(result->text().contains("超时"), "timeout handling");
    network.code = 200; network.error = QNetworkReply::NoError; network.body = "invalid json";
    test->click(); waitFor(test); check(result->text().contains("格式异常"), "bad response handling");
    auto replacement = "test-only-replacement-" + QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
    edit->setText(QString::fromUtf8(replacement)); dialog.findChild<QCheckBox*>("show_ai_key")->setChecked(true);
    dialog.findChild<QPushButton*>("save_ai_key")->click();
    check(store.savedKey() == replacement && edit->text().isEmpty() && edit->echoMode() == QLineEdit::Password, "replace key and restore hidden empty field");
    dialog.grab().save("ai-service-settings.png");
    dialog.findChild<QPushButton*>("clear_ai_key")->click();
    check(store.savedKey().isEmpty() && store.effectiveKey(&source) == env && source == "Windows 环境变量", "clear falls back to environment");
    network.expected = env; network.code = 200; network.body = "{\"data\":[]}";
    test->click(); waitFor(test); check(result->text().contains("连接成功"), "environment fallback connection test");
    qunsetenv("DEEPSEEK_API_KEY"); auto before = network.calls; test->click(); check(network.calls == before && result->text().contains("尚未填写"), "no key sends no request");
    check(store.clear(), "clearing absent credential is safe");
    check(!store.save("bad\r\nheader"), "bad credential is never stored");
    check(store.savedKey().isEmpty(), "test credential removed");
    dialog.reject(); AiCredentialStore::wipe(key); AiCredentialStore::wipe(env); AiCredentialStore::wipe(replacement);
    std::cout << "[PASS] Windows credential store/restart/replace/delete, environment fallback, masked UI, async connection/errors/redaction, no-key offline fallback\n";
}
