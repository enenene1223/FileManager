#include "../mainwindow.h"
#include <QApplication>
#include <QDialog>
#include <QCheckBox>
#include <QPushButton>
#include <QLineEdit>
#include <QTreeWidget>
#include <QComboBox>
#include <QDir>
#include "../AppTheme.h"
#include <QMessageBox>
#include <QFontDatabase>
#include <QFile>
#include <QLabel>
#include "../PrivacyCrypto.h"
#include <iostream>
#include <cstdlib>
class AcceptanceProbe {
public:
static void check(bool b, const char* message) {
    if (!b) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static void run() {
    MainWindow window("acceptance");
    window.file_system->reset();
    auto fs = window.file_system;
    auto root = fs->get_root();
    PrivacyCrypto::randomBytes(window.crypto_salt_, 16);
    PrivacyCrypto::deriveKey("test-only", window.crypto_salt_, 300000, window.crypto_session_key_);
    fs->create_folder(root, "工作");
    auto dataset = fs->create_folder(root, "benchmark");
    for (int i = 0; i < 3000; ++i) fs->create_file(dataset, "document_" + std::to_string(i) + ".txt", "ordinary text");
    fs->create_file(root, "报告.txt", "一般内容一般内容");
    fs->create_file(root, "正文.txt", "项目报告");
    auto locked = fs->create_folder(root, "隐私");
    fs->set_locked(locked, true);
    fs->create_file(locked, "报告秘密.txt", "PRIVATE_SECRET_82746");
    QTimer::singleShot(100, [&] {
        auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(dialog, "advanced dialog opens");
        auto edits = dialog->findChildren<QLineEdit*>();
        auto tree = dialog->findChild<QTreeWidget*>();
        auto body = dialog->findChild<QCheckBox*>();
        auto buttons = dialog->findChildren<QPushButton*>();
        QPushButton* search = nullptr;
        for (auto button : buttons) if (button->text().startsWith("搜索 /")) search = button;
        check(edits.size() >= 6 && tree && body && search, "controls exist");
        edits[0]->setText("报告"); search->click();
        check(tree->topLevelItemCount() == 1, "name-only search excludes body and inherited locked files");
        for (auto label : dialog->findChildren<QLabel*>()) if (label->text().startsWith("结果")) {
            QFile report("performance.txt"); check(report.open(QIODevice::WriteOnly), "performance report");
            report.write(label->text().toUtf8());
            check(label->text().contains("通过"), "indexed and DFS result sets agree");
        }
        body->setChecked(true); search->click();
        check(tree->topLevelItemCount() == 2, "optional body search");
        check(tree->topLevelItem(0)->text(0) == "报告.txt", "name ranking before content");
        auto combos = dialog->findChildren<QComboBox*>();
        check(combos.size() == 3, "scope/type/sort controls");
        combos[2]->setCurrentIndex(2); search->click();
        check(tree->topLevelItem(0)->text(0) == "正文.txt", "numeric size sorting");
        combos[2]->setCurrentIndex(0);
        edits[3]->setText("0"); search->click();
        check(tree->topLevelItemCount() == 0, "max size filter"); edits[3]->clear();
        edits[4]->setText("2099-01-01"); search->click();
        check(tree->topLevelItemCount() == 0, "modified date filter"); edits[4]->clear();
        fs->change_directory(fs->find_by_path("/工作")); search->click();
        check(tree->topLevelItemCount() == 0, "current scope excludes other directories");
        combos[0]->setCurrentIndex(1); search->click();
        check(tree->topLevelItemCount() == 2, "global scope includes other directories");
        fs->change_directory(root); combos[0]->setCurrentIndex(0);
        edits[0]->clear(); edits[1]->setText("cpp"); search->click();
        check(tree->topLevelItemCount() == 0, "suffix filter");
        edits[1]->clear(); edits[0]->setText("报告"); search->click();
        tree->setCurrentItem(tree->topLevelItem(0));
        QPushButton* classify = nullptr;
        for (auto button : buttons) if (button->text().startsWith("为选中")) classify = button;
        check(classify, "classification control");
        // Supply semantic evidence through the selected file's content.
        auto selected = fs->find_by_path("/报告.txt");
        fs->set_content(selected, "项目会议报告");
        auto dismiss = [](int answer) { QTimer::singleShot(100, [answer] {
            if (auto box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->button(static_cast<QMessageBox::StandardButton>(answer))->click();
        }); };
        dismiss(QMessageBox::No); classify->click();
        check(fs->get_path(selected) == "/报告.txt", "decline leaves file in place");
        dismiss(QMessageBox::Yes); classify->click();
        
        check(fs->get_path(selected) == "/工作/报告.txt", "confirmed classification moves file");
        window.on_btn_undo_clicked();
        check(fs->get_path(selected) == "/报告.txt", "classification undo restores file");
        check(window.save_data(), "persistence save");
        QFile archive("data_acceptance.txt"); check(archive.open(QIODevice::ReadOnly), "archive exists");
        auto bytes = archive.readAll();
        check(!bytes.contains(QByteArray("PRIVATE_SECRET_82746").toBase64()), "private plaintext not stored as base64");
        dialog->grab().save("advanced-tools.png");
        dialog->accept();
    });
    window.open_advanced_tools();
    MainWindow reloaded("acceptance");
    auto secret = reloaded.file_system->find_by_path("/隐私/报告秘密.txt");
    check(secret && secret->get_content().empty() && !secret->get_sealed_content().empty(), "restart loads private ciphertext without plaintext");
    qunsetenv("DEEPSEEK_API_KEY");
    QTimer::singleShot(100, [] {
        auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(dialog, "AI dialog opens");
        for (auto button : dialog->findChildren<QPushButton*>()) if (button->text().startsWith("发送")) button->click();
        dialog->accept();
    });
    window.open_ai_tools();
    std::cout << "[PASS] 3000-file index/DFS parity, search, name priority, optional body, privacy, suffix, classification cancel/confirm/undo, encrypted persistence, missing AI key\n";
}
};
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    app.setFont(QFont("Microsoft YaHei", 10));
    app.setStyleSheet(app_theme());
    AcceptanceProbe::run();
}
