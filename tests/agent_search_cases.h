#pragma once
#include "AiSearchAgent.h"
#include <QJsonDocument>
#include <QEventLoop>

// Included by the existing AI regression executable, after FakeReply/check.
class AgentNetwork : public QNetworkAccessManager {
public:
    std::function<QJsonObject(const QJsonObject&, int)> responder;
    QList<QJsonObject> requests;
    int status = 200; bool malformed = false; QByteArray rawOverride;
    QNetworkReply::NetworkError failure = QNetworkReply::NoError;
protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& req, QIODevice* data) override {
        check(op == PostOperation && data && req.url() == QUrl("https://api.deepseek.com/chat/completions"), "agent fixed POST endpoint");
        check(req.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy, "agent never forwards key on redirects");
        check(req.rawHeader("Authorization") == "Bearer test-agent-only", "agent uses supplied test key");
        const auto request = QJsonDocument::fromJson(data->readAll()).object(); requests.append(request);
        const auto message = responder ? responder(request, requests.size()) : QJsonObject{{"content", "unsupported"}};
        const auto json = QJsonObject{{"choices", QJsonArray{QJsonObject{{"message", message}}}}};
        return new FakeReply(req, status, !rawOverride.isEmpty() ? rawOverride : malformed ? QByteArray("invalid") : QJsonDocument(json).toJson(), failure, this);
    }
};
static QJsonObject toolCall(const QString& name, const QJsonObject& args, const QString& id = "call1") {
    const QJsonObject fn{{"name", name}, {"arguments", QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact))}};
    const QJsonObject call{{"id", id}, {"type", "function"}, {"function", fn}};
    return {{"role", "assistant"}, {"content", QJsonValue::Null}, {"tool_calls", QJsonArray{call}}};
}
static void agentSearchCases() {
    const QJsonArray snapshot{
        QJsonObject{{"id", "old"}, {"name", "点名册.txt"}, {"path", "/学习/点名册.txt"}, {"parent", "/学习"}, {"type", "file"}, {"size", 8},
            {"created", "2026-10-14 09:00:00"}, {"modified", "2026-10-15 11:00:00"}},
        QJsonObject{{"id", "new"}, {"name", "点名册1.txt"}, {"path", "/学习/点名册1.txt"}, {"parent", "/学习"}, {"type", "file"}, {"size", 7},
            {"created", "2026-10-15 10:00:00"}, {"modified", "2026-10-15 10:00:00"}},
        QJsonObject{{"id", "other"}, {"name", "games.txt"}, {"path", "/游戏/games.txt"}, {"parent", "/游戏"}, {"type", "file"}, {"size", 2},
            {"created", "2026-10-15 10:00:00"}, {"modified", "2026-10-15 10:00:00"}}
    };
    AgentNetwork network; AiSearchAgent agent(snapshot, &network);
    check(agent.executeTool("finish_search", {{"summary", "guess"}, {"files", QJsonArray{}}}).contains("error"), "agent cannot finish without querying");
    check(agent.executeTool("delete_file", {}).contains("error") && agent.executeTool("read_text", {}).contains("error"), "agent has no write or body tool");
    check(agent.executeTool("get_metadata", {{"id", "new"}}).contains("error"), "unobserved metadata id rejected");
    check(agent.executeTool("query_files", {{"path", "C:/Windows"}}).contains("error"), "Windows paths rejected");
    check(agent.executeTool("query_files", {{"limit", -1}}).contains("error"), "bad pagination rejected");
    auto result = agent.executeTool("query_files", {{"keywords", "点名册"}, {"order_by", "created"}, {"descending", true}});
    check(result.value("items").toArray().first().toObject().value("id") == "new", "created differs from modified ordering");
    result = agent.executeTool("query_files", {{"keywords", "点名册"}, {"order_by", "modified"}, {"descending", true}});
    check(result.value("items").toArray().first().toObject().value("id") == "old", "modified sort chooses different file");
    check(agent.executeTool("query_files", {{"keywords", "游戏"}}).value("total").toInt() == 1, "agent tools reuse offline bilingual aliases");
    check(agent.executeTool("list_directory", {{"path", "/学习"}, {"limit", 1}}).value("has_more").toBool(), "directory listing pagination");
    result = agent.executeTool("query_files", {{"order_by", "created"}, {"descending", true}, {"limit", 1}});
    check(result.value("same_time_beyond_page").toBool(), "time ties are disclosed across pages");
    const auto selection = QJsonArray{QJsonObject{{"id", "new"}, {"reason", "创建时间最新"}}};
    network.responder = [&](const QJsonObject& request, int round) {
        check(request.value("tools").toArray().size() == 4, "only four readonly/finish tools advertised");
        check(!request.contains("response_format"), "tool calling does not force legacy response JSON format");
        if (round == 1) return toolCall("query_files", {{"keywords", "点名册"}, {"order_by", "created"}, {"descending", true}});
        const auto messages = request.value("messages").toArray();
        const auto tool = messages.last().toObject(); check(tool.value("role") == "tool" && tool.value("tool_call_id") == "call1", "tool output fed into next round");
        check(tool.value("content").toString().contains("2026-10-15 10:00:00"), "model receives actual created timestamps");
        return toolCall("finish_search", {{"summary", "点名册1创建更晚"}, {"files", selection}}, "finish");
    };
    bool done = false, failed = false; QJsonArray chosen;
    agent.completed = [&](QString, QJsonArray files) { chosen = files; done = true; };
    agent.failed = [&](QString) { failed = true; done = true; };
    auto wait = [&] { QElapsedTimer timer; timer.start(); while (!done && timer.elapsed() < 3000) { QApplication::processEvents(); QThread::msleep(1); } check(done, "agent async task terminates"); };
    agent.start("找最新创建的点名册", "test-agent-only", "deepseek-chat"); wait();
    check(!failed && chosen == selection && network.requests.size() == 2, "agent independently queries then selects one verified latest file");
    check(agent.executeTool("finish_search", {{"summary", "fake"}, {"files", QJsonArray{QJsonObject{{"id", "fabricated"}, {"reason", "guess"}}}}}).contains("error"), "fabricated final id rejected");
    // A malformed or failed request cannot alter filesystem/UI; no raw errors are exposed.
    for (int status : {401, 429, 500}) {
        AgentNetwork broken; broken.status = status; AiSearchAgent instance(snapshot, &broken);
        done = false; failed = false;
        instance.failed = [&](QString text) { check(!text.contains("test-agent-only"), "agent error redacts key"); done = failed = true; };
        instance.start("查找文件", "test-agent-only", "deepseek-chat"); wait(); check(failed, "HTTP failure surfaces safely");
    }
    AgentNetwork invalid; invalid.malformed = true; AiSearchAgent bad(snapshot, &invalid);
    done = false; bad.failed = [&](QString) { done = true; }; bad.start("查找文件", "test-agent-only", "deepseek-chat"); wait();
    AgentNetwork looping; looping.responder = [](const QJsonObject&, int) { return toolCall("query_files", {}); };
    AiSearchAgent loop(snapshot, &looping); done = false; loop.failed = [&](QString) { done = true; };
    loop.start("查找文件", "test-agent-only", "deepseek-chat"); wait(); check(looping.requests.size() == 6, "unending agent stops after six rounds");
    AgentNetwork many; many.responder = [](const QJsonObject&, int round) {
        QJsonArray calls; for (int i = 0; i < 4; ++i) calls.append(toolCall("query_files", {}, QString("c%1_%2").arg(round).arg(i)).value("tool_calls").toArray().first());
        return QJsonObject{{"role", "assistant"}, {"tool_calls", calls}};
    };
    AiSearchAgent bounded(snapshot, &many); done = false; bounded.failed = [&](QString) { done = true; };
    bounded.start("查找文件", "test-agent-only", "deepseek-chat"); wait(); check(many.requests.size() == 4, "tool-call budget stops repeated parallel calls");
    AgentNetwork oversized; oversized.rawOverride = QByteArray(140000, 'x'); AiSearchAgent large(snapshot, &oversized);
    done = false; large.failed = [&](QString) { done = true; }; large.start("查找文件", "test-agent-only", "deepseek-chat"); wait();
    AgentNetwork timeout; timeout.status = 0; timeout.failure = QNetworkReply::TimeoutError; AiSearchAgent timed(snapshot, &timeout);
    done = false; timed.failed = [&](QString) { done = true; }; timed.start("查找文件", "test-agent-only", "deepseek-chat"); wait();
    AgentNetwork cancelled; AiSearchAgent cancel(snapshot, &cancelled); bool callback = false;
    cancel.failed = [&](QString) { callback = true; }; cancel.completed = [&](QString, QJsonArray) { callback = true; };
    cancel.start("查找文件", "test-agent-only", "deepseek-chat"); cancel.cancel();
    QEventLoop eventLoop; QTimer::singleShot(30, &eventLoop, &QEventLoop::quit); eventLoop.exec();
    check(!callback && cancelled.requests.size() == 1, "cancel prevents continuation and late callbacks");
    std::cout << "[PASS] readonly search Agent tools, multi-round latest-created selection, pagination/ties, bounds, auth/failure/cancel\n";
}

// Optional manual smoke test: synthetic metadata only, never user's file data.
static int agentLiveSmoke() {
    AiCredentialStore store; QString error;
    auto key = store.effectiveKey(nullptr, &error);
    if (key.isEmpty()) { std::cout << "[SKIP] No configured key; no live request sent.\n"; return 0; }
    const QJsonArray snapshot{
        QJsonObject{{"id", "synthetic-old"}, {"name", "点名册.txt"}, {"path", "/合成测试/点名册.txt"}, {"parent", "/合成测试"}, {"type", "file"}, {"size", 8},
            {"created", "2026-10-14 09:00:00"}, {"modified", "2026-10-15 12:00:00"}},
        QJsonObject{{"id", "synthetic-new"}, {"name", "点名册1.txt"}, {"path", "/合成测试/点名册1.txt"}, {"parent", "/合成测试"}, {"type", "file"}, {"size", 7},
            {"created", "2026-10-15 11:00:00"}, {"modified", "2026-10-15 11:00:00"}}
    };
    QNetworkAccessManager network; AiSearchAgent agent(snapshot, &network); QEventLoop eventLoop; int result = 1; bool finished = false;
    agent.completed = [&](QString summary, QJsonArray files) {
        finished = true;
        std::cout << "Synthetic selection count: " << files.size() << "; summary: " << summary.left(800).toStdString() << std::endl;
        for (auto value : files) { const auto id = value.toObject().value("id").toString(); std::cout << "Synthetic id: " << (id == "synthetic-new" ? "new" : id == "synthetic-old" ? "old" : "other") << std::endl; }
        result = files.size() == 1 && files[0].toObject().value("id") == "synthetic-new" ? 0 : 1;
        std::cout << (result == 0 ? "[PASS] Live tool calling selected synthetic newest-created file.\n" : "[FAIL] Live model did not select expected synthetic file.\n"); eventLoop.quit();
    };
    agent.failed = [&](QString message) { finished = true; std::cout << "[FAIL] " << message.toStdString() << '\n'; result = 1; eventLoop.quit(); };
    agent.progress = [](QString message) { std::cout << message.toStdString() << std::endl; };
    agent.start("授权范围：/合成测试\n用户需求：找最新创建的那个点名册，注意区别创建与修改时间。", key,
        QString::fromUtf8(qgetenv("DEEPSEEK_MODEL").isEmpty() ? QByteArray("deepseek-chat") : qgetenv("DEEPSEEK_MODEL")));
    AiCredentialStore::wipe(key); if (!finished) eventLoop.exec(); return result;
}