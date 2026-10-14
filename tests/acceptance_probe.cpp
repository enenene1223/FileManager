#include "../mainwindow.h"
#include "ui_mainwindow.h"
#include "../PrivacyCrypto.h"
#include "../AppTheme.h"
#include <QApplication>
#include <QDialog>
#include <QMessageBox>
#include <QMenu>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QFile>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QToolButton>
#include <iostream>
#include <cstdlib>
class AcceptanceProbe {
public:
static void check(bool b, const char* message) { if (!b) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); } }
static void select(MainWindow& w, FileNode* n) {
    for (int i = 0; i < w.ui->list_files->count(); ++i) if (w.ui->list_files->item(i)->data(Qt::UserRole).value<FileNode*>() == n) { w.ui->list_files->setCurrentRow(i); return; }
    check(false, "select node");
}
static void run() {
    MainWindow window("acceptance"); window.show(); QApplication::processEvents();
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
    qunsetenv("DEEPSEEK_API_KEY");
    QTimer::singleShot(50, [] { auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); for (auto button : dialog->findChildren<QPushButton*>()) if (button->text().startsWith("发送")) button->click(); bool fallback = false; for (auto text : dialog->findChildren<QPlainTextEdit*>()) fallback |= text->toPlainText().contains("未配置"); check(fallback, "missing AI key fallback"); dialog->accept(); });
    window.open_ai_tools();
    std::cout << "[PASS] search/sort/classification/game-Games/undo, breadcrumb/invalid-path/history, pin/persistence/sidebar, blank selection, recycle visibility, privacy/AES, AI fallback\n";
}
};
int main(int argc, char** argv) {
    QApplication app(argc, argv); QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc"); app.setFont(QFont("Microsoft YaHei", 10)); app.setStyleSheet(app_theme()); AcceptanceProbe::run();
}
