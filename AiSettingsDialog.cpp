#include "AiSettingsDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

AiSettingsDialog::AiSettingsDialog(QWidget* parent, AiCredentialStore store, QNetworkAccessManager* network) : QDialog(parent) {
    setWindowTitle("AI 服务设置"); setObjectName("ai_settings_dialog"); resize(590, 330);
    auto layout = new QVBoxLayout(this); auto form = new QFormLayout;
    auto info = new QLabel("DeepSeek · https://api.deepseek.com\n密钥保存在当前 Windows 用户的凭据管理器中，不写入项目或安装目录。", this);
    info->setWordWrap(true); layout->addWidget(info);
    auto key = new QLineEdit(this); key->setObjectName("api_key_edit"); key->setEchoMode(QLineEdit::Password); key->setMaxLength(2048);
    key->setPlaceholderText("输入自己的 DeepSeek API Key"); key->setClearButtonEnabled(true); form->addRow("API Key", key);
    auto show = new QCheckBox("显示本次输入的密钥", this); show->setObjectName("show_ai_key"); form->addRow(show);
    layout->addLayout(form);
    auto status = new QLabel(this); status->setObjectName("ai_key_status"); status->setWordWrap(true); status->setTextFormat(Qt::PlainText); layout->addWidget(status);
    auto refresh = [key, status, store] {
        QString source, error; auto value = store.effectiveKey(&source, &error);
        status->setText(value.isEmpty() ? "当前未配置可用密钥。离线功能可正常使用。" + (error.isEmpty() ? QString() : "\n" + error) : "当前配置来源：" + source + "。本机保存的密钥优先，环境变量作为兼容回退。");
        auto saved = store.savedKey(); key->setPlaceholderText(saved.isEmpty() ? "输入自己的 DeepSeek API Key" : "已安全保存；输入新密钥可替换（不会回显旧密钥）");
        AiCredentialStore::wipe(saved); AiCredentialStore::wipe(value);
    };
    refresh();
    auto buttons = new QHBoxLayout; auto save = new QPushButton("安全保存", this); save->setObjectName("save_ai_key");
    auto clear = new QPushButton("清除已保存密钥", this); clear->setObjectName("clear_ai_key");
    auto test = new QPushButton("测试连接（不保存）", this); test->setObjectName("test_ai_connection");
    buttons->addWidget(save); buttons->addWidget(clear); buttons->addWidget(test); layout->addLayout(buttons);
    auto result = new QLabel("测试只请求模型列表，不上传文件内容。清除本机密钥不会删除环境变量。", this);
    result->setObjectName("ai_connection_result"); result->setTextFormat(Qt::PlainText); result->setWordWrap(true); layout->addWidget(result);
    auto close = new QDialogButtonBox(QDialogButtonBox::Close, this); layout->addWidget(close);
    close->button(QDialogButtonBox::Close)->setText("关闭");
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(show, &QCheckBox::toggled, this, [key](bool visible) { key->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password); });
    connect(save, &QPushButton::clicked, this, [=] {
        auto value = key->text().trimmed().toUtf8(); QString error;
        bool success = store.save(value, &error); AiCredentialStore::wipe(value);
        if (!success) { result->setText(error); return; }
        key->clear(); show->setChecked(false); refresh(); result->setText("已安全保存。连接测试可用于验证密钥是否有效。");
    });
    connect(clear, &QPushButton::clicked, this, [=] {
        QString error;
        if (!store.clear(&error)) { result->setText(error); return; }
        key->clear(); show->setChecked(false); refresh(); result->setText("已清除本机保存的密钥。若仍配置了环境变量，将使用环境变量。");
    });
    if (!network) network = new QNetworkAccessManager(this);
    connect(test, &QPushButton::clicked, this, [=] {
        QString error; auto value = key->text().trimmed().toUtf8();
        if (value.isEmpty()) value = store.effectiveKey(nullptr, &error);
        if (!AiCredentialStore::validKey(value)) { AiCredentialStore::wipe(value); result->setText("尚未填写或配置有效密钥。" + error); return; }
        QNetworkRequest request(QUrl("https://api.deepseek.com/models"));
        request.setRawHeader("Authorization", "Bearer " + value); request.setTransferTimeout(15000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        auto reply = network->get(request); AiCredentialStore::wipe(value);
        for (auto button : {test, save, clear}) button->setEnabled(false);
        key->setEnabled(false); result->setText("正在测试连接…最多等待15秒，可关闭窗口取消。");
        auto deadline = new QTimer(reply); deadline->setSingleShot(true); deadline->start(15000);
        connect(deadline, &QTimer::timeout, reply, [reply] { reply->setProperty("test_timeout", true); reply->abort(); });
        connect(this, &QDialog::finished, reply, [reply] { reply->abort(); });
        connect(reply, &QNetworkReply::finished, this, [=] {
            deadline->stop(); for (auto button : {test, save, clear}) button->setEnabled(true); key->setEnabled(true);
            int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); auto bytes = reply->readAll();
            // Never show the raw response or errorString: a server/proxy could echo credentials.
            if (reply->property("test_timeout").toBool() || reply->error() == QNetworkReply::TimeoutError) result->setText("连接超时，请检查网络。离线功能仍可使用。");
            else if (code == 401 || code == 403) result->setText("认证失败，请检查 API Key 是否正确、有效及有访问权限。");
            else if (code == 402) result->setText("服务提示账户余额不足，请检查 DeepSeek 账户。");
            else if (code == 429) result->setText("请求过于频繁，请稍后再试。");
            else if (code >= 500) result->setText("DeepSeek 服务暂时不可用，请稍后再试。");
            else if (reply->error() != QNetworkReply::NoError || code != 200) result->setText("连接失败，请检查网络、证书或服务状态。离线功能仍可使用。");
            else {
                auto data = QJsonDocument::fromJson(bytes).object().value("data");
                if (!data.isArray()) result->setText("服务返回格式异常，请稍后再试。");
                else result->setText("连接成功，密钥认证通过。测试未上传文件，也未自动保存输入的密钥；聊天模型和余额仍以实际请求为准。");
            }
            reply->deleteLater();
        });
    });
    connect(this, &QDialog::finished, key, [key] { key->clear(); });
}
