#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "FileNode.h"
#include "FileSystem.h"
#include <stack>
#include <string>
#include <QList>
#include <QListWidgetItem>
#include <QTreeWidgetItem>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_btn_new_folder_clicked();

    void on_btn_delete_clicked();

    void on_btn_new_file_clicked();

    void on_btn_rename_clicked();

    void on_btn_back_clicked();

    void on_btn_forward_clicked();

    void on_btn_up_clicked();

    void on_btn_search_clicked();

    void on_list_files_itemDoubleClicked(QListWidgetItem *item);

    void on_tree_dir_itemClicked(QTreeWidgetItem *item, int column);

    void on_btn_copy_clicked();

    void on_btn_move_clicked();

private:
    Ui::MainWindow *ui;

    FileSystem *file_system;    // 底层文件系统核心
    FileNode *current_dir_node; // 当前所在目录的节点指针



    // 导航用的两个栈
    std::stack<std::string> back_stack;
    std::stack<std::string> forward_stack;

    // 辅助函数
    void navigate_to(const std::string& path);
    void search_recursive(FileNode* node, const QString& keyword, QList<FileNode*>& results);

    void build_tree_item(FileNode* node, QTreeWidgetItem* parentItem);

    void refresh_tree();       // 刷新左侧目录树
    void refresh_file_list();  // 刷新右侧文件列表
};
#endif // MAINWINDOW_H
