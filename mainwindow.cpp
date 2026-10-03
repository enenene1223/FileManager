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



// 递归生成树的辅助函数
void MainWindow::build_tree_item(FileNode* node, QTreeWidgetItem* parentItem) {
    if (node == nullptr) return;

    FileNode* child = node->get_first_child();
    while (child != nullptr) {
        // 左侧树只显示文件夹（根据《开发须知》）
        if (child->is_directory()) {
            QTreeWidgetItem* item = new QTreeWidgetItem(parentItem);
            item->setText(0, QString::fromStdString(child->get_name()));

            // 把底层的 FileNode 指针存进 QTreeWidgetItem 的 UserRole 里！
            item->setData(0, Qt::UserRole, QVariant::fromValue(child));

            // 递归创建子节点
            build_tree_item(child, item);
        }
        child = child->get_next_sibling();
    }
}




void MainWindow::refresh_tree() {
    ui->tree_dir->clear();
    if (current_dir_node == nullptr) return;

    // 从根节点开始构建树
    QTreeWidgetItem* rootItem = new QTreeWidgetItem(ui->tree_dir);
    rootItem->setText(0, "我的电脑");
    rootItem->setData(0, Qt::UserRole, QVariant::fromValue(file_system->get_root()));
    build_tree_item(file_system->get_root(), rootItem);

    ui->tree_dir->expandAll(); // 默认展开全部
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


void MainWindow::on_btn_back_clicked()
{
    if (back_stack.empty()) {
        ui->statusbar->showMessage("已经在最开始的位置了", 2000);
        return;
    }
    // 将当前路径压入前进栈
    forward_stack.push(file_system->get_path(current_dir_node));
    // 取出后退栈的历史路径
    std::string targetPath = back_stack.top();
    back_stack.pop();
    // 跳转
    navigate_to(targetPath);
}


void MainWindow::on_btn_forward_clicked()
{
    if (forward_stack.empty()) {
        ui->statusbar->showMessage("没有可以前进的历史", 2000);
        return;
    }
    // 将当前路径压入后退栈
    back_stack.push(file_system->get_path(current_dir_node));
    // 取出前进栈的历史路径
    std::string targetPath = forward_stack.top();
    forward_stack.pop();
    // 跳转
    navigate_to(targetPath);
}


void MainWindow::on_btn_up_clicked()
{
    if (current_dir_node == nullptr || current_dir_node->get_parent() == nullptr) {
        ui->statusbar->showMessage("已经是根目录了", 2000);
        return;
    }

    // 记录当前路径到后退栈
    back_stack.push(file_system->get_path(current_dir_node));
    // 进入新目录后，前进栈必须清空
    while (!forward_stack.empty()) forward_stack.pop();

    // 切换到父节点
    current_dir_node = current_dir_node->get_parent();

    // 刷新界面
    refresh_file_list();
    refresh_tree();
    ui->lineEdit_path->setText(QString::fromStdString(file_system->get_path(current_dir_node)));
}


void MainWindow::on_btn_search_clicked()
{
    // 注意：这里必须是 lineEdit_search，如果你还没拖输入框，请立刻去UI里加一个并改名为 lineEdit_search
    QString keyword = ui->lineEdit_search->text().trimmed();
    if (keyword.isEmpty()) {
        QMessageBox::warning(this, "提示", "请在搜索框中输入关键字！");
        return;
    }

    QList<FileNode*> results;
    // 从当前目录开始递归搜索
    search_recursive(current_dir_node, keyword, results);

    if (results.isEmpty()) {
        QMessageBox::information(this, "搜索", "未找到匹配的内容！");
        return;
    }

    // 把搜索结果展示在右侧列表中
    ui->list_files->clear();
    for (FileNode* node : results) {
        // 显示完整路径，方便区分不同文件夹下的同名文件
        QString displayPath = QString::fromStdString(file_system->get_path(node));
        QListWidgetItem* item = new QListWidgetItem(displayPath);
        item->setData(Qt::UserRole, QVariant::fromValue(node));
        ui->list_files->addItem(item);
    }
    ui->statusbar->showMessage(QString("共找到 %1 个结果").arg(results.size()), 3000);
}


// 统一的跳转逻辑
void MainWindow::navigate_to(const std::string& path) {
    FileNode* target = file_system->find_by_path(path);
    if (target != nullptr && target->is_directory()) {
        current_dir_node = target;
        refresh_file_list();
        refresh_tree();
        // 更新路径栏显示
        ui->lineEdit_path->setText(QString::fromStdString(file_system->get_path(current_dir_node)));
    } else {
        // 如果历史路径已失效
        ui->lineEdit_path->setText(QString::fromStdString(file_system->get_path(current_dir_node)));
        ui->statusbar->showMessage("历史路径不存在或已被删除！", 2000);
    }
}

// 递归搜索（DFS）
void MainWindow::search_recursive(FileNode* node, const QString& keyword, QList<FileNode*>& results) {
    if (node == nullptr) return;

    QString name = QString::fromStdString(node->get_name());
    // 模糊匹配，不区分大小写
    if (name.contains(keyword, Qt::CaseInsensitive)) {
        results.append(node);
    }

    // 递归遍历子节点
    FileNode* child = node->get_first_child();
    while (child != nullptr) {
        search_recursive(child, keyword, results);
        child = child->get_next_sibling();
    }
}

void MainWindow::on_list_files_itemDoubleClicked(QListWidgetItem *item)
{

        if (item == nullptr) return;

        // 1. 取出底层节点指针
        FileNode* targetNode = item->data(Qt::UserRole).value<FileNode*>();
        if (targetNode == nullptr) return;

        // 2. 判断是不是文件夹（只有文件夹能双击进入）
        if (targetNode->is_directory()) {
            // ===== 导航核心逻辑：记录历史，清空前进栈 =====
            back_stack.push(file_system->get_path(current_dir_node)); // 把当前路径压入后退栈
            while (!forward_stack.empty()) forward_stack.pop();       // 清空前进栈（符合浏览器规则）
            // ============================================

            // 3. 切换当前目录节点
            current_dir_node = targetNode;

            // 4. 刷新界面：右侧列表、路径栏
            refresh_file_list();
            refresh_tree(); // 如果左侧树也能同步高亮就更好
            ui->lineEdit_path->setText(QString::fromStdString(file_system->get_path(current_dir_node)));

            ui->statusbar->showMessage("已进入文件夹：" + QString::fromStdString(current_dir_node->get_name()), 2000);
        } else {
            // 双击的是文件，弹窗提示（或者以后用来显示文件属性）
            QMessageBox::information(this, "提示", "这是一个文件，双击不能进入。");
        }

}


void MainWindow::on_tree_dir_itemClicked(QTreeWidgetItem *item, int column)
{

        if (item == nullptr) return;
        FileNode* clickedNode = item->data(0, Qt::UserRole).value<FileNode*>();

        // 只有点中文件夹才切换
        if (clickedNode != nullptr && clickedNode->is_directory() && clickedNode != current_dir_node) {
            // 记录导航历史
            back_stack.push(file_system->get_path(current_dir_node));
            while (!forward_stack.empty()) forward_stack.pop();

            current_dir_node = clickedNode;
            refresh_file_list();
            ui->lineEdit_path->setText(QString::fromStdString(file_system->get_path(current_dir_node)));
        }

}




