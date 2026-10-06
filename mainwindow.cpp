#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QListWidgetItem>

#include <QMenu>
#include <QAction>

#include <QSignalBlocker>
#include <QTimer>
#include <QAbstractItemView>

#include <QDateTime>
#include <QApplication>
#include <qtoolbutton.h>

#include <fstream>
#include <string>
#include <sstream>
#include <QCloseEvent>

#include <algorithm>
#include <QHeaderView>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 左侧目录树使用两列：名称 + 置顶图标
    ui->tree_dir->setColumnCount(2);

    QHeaderView *header = ui->tree_dir->header();

    header->setStretchLastSection(false);
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::Fixed);
    header->resizeSection(1, 32);

    ui->tree_dir->setHeaderHidden(true);

    // 关闭 QListWidget 默认的双击编辑，
    // 只允许我们在“新建”完成后主动进入重命名状态
    ui->list_files->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // 初始化底层系统
    file_system = new FileSystem();
    current_dir_node = file_system->get_root(); // 根节点

    //自动加载上一次保存的数据
    load_data();

    // 刷新界面
    refresh_tree();
    refresh_file_list();

    // 新建的下拉菜单
    QMenu *newMenu = new QMenu(this);

    QAction *newFileAction =
        newMenu->addAction("新建文件");

    QAction *newFolderAction =
        newMenu->addAction("新建文件夹");

    ui->btn_new->setMenu(newMenu);
    ui->btn_new->setPopupMode(QToolButton::InstantPopup);

    connect(newFileAction,
            &QAction::triggered,
            this,
            &MainWindow::on_btn_new_file_clicked);

    connect(newFolderAction,
            &QAction::triggered,
            this,
            &MainWindow::on_btn_new_folder_clicked);

}

MainWindow::~MainWindow()
{
    delete ui;
    delete file_system; // 记得释放内存
}

void MainWindow::refresh_file_list() {

        ui->list_files->clear();
        if (current_dir_node == nullptr) return;

        // 第一趟遍历：先加载所有“置顶”的
        FileNode *child = current_dir_node->get_first_child();
        while (child != nullptr) {
            if (child->is_pinned()) {
                addToListWidget(child);
            }
            child = child->get_next_sibling();
        }

        // 第二趟遍历：再加载所有“非置顶”的
        child = current_dir_node->get_first_child(); // 重置指针，从头开始
        while (child != nullptr) {
            if (!child->is_pinned()) {
                addToListWidget(child);
            }
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
            item->setData(
                0,
                Qt::UserRole,
                QVariant::fromValue(child)
            );

            // 递归创建子节点
            build_tree_item(child, item);
        }
        child = child->get_next_sibling();
    }
}




void MainWindow::refresh_tree()
{
    ui->tree_dir->clear();

    if (current_dir_node == nullptr)
        return;

    // ==================================================
    // 第一部分：回收站
    // ==================================================
    QTreeWidgetItem* recycleItem =
        new QTreeWidgetItem(ui->tree_dir);

    recycleItem->setText(0, "回收站");


    // ==================================================
    // 第二部分：真正的置顶项目
    // 按置顶先后顺序排列：
    // 先置顶的在上，后置顶的在下
    // ==================================================
    QList<FileNode*> pinnedNodes;

    file_system->traverse(
        file_system->get_root(),
        [&pinnedNodes, this](FileNode* node)
        {
            if (node == file_system->get_root())
                return;

            if (file_system->is_pinned(node))
            {
                pinnedNodes.append(node);
            }
        }
        );

    std::sort(
        pinnedNodes.begin(),
        pinnedNodes.end(),
        [](FileNode* a, FileNode* b)
        {
            return a->get_pin_order() <
                   b->get_pin_order();
        }
        );

    for (FileNode* node : pinnedNodes)
    {
        QTreeWidgetItem* item =
            new QTreeWidgetItem(ui->tree_dir);

        // 第一列：文件名
        item->setText(
            0,
            QString::fromStdString(
                node->get_name()
                )
            );

        // 第二列：置顶符号
        item->setText(1, "📌");

        item->setTextAlignment(
            1,
            Qt::AlignRight | Qt::AlignVCenter
            );

        // 保存节点指针
        item->setData(
            0,
            Qt::UserRole,
            QVariant::fromValue(node)
            );
    }


    // ==================================================
    // 第三部分：最近访问
    // history_list 内部仍然保存完整路径
    // 左侧只显示最后一级
    // ==================================================
    for (const QString& path : history_list)
    {
        FileNode* node =
            file_system->find_by_path(
                path.toStdString()
                );

        if (node == nullptr ||
            !node->is_directory())
        {
            continue;
        }

        // 已经置顶的项目不在最近访问中重复显示
        if (file_system->is_pinned(node))
            continue;

        QTreeWidgetItem* item =
            new QTreeWidgetItem(ui->tree_dir);

        QString displayName = path;

        int lastSlash =
            displayName.lastIndexOf('/');

        if (lastSlash >= 0)
        {
            displayName =
                displayName.mid(lastSlash + 1);
        }

        // 根目录 "/" 的最后一级为空
        if (displayName.isEmpty())
        {
            displayName = "我的电脑";
        }

        item->setText(0, displayName);

        item->setData(
            0,
            Qt::UserRole,
            QVariant::fromValue(node)
            );
    }


    // ==================================================
    // 第四部分：真实目录树
    // ==================================================
    QTreeWidgetItem* rootItem =
        new QTreeWidgetItem(ui->tree_dir);

    rootItem->setText(0, "我的电脑");

    rootItem->setData(
        0,
        Qt::UserRole,
        QVariant::fromValue(
            file_system->get_root()
            )
        );

    build_tree_item(
        file_system->get_root(),
        rootItem
        );

    rootItem->setExpanded(true);
}


void MainWindow::on_btn_new_folder_clicked()
{
    QString name = "新建文件夹";

    // 如果存在同名文件夹，则自动生成：
    // 新建文件夹 (2)
    // 新建文件夹 (3)
    // ...
    int index = 2;

    while (file_system->find_child(
               current_dir_node,
               name.toStdString()) != nullptr)
    {
        name = QString("新建文件夹 (%1)").arg(index);
        ++index;
    }

    // 直接创建
    FileNode* newNode =
        file_system->create_folder(
            current_dir_node,
            name.toStdString()
            );

    if (newNode == nullptr) {
        QMessageBox::warning(
            this,
            "失败",
            "创建文件夹失败！"
            );
        return;
    }

    // 刷新界面
    refresh_file_list();
    refresh_tree();

    // 找到刚刚创建的节点
    QListWidgetItem* newItem = nullptr;

    for (int i = 0; i < ui->list_files->count(); ++i) {
        QListWidgetItem* item = ui->list_files->item(i);

        FileNode* node =
            item->data(Qt::UserRole).value<FileNode*>();

        if (node == newNode) {
            newItem = item;
            break;
        }
    }

    if (newItem != nullptr) {
        QTimer::singleShot(0, this, [this, newItem]() {
            ui->list_files->setCurrentItem(newItem);
            ui->list_files->editItem(newItem);
        });
    }
}



void MainWindow::on_btn_new_file_clicked()
{
    // 默认名称
    QString name = "新建文件.txt";

    // 如果已经存在同名文件，则自动生成：
    // 新建文件 (2).txt
    // 新建文件 (3).txt
    // ...
    int index = 2;

    while (file_system->find_child(
               current_dir_node,
               name.toStdString()) != nullptr)
    {
        name = QString("新建文件 (%1).txt").arg(index);
        ++index;
    }

    // 直接创建，不再弹输入框
    FileNode* newNode =
        file_system->create_file(
            current_dir_node,
            name.toStdString(),
            ""
            );

    if (newNode == nullptr) {
        QMessageBox::warning(
            this,
            "失败",
            "创建文件失败！"
            );
        return;
    }

    // 刷新界面
    refresh_file_list();
    refresh_tree();

    // 在刷新后的列表中找到刚刚创建的节点
    QListWidgetItem* newItem = nullptr;

    for (int i = 0; i < ui->list_files->count(); ++i) {
        QListWidgetItem* item = ui->list_files->item(i);

        FileNode* node =
            item->data(Qt::UserRole).value<FileNode*>();

        if (node == newNode) {
            newItem = item;
            break;
        }
    }

    if (newItem != nullptr) {
        // 下一轮事件循环中进入编辑状态
        // 这样视觉上更接近 Windows 的新建效果
        QTimer::singleShot(0, this, [this, newItem]() {
            ui->list_files->setCurrentItem(newItem);
            ui->list_files->editItem(newItem);
        });
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
                file_system->set_pinned(selectedNode, false);

                refresh_file_list();
                refresh_tree();
            } else {
                QMessageBox::warning(this, "失败", "删除失败！可能是根目录不可删除。");
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
            } else {
                QMessageBox::warning(this, "失败", "重命名失败！可能存在重名或非法字符。");
            }
        }

}


void MainWindow::on_list_files_itemChanged(QListWidgetItem *item)
{
    if (item == nullptr)
        return;

    // 取出对应的 FileNode
    FileNode* node =
        item->data(Qt::UserRole).value<FileNode*>();

    if (node == nullptr)
        return;

    QString newName = item->text();

    QString oldName =
        QString::fromStdString(node->get_name());

    // 名称没有改变，不需要处理
    if (newName == oldName)
        return;

    // 不能为空
    if (newName.trimmed().isEmpty()) {
        QSignalBlocker blocker(ui->list_files);
        item->setText(oldName);

        QMessageBox::warning(
            this,
            "重命名失败",
            "文件名不能为空！"
            );

        return;
    }

    // 调用已有核心接口
    bool success =
        file_system->rename_node(
            node,
            newName.toStdString()
            );

    if (!success) {
        // 核心拒绝重命名，例如：
        // 同目录重名
        // 非法字符
        QSignalBlocker blocker(ui->list_files);

        item->setText(oldName);

        QMessageBox::warning(
            this,
            "重命名失败",
            "名称非法或当前目录已经存在同名文件/文件夹。"
            );

        return;
    }

    // 重命名成功后刷新界面
    refresh_file_list();
    refresh_tree();

    // 如果改的是当前目录或者其他可能影响路径显示的节点，
    // 同步刷新当前路径栏
    ui->lineEdit_path->setText(
        QString::fromStdString(
            file_system->get_path(current_dir_node)
            )
        );
}


void MainWindow::on_list_files_itemPressed(QListWidgetItem *item)
{
    if (item == nullptr)
        return;

    qint64 current_time =
        QDateTime::currentMSecsSinceEpoch();

    // 如果是同一个项目，并且距离上一次点击
    // 已经超过双击时间间隔，认为是“再次点击重命名”
    if (last_clicked_item == item &&
        current_time - last_click_time > QApplication::doubleClickInterval())
    {
        ui->list_files->editItem(item);

        // 重置，避免连续误触发
        last_clicked_item = nullptr;
        last_click_time = 0;
        return;
    }

    // 第一次点击，或者点击了其他项目
    last_clicked_item = item;
    last_click_time = current_time;
}



void MainWindow::on_btn_copy_clicked()
{
    QListWidgetItem *item = ui->list_files->currentItem();

    if (item == nullptr)
        return;

    FileNode *node =
        item->data(Qt::UserRole).value<FileNode *>();

    if (node == nullptr)
        return;

    clipboard_node = node;
    clipboard_mode = ClipboardMode::Copy;
}


void MainWindow::on_btn_move_clicked()
{
    QListWidgetItem *item = ui->list_files->currentItem();

    if (item == nullptr)
        return;

    FileNode *node =
        item->data(Qt::UserRole).value<FileNode *>();

    if (node == nullptr)
        return;

    clipboard_node = node;
    clipboard_mode = ClipboardMode::Move;
}


void MainWindow::on_btn_paste_clicked()
{
    if (clipboard_node == nullptr)
        return;

    if (clipboard_mode == ClipboardMode::Copy)
    {
        FileNode *new_node =
            file_system->copy_node(
                clipboard_node,
                current_dir_node
                );

        if (new_node == nullptr)
        {
            QMessageBox::warning(
                this,
                "粘贴失败",
                "复制文件或文件夹失败！"
                );
            return;
        }

        refresh_tree();
        refresh_file_list();

        // 复制模式下，剪贴板继续保留
        // 可以继续粘贴到其他目录
    }
    else if (clipboard_mode == ClipboardMode::Move)
    {
        bool success =
            file_system->move_node(
                clipboard_node,
                current_dir_node
                );

        if (!success)
        {
            QMessageBox::warning(
                this,
                "粘贴失败",
                "移动文件或文件夹失败！"
                );
            return;
        }

        refresh_tree();
        refresh_file_list();

        // 移动成功以后清空剪贴板
        clipboard_node = nullptr;
        clipboard_mode = ClipboardMode::None;
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

    if (target != nullptr && target->is_directory()) {
        // ... 更新界面 ...

        // 记录历史记录
        QString currentPath = QString::fromStdString(path);
        history_list.removeAll(currentPath); // 去重
        history_list.prepend(currentPath);   // 放到最前面
        if (history_list.size() > 20) history_list.removeLast(); // 限制最多20条
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

            QString currentPath = QString::fromStdString(file_system->get_path(clickedNode));
            history_list.removeAll(currentPath);
            history_list.prepend(currentPath);
            if (history_list.size() > 20) history_list.removeLast();

            current_dir_node = clickedNode;
            refresh_file_list();
            ui->lineEdit_path->setText(QString::fromStdString(file_system->get_path(current_dir_node)));
        }

}

void MainWindow::on_btn_refresh_clicked()
{
    refresh_tree();
    refresh_file_list();
}


void MainWindow::save_node_recursive(FileNode* node, std::ofstream& out) {
    if (node == nullptr) return;

    // 1. 获取当前节点的路径（底层接口）
    std::string path = file_system->get_path(node);

    // 2. 按格式写入： D或F | 路径 | 创建时间 | 修改时间


    // 原来： out << ... << get_modified_time() << "\n";
    // 改成：
    out << (node->is_directory() ? "D" : "F") << "|"
        << path << "|"
        << node->get_created_time() << "|"
        << node->get_modified_time() << "|"
        << (node->is_pinned() ? "1" : "0") << "\n"; // 👈 加上置顶状态

    // 3. 递归遍历子节点
    FileNode* child = node->get_first_child();
    while (child != nullptr) {
        save_node_recursive(child, out);
        child = child->get_next_sibling();
    }
}


void MainWindow::save_data() {
    std::ofstream out("data.txt");
    if (!out.is_open()) {
        QMessageBox::warning(this, "错误", "无法创建保存文件！");
        return;
    }
    save_node_recursive(file_system->get_root(), out);
    out.close();
    ui->statusbar->showMessage("数据保存成功！", 2000);
}





void MainWindow::load_data() {
    std::ifstream in("data.txt");
    if (!in.is_open()) return; // 第一次运行没有文件，直接返回，正常

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        // 1. 按照 '|' 分割字符串
        std::stringstream ss(line);
        std::string type, path, c_time, m_time;
        std::string pinned_str;
        std::getline(ss, type, '|');
        std::getline(ss, path, '|');
        std::getline(ss, c_time, '|');
        std::getline(ss, m_time, '|');
        std::getline(ss,pinned_str,'|');




        // 2. 跳过根节点（因为 FileSystem 构造函数已经创建了根节点）
        if (path == "/" || path.empty()) continue;

        // 3. 找到父路径 (例如 /学习/C++ 的父路径是 /学习)
        size_t lastSlash = path.find_last_of('/');
        if (lastSlash == std::string::npos) continue;
        std::string parentPath = path.substr(0, lastSlash);
        if (parentPath.empty()) parentPath = "/"; // 容错处理

        std::string name = path.substr(lastSlash + 1);

        // 4. 找到父节点并创建当前节点
        FileNode* parentNode = file_system->find_by_path(parentPath);
        if (parentNode == nullptr) continue; // 父节点没找到，跳过（容错）

        FileNode* newNode =nullptr;

        if (type == "D") {
            newNode=file_system->create_folder(parentNode, name);
        } else {
            newNode=file_system->create_file(parentNode, name, ""); // 内容为空，因为只模拟文件信息
        }
        // 🛡️ 关键：设置置顶状态
        if (newNode != nullptr) {
            // 如果读到了 "1"，或者旧格式里没这个字段但你想兼容，就根据情况判断
            bool is_pinned = (pinned_str == "1");
            newNode->set_pinned(is_pinned); // 调用队友的接口

        }

    }
    in.close();

    // 5. 加载完成后，刷新界面
    refresh_tree();
    refresh_file_list();
    ui->lineEdit_path->setText(QString::fromStdString(file_system->get_path(current_dir_node)));
}

void MainWindow::closeEvent(QCloseEvent *event) {
    save_data(); // 窗口关闭时，偷偷执行一次保存
    event->accept(); // 接受关闭事件
}


bool MainWindow::is_pinned(FileNode* node) const
{
    return file_system->is_pinned(node);
}

void MainWindow::toggle_pin(FileNode* node)
{
    if (node == nullptr)
        return;

    bool newState = !file_system->is_pinned(node);

    if (!file_system->set_pinned(node, newState))
        return;

    refresh_tree();
    refresh_file_list();
}

//临时测试置顶功能
void MainWindow::on_btn_test_pin_clicked()
{
    QListWidgetItem* item = ui->list_files->currentItem();

    if (item == nullptr)
    {
        QMessageBox::information(this, "提示", "请先选择一个文件或文件夹！");
        return;
    }

    FileNode* node =
        item->data(Qt::UserRole).value<FileNode*>();

    if (node == nullptr)
        return;

    toggle_pin(node);
}


void MainWindow::addToListWidget(FileNode* node) {
    if (node == nullptr) return;

    QListWidgetItem *item = new QListWidgetItem(QString::fromStdString(node->get_name()));
    item->setData(Qt::UserRole, QVariant::fromValue(node));
    item->setFlags(item->flags() | Qt::ItemIsEditable);

    // 方案B的核心：如果置顶，就加粗字体
   // if (node->is_pinned()) {
     //   QFont font = item->font();
       // font.setBold(true);
        //item->setFont(font);
        // 也可以加个图钉图标：item->setText("📌 " + item->text());
    //}

    ui->list_files->addItem(item);
}
