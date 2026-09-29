#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QListWidgetItem>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 1. 初始化底层系统
    file_system = new FileSystem();
    current_dir_node = file_system->get_root(); // 根节点

    // 2. 刷新界面
    refresh_tree();
    refresh_file_list();
}

MainWindow::~MainWindow()
{
    delete ui;
    delete file_system; // 记得释放内存
}

void MainWindow::refresh_file_list() {
    ui->list_files->clear();
    if (current_dir_node == nullptr) return;

    // 1. 改用 getter 方法获取第一个子节点
    FileNode *child = current_dir_node->get_first_child();

    while (child != nullptr) {
        // 2. 改用 get_name() 获取名字，它返回的是 const std::string&，可以无缝转换
        QString nameStr = QString::fromStdString(child->get_name());
        QListWidgetItem *item = new QListWidgetItem(nameStr);

        // 把节点指针存进 item，方便以后点击时用
        item->setData(Qt::UserRole, QVariant::fromValue(child));
        ui->list_files->addItem(item);

        // 3. 改用 get_next_sibling() 获取下一个兄弟节点
        child = child->get_next_sibling();
    }
}

void MainWindow::refresh_tree() {
    ui->tree_dir->clear(); // 注意这里改成了 tree_dir

}
void MainWindow::on_btn_new_folder_clicked()
{

        // 1. 弹出输入框，让用户输入名字
        bool ok;
        QString name = QInputDialog::getText(this, "新建文件夹", "请输入文件夹名：", QLineEdit::Normal, "", &ok);

        // 2. 如果用户点了确定，且名字不为空
        if (ok && !name.isEmpty()) {
            // 3. 调用底层接口！
            FileNode* newNode = file_system->create_folder(current_dir_node, name.toStdString());

            // 4. 判断结果
            if (newNode != nullptr) {
                refresh_file_list(); // 成功，刷新右侧列表
                refresh_tree();      // 刷新左侧目录树
                QMessageBox::information(this, "成功", "文件夹创建成功！");
            } else {
                QMessageBox::warning(this, "失败", "创建失败，可能存在重名或非法字符！");
            }
        }

}



void MainWindow::on_btn_delete_clicked()
{


        // 1. 获取当前选中的项
        QListWidgetItem *item = ui->list_files->currentItem();
        if (item == nullptr) {
            QMessageBox::warning(this, "提示", "请先选择要删除的文件或文件夹！");
            return;
        }

        // 2. 取出底层节点指针
        FileNode* selectedNode = item->data(Qt::UserRole).value<FileNode*>();
        if (selectedNode == nullptr) return;

        // 3. 二次确认，防止用户误点
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "确认删除",
                                      "确定要删除选中的项吗？(将移入回收站)",
                                      QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            // 4. 调用底层 delete_node (放入回收站)
            bool success = file_system->delete_node(selectedNode);

            if (success) {
                refresh_file_list();
                refresh_tree();
                QMessageBox::information(this, "成功", "已移入回收站！");
            } else {
                QMessageBox::warning(this, "失败", "删除失败！可能是根目录不可删除。");
            }
        }

}


void MainWindow::on_btn_new_file_clicked()
{

        bool ok;
        // 弹出输入框，默认文件名给个提示
        QString name = QInputDialog::getText(this, "新建文件", "请输入文件名：", QLineEdit::Normal, "新建文件.txt", &ok);

        if (ok && !name.isEmpty()) {
            // 调用底层接口，第三个参数是模拟文件的大小，我们随便传个 0 或 1024
            FileNode* newNode = file_system->create_file(current_dir_node, name.toStdString(), "");

            if (newNode != nullptr) {
                refresh_file_list(); // 刷新右侧列表
                refresh_tree();      // 刷新左侧目录树
                QMessageBox::information(this, "成功", "文件创建成功！");
            } else {
                QMessageBox::warning(this, "失败", "创建失败！可能存在重名或包含了非法字符(/、|)。");
            }
        }

}


void MainWindow::on_btn_rename_clicked()
{

        // 1. 获取当前在右侧列表选中的项
        QListWidgetItem *item = ui->list_files->currentItem();
        if (item == nullptr) {
            QMessageBox::warning(this, "提示", "请先在右侧列表中选择一个文件或文件夹！");
            return;
        }

        // 2. 从控件中取出底层节点指针
        FileNode* selectedNode = item->data(Qt::UserRole).value<FileNode*>();
        if (selectedNode == nullptr) return;

        bool ok;
        // 3. 弹出输入框，默认显示原名字
        QString newName = QInputDialog::getText(this, "重命名", "请输入新名称：", QLineEdit::Normal,
                                                QString::fromStdString(selectedNode->get_name()), &ok);

        if (ok && !newName.isEmpty()) {
            // 4. 调用底层接口
            bool success = file_system->rename_node(selectedNode, newName.toStdString());

            if (success) {
                refresh_file_list();
                refresh_tree();
                QMessageBox::information(this, "成功", "重命名成功！");
            } else {
                QMessageBox::warning(this, "失败", "重命名失败！可能存在重名或非法字符。");
            }
        }

}

