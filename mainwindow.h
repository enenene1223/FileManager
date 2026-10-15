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
#include <QJsonObject>
#include <QHeaderView>
#include <QPushButton>
#include <QHash>
#include <QSet>
#include <QStackedWidget>
#include "AiCredentialStore.h"
#include "Operation.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    friend class AcceptanceProbe;
    Q_OBJECT

public:
    explicit  MainWindow(QString username, QWidget *parent = nullptr);
    ~MainWindow() override;
    bool is_logout_action = false; // 区分是否是注销

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;


private slots:
    void on_action_lock_clicked();
    void on_action_show_privacy_triggered();
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

    void save_node_recursive(FileNode* node, std::ostream& out); // 递归辅助函数

    // 回收站
    void on_btn_restore_clicked();
    void on_btn_permanent_delete_clicked();
    void on_btn_clear_recycle_clicked();

    void on_btn_undo_clicked();

    void on_btn_reset_clicked();
    void on_btn_logout_clicked(); // 注销按钮
    void on_btn_exit_clicked();   // 退出按钮

private:

    QStringList classification_history_;
    QList<bool> classification_history_privacy_;
    void setup_integrated_tools();
    void run_smart_search();
    QList<QPair<FileNode*, int>> search_hits(bool indexed) const;
    void auto_classify();
    void apply_file_sort();
    QHeaderView* file_header_ = nullptr;
    QPushButton* search_options_button_ = nullptr;
    QPushButton* classify_button_ = nullptr;
    QStackedWidget* path_stack_ = nullptr;
    QWidget* breadcrumb_bar_ = nullptr;
    void sync_breadcrumbs();
    void edit_path();
    bool search_mode_ = false;
    bool search_global_ = false;
    bool search_body_ = false;
    int search_type_ = 0;
    QString search_after_, search_before_, search_min_, search_max_;
    int file_sort_ = 0;
    bool sort_descending_ = false;
    mutable bool search_index_valid_ = false;
    mutable QList<FileNode*> search_index_nodes_;
    mutable QHash<QString, QSet<FileNode*>> search_name_index_;
    void open_ai_tools();
    void open_ai_settings();
    AiCredentialStore ai_credentials_;
    QJsonObject ai_search_conditions_;
    Ui::MainWindow *ui;

    FileSystem *file_system = nullptr;    // 底层文件系统核心

    bool has_privacy_access = false; // 当前会话是否已通过隐私密码验证
    bool show_privacy_mode = false;  // 是否处于“显示隐私文件”模式
    std::string encryptContent(const std::string& content);
    std::string decryptContent(const std::string& encryptedContent);

    // AES-256-GCM / PBKDF2：salt 写入 KDF1，key 仅在已授权会话中保留。
    std::string crypto_salt_;
    int crypto_iterations_ = 300000;
    std::string crypto_session_key_;
    bool unlock_encrypted_content(const QString& password);
    bool prepare_encrypted_content_for_save();
    void discard_privacy_key();

    bool verify_privacy_password(const QString& password); // 校验隐私密码
    // 判断节点自身或祖先文件夹是否被锁定
    bool is_privacy_protected(FileNode* node) const;
    // 判断当前会话是否允许访问该节点
    bool can_access_node(FileNode* node) const;
    QString hashPassword(const QString& password);  // 哈希工具（复用登录界面的逻辑）

    // 导航用的两个栈
    std::stack<std::string> back_stack;
    std::stack<std::string> forward_stack;

    std::stack<Operation> undo_stack;

    QString current_user;

    void navigate_to( // 导航
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

    void toggle_pin(FileNode* node);// 置顶

    // 显示文件或文件夹的只读属性
    void show_node_properties(FileNode* node);

    // 打开内置 TXT 编辑器
    void open_text_editor(FileNode* node);

    // 持久化
    bool save_data();
    void load_data();

    // 加载失败时禁止覆写原始存档，等待用户手动恢复
    bool data_load_blocked_ = false;
    QString data_load_error_;

    // 回收站
    bool recycle_mode = false;

    // 当前是否正在查看“隐私文件总览”
    bool privacy_overview_mode = false;

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
