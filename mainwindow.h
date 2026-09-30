#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "FileNode.h"
#include "FileSystem.h"

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

private:
    Ui::MainWindow *ui;

    FileSystem *file_system;    // 底层文件系统核心
    FileNode *current_dir_node; // 当前所在目录的节点指针

    void refresh_tree();       // 刷新左侧目录树
    void refresh_file_list();  // 刷新右侧文件列表
};
#endif // MAINWINDOW_H
