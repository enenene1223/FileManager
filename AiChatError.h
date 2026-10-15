#pragma once
#include <QNetworkReply>
#include <QString>

// Never display provider response bodies, URLs or errorString(): they may echo credentials.
inline QString ai_chat_error(int status, QNetworkReply::NetworkError error, bool deadlineExpired = false) {
    const QString offline = "本地功能仍可使用。";
    if (deadlineExpired) return "AI 请求超时，请稍后重试。" + offline;
    if (status == 401 || status == 403) return "AI 认证或访问权限失败，请检查 API Key。" + offline;
    if (status == 402) return "AI 服务提示账户余额不足，请检查账户。" + offline;
    if (status == 429) return "AI 服务限流，请稍后重试。" + offline;
    if (status >= 500 && status <= 599) return QString("AI 服务端异常（HTTP %1），请稍后重试。").arg(status) + offline;
    if (status >= 300 && status <= 399) return QString("AI 接口返回重定向（HTTP %1），未转发密钥。").arg(status) + offline;
    if (status >= 400) return QString("AI 请求被拒绝（HTTP %1），请检查接口与模型配置。").arg(status) + offline;
    if (error == QNetworkReply::TimeoutError) return "AI 网络连接超时，请检查网络后重试。" + offline;
    if (error == QNetworkReply::OperationCanceledError) return "AI 请求已取消。" + offline;
    if (error == QNetworkReply::SslHandshakeFailedError) return "AI 安全连接失败，请检查证书和网络。" + offline;
    if (error != QNetworkReply::NoError) return "AI 网络连接失败，请检查网络后重试。" + offline;
    if (status != 200) return QString("AI 接口返回异常状态（HTTP %1）。").arg(status) + offline;
    return {};
}
