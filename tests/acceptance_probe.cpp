#include "../mainwindow.h"
#include "ui_mainwindow.h"
#include "../PrivacyCrypto.h"
#include "../AppTheme.h"
#include <QApplication>
#include <QDialog>
#include <QMessageBox>
#include <QMenu>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QFile>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QToolButton>
#include <QUuid>
#include <iostream>
#include <cstdlib>
class AcceptanceProbe {
public:
static void check(bool b, const char* message) { if (!b) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); } }
static void select(MainWindow& w, FileNode* n) {
    for (int i = 0; i < w.ui->list_files->count(); ++i) if (w.ui->list_files->item(i)->data(Qt::UserRole).value<FileNode*>() == n) { w.ui->list_files->setCurrentRow(i); return; }
    check(false, "select node");
}
static void semanticSearch(MainWindow& window) {
    auto fs = window.file_system; auto root = fs->get_root();
    auto folder = fs->create_folder(root, "语义搜索回归");
    auto chinese = fs->create_file(folder, "游戏资料.txt", "");
    auto english = fs->create_file(folder, "games_notes.txt", "");
    auto gaming = fs->create_file(folder, "gaming.txt", "");
    auto bodyOnly = fs->create_file(folder, "memo.txt", "games");
    auto workReport = fs->create_file(folder, "work_report.txt", "");
    auto report = fs->create_file(folder, "report.txt", "");
    auto reporter = fs->create_file(folder, "reporter.txt", "");
    auto unrelated = fs->create_file(folder, "endgames.txt", "");
    auto literal = fs->create_file(folder, "xyzunique.txt", "");
    auto hidden = fs->create_folder(folder, "隐藏"); fs->set_locked(hidden, true);
    auto secret = fs->create_file(hidden, "games秘密.txt", "游戏");
    window.navigate_to("/语义搜索回归");
    window.search_global_ = false; window.search_body_ = false; window.search_type_ = 2;
    window.search_after_.clear(); window.search_before_.clear(); window.search_min_.clear(); window.search_max_.clear();
    window.file_sort_ = 0; window.sort_descending_ = false;
    auto query = [&](const QString& text) {
        window.ui->lineEdit_search->setText(text);
        const auto indexed = window.search_hits(true), traversed = window.search_hits(false);
        QMap<FileNode*, int> a, b;
        for (auto h : indexed) a[h.first] = h.second;
        for (auto h : traversed) b[h.first] = h.second;
        check(a == b, "semantic index/DFS result and score parity");
        check(!a.contains(secret), "semantic aliases never expose locked files");
        return a;
    };
    auto hits = query("游戏");
    check(hits.size() == 3 && hits.contains(chinese) && hits.contains(english) && hits.contains(gaming), "Chinese finds English game variants");
    check(hits[chinese] > hits[english] && !hits.contains(bodyOnly) && !hits.contains(unrelated), "literal priority, body off and alias word boundaries");
    hits = query("GaMeS");
    check(hits.contains(chinese) && hits.contains(gaming) && hits[english] > hits[chinese], "English finds Chinese; case-insensitive literal priority");
    hits = query("游戏 笔记"); check(hits.size() == 1 && hits.contains(english), "multiple semantic terms retain AND");
    hits = query("工作报告"); check(hits.size() == 1 && hits.contains(workReport), "Chinese compound matches work_report");
    hits = query("工作报告不存在"); check(hits.isEmpty(), "unknown compound suffix is not discarded");
    hits = query("报告"); check(hits.size() == 2 && hits.contains(report) && hits.contains(workReport) && !hits.contains(reporter), "report does not expand to reporter");
    hits = query("xyzunique"); check(hits.size() == 1 && hits.contains(literal), "unknown keywords retain literal matching");
    hits = query("游"); check(hits.size() == 1 && hits.contains(chinese), "single-character fallback retains parity");
    window.search_body_ = true; hits = query("游戏");
    check(hits.size() == 4 && hits.contains(bodyOnly) && hits[english] > hits[bodyOnly], "optional body aliases and name priority");
    window.search_body_ = false;
    window.search_global_ = true; hits = query("游戏");
    check(hits.contains(fs->find_by_path("/game.txt")), "global aliases include other directories");
    window.search_global_ = false;
    window.search_type_ = 1; check(query("游戏").isEmpty(), "semantic type filter"); window.search_type_ = 2;
    window.search_after_ = "2099-01-01"; check(query("游戏").isEmpty(), "semantic date filter"); window.search_after_.clear();
    window.search_min_ = "1"; check(query("游戏").isEmpty(), "semantic size filter excludes empty files"); window.search_min_.clear();
    window.ui->lineEdit_search->setText("游戏"); window.run_smart_search();
    check(window.ui->list_files->item(0)->data(Qt::UserRole).value<FileNode*>() == chinese, "UI ranks direct name before alias names");
    bool explained = false;
    for (int i = 0; i < window.ui->list_files->count(); ++i) {
        auto row = window.ui->list_files->item(i);
        if (row->data(Qt::UserRole).value<FileNode*>() == english) explained = row->toolTip().contains("游戏 → games");
    }
    check(explained, "tooltip explains alias without disclosing body excerpts");
    window.ui->lineEdit_search->clear(); window.navigate_to("/");
}
static void readonlyAgentResults(MainWindow& window) {
    auto fs = window.file_system; auto root = fs->get_root();
    auto folder = fs->create_folder(root, "Agent回归");
    auto older = fs->create_file(folder, "点名册.txt", "DO_NOT_UPLOAD_BODY_3812");
    auto newer = fs->create_file(folder, "点名册1.txt", "DO_NOT_UPLOAD_BODY_3813");
    check(fs->set_metadata(older, older->get_size(), "2026-10-14 09:00:00", "2026-10-15 12:00:00"), "fixture older metadata");
    check(fs->set_metadata(newer, newer->get_size(), "2026-10-15 11:00:00", "2026-10-15 11:00:00"), "fixture newer metadata");
    auto locked = fs->create_folder(folder, "AGENT_PRIVATE_DIRECTORY_12"); fs->set_locked(locked, true);
    fs->create_file(locked, "AGENT_PRIVATE_NAME_12.txt", "AGENT_PRIVATE_BODY_12");
    auto outside = fs->create_file(root, "OUTSIDE_AGENT_SCOPE.txt", "");
    QHash<QString, FileNode*> handles;
    auto snapshot = window.agent_search_snapshot(folder, handles);
    const auto json = QJsonDocument(snapshot).toJson();
    check(snapshot.size() == 2 && !json.contains("DO_NOT_UPLOAD_BODY") && !json.contains("AGENT_PRIVATE") && !handles.values().contains(outside), "agent snapshot excludes bodies, protected hierarchy and outside scope");
    auto previousAccess = window.has_privacy_access, previousShow = window.show_privacy_mode;
    window.has_privacy_access = window.show_privacy_mode = true;
    QHash<QString, FileNode*> authorizedHandles;
    const auto authorized = window.agent_search_snapshot(folder, authorizedHandles);
    check(authorized.size() == 2 && window.agent_search_snapshot(locked, authorizedHandles).isEmpty(), "even unlocked privacy metadata never uploaded");
    window.has_privacy_access = previousAccess; window.show_privacy_mode = previousShow;
    QString id;
    for (auto it = handles.begin(); it != handles.end(); ++it) if (it.value() == newer) id = it.key();
    const QJsonArray selection{QJsonObject{{"id", id}, {"reason", "创建时间更晚，修改时间不是判断依据"}}};
    const auto undoCount = window.undo_stack.size(); window.navigate_to("/Agent回归"); window.ui->lineEdit_search->setText("旧关键词");
    check(window.show_agent_search_results(snapshot, handles, selection, "找最新创建的点名册", "点名册1创建更晚"), "agent result accepted after live validation");
    check(window.ui->lineEdit_search->text().isEmpty(), "AI task does not populate ordinary search field");
    check(window.ui->list_files->count() == 1 && window.ui->list_files->item(0)->data(Qt::UserRole).value<FileNode*>() == newer, "agent can display one selected result rather than all keyword matches");
    check(window.ui->list_files->item(0)->toolTip().contains("创建时间更晚") && window.undo_stack.size() == undoCount && fs->get_path(newer) == "/Agent回归/点名册1.txt", "agent explains without mutating or adding undo entries");
    window.on_list_files_itemDoubleClicked(window.ui->list_files->item(0));
    check(fs->get_current_directory() == folder && window.ui->list_files->currentItem()->data(Qt::UserRole).value<FileNode*>() == newer, "agent result double click locates selected file");
    const auto oldCount = window.ui->list_files->count();
    check(fs->set_metadata(newer, newer->get_size(), newer->get_created_time(), "2026-10-15 13:00:00"), "mutate fixture while request in flight");
    check(!window.show_agent_search_results(snapshot, handles, selection, "test", "stale") && window.ui->list_files->count() == oldCount, "stale metadata does not replace current UI");
    check(!window.show_agent_search_results(snapshot, handles, QJsonArray{QJsonObject{{"id", "invented"}, {"reason", "guess"}}}, "test", "invalid"), "fabricated integration id rejected");
    window.ui->lineEdit_search->setText("点名册"); window.run_smart_search();
    check(window.ui->list_files->count() == 2, "ordinary offline search remains usable after agent results");
    window.ui->lineEdit_search->clear(); window.navigate_to("/");
}
static void aiClassificationChoices(MainWindow& window) {
    auto fs = window.file_system; auto folder = fs->create_folder(fs->get_root(), "AI归类回归");
    auto source = fs->create_file(folder, "周五讨论.txt", "角色技能平衡与关卡设计讨论");
    auto games = fs->create_folder(folder, "游戏"); auto study = fs->create_folder(folder, "学习"); auto work = fs->create_folder(folder, "工作");
    auto fourth = fs->create_folder(folder, "其他");
    QMap<QString, FileNode*> candidates;
    for (auto target : {games, study, work, fourth}) candidates[QString::fromStdString(fs->get_path(target))] = target;
    candidates["/捏造路径"] = games;
    auto item = [&](FileNode* target, const QString& reason) { return QJsonObject{{"path", QString::fromStdString(fs->get_path(target))}, {"reason", reason}}; };
    QJsonObject recommendation{{"recommendations", QJsonArray{
        QJsonObject{{"path", "/捏造路径"}, {"reason", "无效路径"}}, item(games, "正文提到角色技能与关卡设计，与游戏目录相关"),
        item(games, "重复项"), item(study, "如果作为课程讨论记录，可归入学习"), item(work, "如果作为项目会议记录，可归入工作"), item(fourth, "多余候选")}}};
    const auto path = fs->get_path(source); const auto undoCount = window.undo_stack.size();
    QTimer::singleShot(50, [] {
        auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); check(dialog && dialog->objectName() == "ai_classification_choices", "AI offers separate choices dialog");
        auto list = dialog->findChild<QListWidget*>("ai_classification_targets");
        auto confirm = dialog->findChild<QPushButton*>("confirm_ai_classification");
        check(list->count() == 3 && list->item(0)->text().contains("角色技能") && !confirm->isEnabled(), "three unique validated recommendations with concrete reasons and explicit selection");
        list->setCurrentRow(1); check(confirm->isEnabled(), "selection enables explicit move"); dialog->reject();
    });
    check(window.choose_ai_classification(source, candidates, recommendation, &window).contains("取消"), "cancel status");
    check(fs->get_path(source) == path && window.undo_stack.size() == undoCount, "cancel does not move or add undo");
    QTimer::singleShot(50, [] {
        auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); auto list = dialog->findChild<QListWidget*>("ai_classification_targets");
        list->setCurrentRow(1); dialog->findChild<QPushButton*>("confirm_ai_classification")->click();
    });
    check(window.choose_ai_classification(source, candidates, recommendation, &window).contains("已移动并保存"), "selected AI choice moves and persists");
    check(source->get_parent() == study && window.undo_stack.size() == undoCount + 1, "human can choose second AI recommendation");
    window.on_btn_undo_clicked(); check(fs->get_path(source) == path && window.undo_stack.size() == undoCount, "AI recommendation move supports existing undo");
    QJsonObject none{{"recommendations", QJsonArray{}}, {"reason", "无法确定合适位置"}};
    check(window.choose_ai_classification(source, candidates, none, &window).contains("没有合适"), "AI may decline to recommend");
    check(window.choose_ai_classification(source, candidates, QJsonObject{{"path", "bad"}}, &window).contains("格式无效"), "malformed response cannot move");
    auto conflict = fs->create_file(games, source->get_name(), "");
    QJsonObject single{{"recommendations", QJsonArray{item(games, "角色技能讨论")}}};
    check(window.choose_ai_classification(source, candidates, single, &window).contains("没有合适"), "name conflict rejects destination");
    QJsonObject chooseWork{{"recommendations", QJsonArray{item(work, "项目讨论")}}};
    QTimer::singleShot(50, [&] {
        auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); auto list = dialog->findChild<QListWidget*>("ai_classification_targets");
        list->setCurrentRow(0); fs->set_locked(work, true); dialog->findChild<QPushButton*>("confirm_ai_classification")->click();
    });
    check(window.choose_ai_classification(source, candidates, chooseWork, &window).contains("权限已变化"), "privacy changed while selecting rejects move");
    fs->set_locked(work, false);
    check(fs->get_path(source) == path && window.undo_stack.size() == undoCount && conflict, "rejected recommendations preserve file and undo");
    window.navigate_to("/");
}
static void filterOnlySearch(MainWindow& window) {
    auto fs = window.file_system; auto folder = fs->create_folder(fs->get_root(), "仅筛选回归");
    auto nested = fs->create_folder(folder, "子目录");
    auto old = fs->create_file(folder, "旧文件.txt", "a"); auto recent = fs->create_file(nested, "新文件.txt", "abcdef");
    fs->set_metadata(old, 1, "2020-01-01 10:00:00", "2020-01-01 10:00:00");
    fs->set_metadata(recent, 6, "2025-02-03 10:00:00", "2025-02-03 10:00:00");
    auto locked = fs->create_folder(folder, "锁定"); fs->set_locked(locked, true);
    auto secret = fs->create_file(locked, "秘密.txt", "abcdef"); fs->set_metadata(secret, 6, "2025-02-03 10:00:00", "2025-02-03 10:00:00");
    window.search_global_ = false; window.search_type_ = 0; window.search_body_ = false;
    window.search_after_.clear(); window.search_before_.clear(); window.search_min_.clear(); window.search_max_.clear();
    window.ui->lineEdit_search->clear(); window.navigate_to("/仅筛选回归");
    auto dateLow = window.findChild<QLineEdit*>("search_date_after"), dateHigh = window.findChild<QLineEdit*>("search_date_before");
    dateLow->setText("2025-01-01"); dateHigh->setText("2025-12-31");
    check(!window.search_mode_, "filter-only starts from ordinary view");
    window.findChild<QPushButton*>("apply_search_date")->click();
    check(window.search_mode_ && window.ui->list_files->count() == 1 && window.ui->list_files->item(0)->data(Qt::UserRole).value<FileNode*>() == recent,
          "empty keyword date-only filter starts search and finds descendants without exposing private files");
    dateLow->setText("2099-01-01"); dateHigh->clear(); window.findChild<QPushButton*>("apply_search_date")->click();
    check(window.ui->list_files->count() == 0 && window.search_mode_, "empty keyword date filter can return zero results");
    window.search_after_.clear(); window.search_before_.clear(); window.navigate_to("/仅筛选回归");
    auto types = window.search_options_button_->menu()->actions()[2]->menu()->actions();
    types[2]->trigger(); check(window.search_mode_ && window.ui->list_files->count() == 2, "empty keyword TXT-only filter starts from ordinary view");
    types[1]->trigger(); check(window.ui->list_files->count() == 1 && window.ui->list_files->item(0)->data(Qt::UserRole).value<FileNode*>() == nested, "directory-only excludes private directory");
    types[0]->trigger(); check(!window.search_mode_, "clearing all constraints restores ordinary view");
    auto min = window.findChild<QLineEdit*>("search_size_min"), max = window.findChild<QLineEdit*>("search_size_max");
    min->setText("2"); max->setText("10"); window.findChild<QPushButton*>("apply_search_size")->click();
    QSet<FileNode*> rows; for (int i = 0; i < window.ui->list_files->count(); ++i) rows.insert(window.ui->list_files->item(i)->data(Qt::UserRole).value<FileNode*>());
    check(window.search_mode_ && rows.contains(recent) && !rows.contains(old) && !rows.contains(secret), "empty keyword size-only filter respects bounds and privacy");
    types[2]->trigger(); check(window.ui->list_files->count() == 1, "empty keyword combined size/type filters");
    window.search_type_ = 0; window.search_min_.clear(); window.search_max_.clear(); window.ui->lineEdit_search->clear(); window.navigate_to("/");
}
static void summaryPreflight(MainWindow& window) {
    auto fs = window.file_system; auto root = fs->get_root();
    auto empty = fs->create_file(root, "摘要空白.txt", " \n\t"); window.navigate_to("/"); select(window, empty);
    const auto undoCount = window.undo_stack.size();
    QTimer::singleShot(50, [] {
        auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); check(dialog, "summary dialog opens");
        dialog->findChild<QComboBox*>()->setCurrentIndex(2); dialog->findChild<QPushButton*>("send_ai_task")->click();
        bool blank = false; for (auto output : dialog->findChildren<QPlainTextEdit*>()) blank |= output->toPlainText().contains("未发送请求");
        check(blank && !dialog->findChild<QPushButton*>("copy_ai_summary")->isEnabled(), "blank TXT rejected before credentials/upload; error not copyable"); dialog->reject();
    });
    window.open_ai_tools();
    const auto access = window.has_privacy_access, show = window.show_privacy_mode;
    auto secret = fs->create_file(root, "摘要隐私.txt", "PRIVATE_SUMMARY_SENTINEL_20"); fs->set_locked(secret, true);
    window.has_privacy_access = window.show_privacy_mode = true; window.navigate_to("/"); select(window, secret);
    QTimer::singleShot(50, [] {
        auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); dialog->findChild<QComboBox*>()->setCurrentIndex(2);
        dialog->findChild<QPushButton*>("send_ai_task")->click(); bool blocked = false;
        for (auto output : dialog->findChildren<QPlainTextEdit*>()) blocked |= output->toPlainText().contains("禁止上传");
        check(blocked && !dialog->findChild<QPushButton*>("copy_ai_summary")->isEnabled(), "even locally unlocked private TXT cannot be summarised online"); dialog->reject();
    });
    window.open_ai_tools(); window.has_privacy_access = access; window.show_privacy_mode = show;
    check(window.undo_stack.size() == undoCount && empty->get_content() == " \n\t" && secret->get_content() == "PRIVATE_SUMMARY_SENTINEL_20", "summary preflight leaves files and undo unchanged");
    window.navigate_to("/");
}
static void run() {
    MainWindow window("acceptance"); window.show(); QApplication::processEvents();
    window.ai_credentials_ = AiCredentialStore("FileManager/Tests/MissingKey/" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    auto fs = window.file_system; fs->reset();
    auto root = fs->get_root();
    auto work = fs->create_folder(root, "工作文档"); fs->create_folder(root, "工作档案");
    auto report = fs->create_file(root, "报告.txt", "项目会议报告报告");
    fs->create_file(root, "正文.txt", "项目报告");
    auto locked = fs->create_folder(root, "隐私"); fs->set_locked(locked, true);
    fs->create_file(locked, "报告秘密.txt", "PRIVATE_SECRET_82746");
    auto dataset = fs->create_folder(root, "benchmark");
    for (int i = 0; i < 3000; ++i) fs->create_file(dataset, "document_" + std::to_string(i) + ".txt", "ordinary text");
    PrivacyCrypto::randomBytes(window.crypto_salt_, 16);
    PrivacyCrypto::deriveKey("test-only", window.crypto_salt_, 300000, window.crypto_session_key_);
    window.navigate_to("/");
    check(!window.history_list.contains("/"), "root not in history");
    check(window.file_header_->model()->headerData(0, Qt::Horizontal).toString() == "名称", "name header");
    check(window.file_header_->model()->headerData(1, Qt::Horizontal).toString() == "修改日期", "date header");
    check(window.file_header_->model()->headerData(2, Qt::Horizontal).toString() == "类型", "type header");
    check(window.ui->btn_copy->text().isEmpty() && !window.ui->btn_copy->icon().isNull(), "resource icon replaces text");
    check(window.ui->btn_undo->size() == window.ui->btn_new->size() && window.ui->btn_reset->size() == window.ui->btn_new->size(), "button sizes match");
    for (auto action : window.menuBar()->actions()) check(!action->text().contains("智能搜索 / 分类"), "combined menu removed");
    bool separator = false;
    for (int i = 0; i < window.ui->tree_dir->topLevelItemCount(); ++i) {
        auto item = window.ui->tree_dir->topLevelItem(i);
        if (item->data(0, Qt::UserRole + 1).toInt() == 99) separator = !(item->flags() & Qt::ItemIsSelectable);
    }
    check(separator, "tree separator is not selectable");
    check(window.search_options_button_->isHidden(), "options initially hidden");
    QFocusEvent focus(QEvent::FocusIn); QApplication::sendEvent(window.ui->lineEdit_search, &focus);
    check(!window.search_options_button_->isHidden(), "search focus shows options");
    auto actions = window.search_options_button_->menu()->actions();
    check(actions.size() == 5, "five options");
    for (int i = 0; i < 5; ++i) check(actions[i]->text() == QStringList{"范围", "修改日期", "类型", "大小", "包含 TXT 正文"}[i], "option order");
    window.ui->lineEdit_search->setText("报告"); window.on_btn_search_clicked();
    check(window.ui->list_files->count() == 1, "name-only excludes body and locked descendants");
    QElapsedTimer timer; timer.start(); auto indexed = window.search_hits(true); auto indexNs = timer.nsecsElapsed();
    timer.restart(); auto dfs = window.search_hits(false); auto dfsNs = timer.nsecsElapsed();
    QSet<FileNode*> a, b; for (auto h : indexed) a.insert(h.first); for (auto h : dfs) b.insert(h.first); check(a == b, "index/DFS parity");
    QFile perf("performance.txt"); check(perf.open(QIODevice::WriteOnly), "performance report"); perf.write(QString("3000 files: cached name query %1 ms; DFS %2 ms\n").arg(indexNs / 1e6).arg(dfsNs / 1e6).toUtf8());
    actions[4]->setChecked(true);
    check(window.ui->list_files->count() == 2, "body menu enables content search");
    check(window.ui->list_files->item(0)->data(Qt::UserRole).value<FileNode*>() == report, "name priority");
    window.findChild<QPushButton*>("btn_sort")->menu()->actions()[2]->trigger(); check(window.ui->list_files->item(0)->text() == "正文.txt", "numeric sort menu");
    window.file_sort_ = 0;
    window.findChild<QLineEdit*>("search_size_max")->setText("0"); window.findChild<QPushButton*>("apply_search_size")->click(); check(window.ui->list_files->count() == 0, "size submenu applies"); window.search_max_.clear();
    window.findChild<QLineEdit*>("search_date_after")->setText("2099-01-01"); window.findChild<QPushButton*>("apply_search_date")->click(); check(window.ui->list_files->count() == 0, "date submenu applies"); window.search_after_.clear();
    actions[2]->menu()->actions()[1]->trigger(); check(window.ui->list_files->count() == 0, "directory type menu"); actions[2]->menu()->actions()[2]->trigger();
    fs->change_directory(work); window.run_smart_search(); check(window.ui->list_files->count() == 0, "current scope");
    actions[0]->menu()->actions()[1]->trigger(); check(window.ui->list_files->count() == 2, "global scope menu");
    fs->change_directory(root); window.run_smart_search(); select(window, report);
    QTimer::singleShot(50, [] { auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); check(dialog && dialog->objectName() == "classification_dialog", "separate classification dialog"); check(dialog->findChild<QListWidget*>("classification_targets")->count() >= 2, "multiple candidate targets"); dialog->reject(); });
    window.auto_classify(); check(fs->get_path(report) == "/报告.txt", "cancel does not move");
    QTimer::singleShot(50, [] { auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); auto choices = dialog->findChild<QListWidget*>("classification_targets"); for (int i = 0; i < choices->count(); ++i) if (choices->item(i)->text() == "/工作档案") choices->setCurrentRow(i); dialog->findChild<QPushButton*>("confirm_classification")->click(); });
    window.auto_classify(); check(fs->get_path(report) == "/工作档案/报告.txt", "moves to chosen candidate");
    window.on_btn_undo_clicked(); check(fs->get_path(report) == "/报告.txt", "classification undo");
    window.ui->list_files->clearSelection();
    QTimer::singleShot(50, [] { auto box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); check(box && box->windowTitle() == "最近自动归类" && box->text().contains("工作档案"), "no selection shows recent history"); box->accept(); });
    window.auto_classify();
    window.ui->lineEdit_search->clear(); window.refresh_file_list(); select(window, report);
    QTimer::singleShot(50, [] {
        auto menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()); check(menu, "context menu opens");
        auto entries = menu->actions(); int open = -1, classify = -1;
        for (int i = 0; i < entries.size(); ++i) { if (entries[i]->text() == "打开") open = i; if (entries[i]->text() == "自动归类") classify = i; }
        check(open >= 0 && classify == open + 1, "classification directly below Open"); menu->close();
    });
    window.ui->list_files->customContextMenuRequested(window.ui->list_files->visualItemRect(window.ui->list_files->currentItem()).center());
    window.on_btn_rename_clicked(); QApplication::processEvents();
    auto editor = window.ui->list_files->findChild<QLineEdit*>();
    check(editor && editor->selectedText() == "报告", "rename excludes suffix");
    auto item = window.ui->list_files->currentItem(); auto rowRect = window.ui->list_files->visualItemRect(item);
    check(editor->geometry().left() >= rowRect.left() + 30 && !item->icon().isNull(), "rename leaves icon visible");
    window.grab().save("rename-preview.png");
    window.ui->list_files->closePersistentEditor(item); window.ui->lineEdit_search->setFocus(); QApplication::processEvents();
    check(window.save_data(), "save succeeds");
    MainWindow reloaded("acceptance"); auto secret = reloaded.file_system->find_by_path("/隐私/报告秘密.txt");
    check(secret && secret->get_content().empty() && !secret->get_sealed_content().empty(), "restart private ciphertext only");
    auto jump = fs->create_file(work, "jump.txt", "jump"); window.refresh_file_list();
    window.ui->lineEdit_search->setText("jump"); window.search_global_ = true; window.run_smart_search();
    check(window.ui->list_files->count() == 1, "jump result"); window.on_list_files_itemDoubleClicked(window.ui->list_files->item(0));
    check(fs->get_current_directory() == work && window.ui->list_files->currentItem()->data(Qt::UserRole).value<FileNode*>() == jump, "search result jumps and selects file");
    window.navigate_to("/"); window.grab().save("integrated-tools.png");
    check(window.ui->tree_dir->indentation() == 14, "compact tree indentation");
    window.on_tree_dir_itemClicked(window.ui->tree_dir->topLevelItem(0), 0);
    check(window.recycle_mode && window.classify_button_->isHidden(), "recycle hides classification");
    window.navigate_to("/"); check(!window.classify_button_->isHidden(), "normal view restores classification");
    select(window, report);
    QPoint blank(window.ui->list_files->viewport()->width() / 2, window.ui->list_files->viewport()->height() - 10);
    check(!window.ui->list_files->itemAt(blank), "test point is blank");
    QMouseEvent click(QEvent::MouseButtonPress, QPointF(blank), QPointF(window.ui->list_files->viewport()->mapToGlobal(blank)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window.ui->list_files->viewport(), &click);
    check(window.ui->list_files->selectedItems().isEmpty() && !window.ui->list_files->currentItem(), "blank click clears selection and current item");

    auto games = fs->create_folder(root, "Games"); fs->create_folder(root, "Photos");
    auto game = fs->create_file(root, "game.txt", ""); window.refresh_file_list(); select(window, game);
    QTimer::singleShot(50, [] {
        auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); check(dialog && dialog->objectName() == "classification_dialog", "game recommendation dialog");
        auto choices = dialog->findChild<QListWidget*>("classification_targets"); check(choices->item(0)->text() == "/Games", "game.txt ranks Games first");
        for (int i = 0; i < choices->count(); ++i) check(choices->item(i)->text() != "/Photos", "unrelated category excluded");
        dialog->findChild<QPushButton*>("confirm_classification")->click();
    });
    window.auto_classify(); check(fs->get_path(game) == "/Games/game.txt", "confirmed game classification");
    window.on_btn_undo_clicked(); check(fs->get_path(game) == "/game.txt", "game move undo");

    window.toggle_pin(report); check(fs->is_pinned(report), "files can be pinned"); check(window.save_data(), "pin save");
    MainWindow pinReload("acceptance"); check(pinReload.file_system->is_pinned(pinReload.file_system->find_by_path("/报告.txt")), "file pin persists");
    window.navigate_to("/工作文档");
    QTreeWidgetItem* pinned = nullptr;
    for (int i = 0; i < window.ui->tree_dir->topLevelItemCount(); ++i) if (window.ui->tree_dir->topLevelItem(i)->data(0, Qt::UserRole).value<FileNode*>() == report) pinned = window.ui->tree_dir->topLevelItem(i);
    check(pinned, "pinned file sidebar entry"); window.on_tree_dir_itemClicked(pinned, 0);
    check(fs->get_current_directory() == root && window.ui->list_files->currentItem()->data(Qt::UserRole).value<FileNode*>() == report, "pinned file locates and selects");

    auto nested = fs->create_folder(work, "Levels"); window.navigate_to("/"); window.forward_stack.push("/benchmark");
    auto backSize = window.back_stack.size();
    window.edit_path(); window.ui->lineEdit_path->setText("/此电脑/工作文档/Levels"); window.ui->lineEdit_path->returnPressed(); QApplication::processEvents();
    check(fs->get_current_directory() == nested && window.back_stack.size() == backSize + 1 && window.forward_stack.empty(), "typed path validates then updates history");
    window.grab().save("breadcrumb-preview.png");
    QToolButton* parentCrumb = nullptr;
    for (auto button : window.breadcrumb_bar_->findChildren<QToolButton*>()) if (!button->isHidden() && button->property("path").toString() == "/工作文档") parentCrumb = button;
    check(parentCrumb, "parent breadcrumb exists"); parentCrumb->click(); QApplication::processEvents();
    check(fs->get_current_directory() == work, "breadcrumb jumps to ancestor");
    window.on_btn_back_clicked(); check(fs->get_current_directory() == nested, "breadcrumb back history");
    window.on_btn_forward_clicked(); check(fs->get_current_directory() == work, "breadcrumb forward history");
    auto failBack = window.back_stack.size(), failForward = window.forward_stack.size();
    for (auto invalid : QStringList{"relative/path", "/missing-directory", "/game.txt", "/隐私"}) {
        window.edit_path(); window.ui->lineEdit_path->setText(invalid);
        QTimer::singleShot(50, [] { auto box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); check(box, "invalid path warning"); box->accept(); });
        window.ui->lineEdit_path->returnPressed(); QApplication::processEvents();
        check(fs->get_current_directory() == work && window.ui->lineEdit_path->text() == "/此电脑/工作文档", "failed path restores current address");
        check(window.back_stack.size() == failBack && window.forward_stack.size() == failForward, "failed path preserves navigation stacks");
    }
    check(!window.history_list.contains("/"), "breadcrumb root excluded from recent history");
    window.navigate_to("/"); window.file_header_->sectionClicked(0); window.file_header_->sectionClicked(0); QApplication::processEvents();
    check(window.file_header_->sortIndicatorSection() == 0 && window.file_header_->sortIndicatorOrder() == Qt::DescendingOrder, "header sort direction toggles");
    window.grab().save("navigation-and-header.png");
    semanticSearch(window);
    readonlyAgentResults(window);
    aiClassificationChoices(window);
    filterOnlySearch(window);
    summaryPreflight(window);
    qunsetenv("DEEPSEEK_API_KEY");
    QTimer::singleShot(50, [] { auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); for (auto button : dialog->findChildren<QPushButton*>()) if (button->objectName() == "send_ai_task") button->click(); bool fallback = false; for (auto text : dialog->findChildren<QPlainTextEdit*>()) fallback |= text->toPlainText().contains("未配置"); check(fallback, "missing AI key fallback"); dialog->accept(); });
    window.open_ai_tools();
    std::cout << "[PASS] search/sort/classification/game-Games/undo, breadcrumb/invalid-path/history, pin/persistence/sidebar, blank selection, recycle visibility, privacy/AES, AI fallback\n";
}
};
int main(int argc, char** argv) {
    QApplication app(argc, argv); QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc"); app.setFont(QFont("Microsoft YaHei", 10)); app.setStyleSheet(app_theme()); AcceptanceProbe::run();
}
