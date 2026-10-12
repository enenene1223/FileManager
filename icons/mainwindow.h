#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QCloseEvent>
#include "FileNode.h"
#include "FileSystem.h"
#include <stack>
#include <string>
#include <QList>
#include <iosfwd>
#include <QListWidgetItem>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QStringList>
#include "Operation.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit  MainWindow(QString username, QWidget *parent = nullptr);
    ~MainWindow() override;
    bool is_logout_action = false; // 区分是否是注销

protected:
    void closeEvent(QCloseEvent *event) override;


private slots:
    void on_btn_delete_clicked();

    void on_btn_rename_clicked();

    void on_btn_back_clicked();
    void on_btn_forward_clicked();
    void on_btn_up_clicked();

    void on_btn_search_clicked();

    void on_list_files_itemDoubleClicked(QListWidgetItem *item);

    void on_list_files_itemChanged(QListWidgetItem *item);

    void on_list_files_itemPressed(QListWidgetItem *item);

    void on_tree_dir_itemClicked(QTreeWidgetItem *item, int column);

    void on_btn_copy_clicked();
    void on_btn_cut_clicked();    void on_btn_paste_clicked();

    void on_btn_refresh_clicked();

    void save_node_recursive(FileNode* node, std::ofstream& out); // 递归辅助函数

    void on_btn_test_pin_clicked();//临时测试置顶功能

    // 回收站
    void on_btn_restore_clicked();
    void on_btn_permanent_delete_clicked();
    void on_btn_clear_recycle_clicked();

    void on_btn_undo_clicked();

    void on_btn_reset_clicked();
    void on_btn_logout_clicked(); // 注销按钮
    void on_btn_exit_clicked();   // 退出按钮

private:

    Ui::MainWindow *ui;

    FileSystem *file_system;    // 底层文件系统核心

    bool has_privacy_access = false; // 当前会话是否已通过隐私密码验证
    bool show_privacy_mode = false;  // 是否处于“显示隐私文件”模式

    bool verify_privacy_password(const QString& password); // 校验隐私密码
    QString hashPassword(const QString& password);         // 哈希工具（复用登录界面的逻辑）

    // 导航用的两个栈
    std::stack<std::string> back_stack;
    std::stack<std::string> forward_stack;

    std::stack<Operation> undo_stack;

    QString current_user;

    // 辅助函数
    void navigate_to(
        const std::string& path,
        bool refreshTree = true,
        bool recordHistory = true
        );

    void build_tree_item(FileNode* node, QTreeWidgetItem* parentItem);

    void on_btn_new_file_clicked();// 新建文件
    void on_btn_new_folder_clicked();// 新建文件夹

    void refresh_tree();       // 刷新左侧目录树
    void refresh_file_list();  // 刷新右侧文件列表

    void addToListWidget(FileNode* node); // 声明这个新函数

    void toggle_pin(FileNode* node);

    // 显示文件或文件夹的只读属性
    void show_node_properties(FileNode* node);

    // 持久化
    void save_data();
    void load_data();

    bool recycle_mode = false;

    // 删除选中文件或文件夹
    void delete_selected_item(bool needConfirm);

    QListWidgetItem *last_clicked_item = nullptr;   //双击重命名
    qint64 last_click_time = 0;
    QTimer *rename_click_timer = nullptr;

    QStringList history_list;//访问记录
    int history_replace_index = 0;

    enum class ClipboardMode
    {
        None,
        Copy,
        Cut
    };

    FileNode *clipboard_node = nullptr;
    ClipboardMode clipboard_mode = ClipboardMode::None;

};

#endif // MAINWINDOW_H