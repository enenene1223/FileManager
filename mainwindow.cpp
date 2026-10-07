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
#include <QToolButton>

#include <fstream>
#include <string>
#include <sstream>
#include <QCloseEvent>

#include <algorithm>
#include <QHeaderView>

#include <utility>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    // 生成界面
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


    // 默认隐藏回收站按钮
    ui->btn_restore->hide();
    ui->btn_permanent_delete->hide();
    ui->btn_clear_recycle->hide();

}

MainWindow::~MainWindow()
{
    delete ui;
    delete file_system; // 记得释放内存
}

void MainWindow::refresh_file_list()
{
    last_clicked_item = nullptr;
    last_click_time = 0;

    QSignalBlocker blocker(ui->list_files);

    ui->list_files->clear();

    // ==================================================
    // 回收站
    // ==================================================
    if (recycle_mode)
    {
        file_system->for_each_recycle_item(
            [this](FileNode* node, const std::string& original_path)
            {
                if (node == nullptr)
                    return;

                QListWidgetItem* item =
                    new QListWidgetItem(
                        QString::fromStdString(
                            node->get_name()
                            )
                        );

                item->setData(
                    Qt::UserRole,
                    QVariant::fromValue(node)
                    );

                // 鼠标悬停时显示原来的完整路径
                item->setToolTip(
                    QString::fromStdString(original_path)
                    );

                ui->list_files->addItem(item);
            }
            );

        return;
    }


    // ==================================================
    // 普通目录
    // ==================================================
    FileNode* currentDirectory =
        file_system->get_current_directory();

    if (currentDirectory == nullptr)
        return;

    // 第一趟：置顶
    FileNode* child =
        currentDirectory->get_first_child();

    while (child != nullptr)
    {
        if (child->is_pinned())
            addToListWidget(child);

        child = child->get_next_sibling();
    }

    // 第二趟：普通项目
    child =
        currentDirectory->get_first_child();

    while (child != nullptr)
    {
        if (!child->is_pinned())
            addToListWidget(child);

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

    if (file_system->get_current_directory() == nullptr)
        return;


    // ==================================================
    // 第一部分：回收站
    // ==================================================
    QTreeWidgetItem* recycleItem =
        new QTreeWidgetItem(ui->tree_dir);

    recycleItem->setText(0, "回收站");

    recycleItem->setData(
        0,
        Qt::UserRole + 1,
        1
        );


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
            displayName = "此电脑";
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

    rootItem->setText(0, "此电脑");

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

    FileNode* currentDirectory =
        file_system->get_current_directory();

    auto expandToCurrent =
        [&](auto&& self, QTreeWidgetItem* treeItem) -> bool
    {
        if (treeItem == nullptr)
            return false;

        FileNode* node =
            treeItem->data(
                        0,
                        Qt::UserRole
                        ).value<FileNode*>();

        // 找到了当前目录
        if (node == currentDirectory)
        {
            treeItem->setExpanded(true);

            // 顺便让左侧目录树高亮当前目录
            ui->tree_dir->setCurrentItem(treeItem);

            return true;
        }

        // 在孩子中继续找
        for (int i = 0;
             i < treeItem->childCount();
             ++i)
        {
            if (self(self, treeItem->child(i)))
            {
                // 当前目录在这个节点下面，
                // 所以这个父节点也必须展开
                treeItem->setExpanded(true);
                return true;
            }
        }

        return false;
    };

    expandToCurrent(
        expandToCurrent,
        rootItem
        );

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
               file_system->get_current_directory(),
               name.toStdString()) != nullptr)
    {
        name = QString("新建文件夹 (%1)").arg(index);
        ++index;
    }

    // 直接创建
    FileNode* newNode =
        file_system->create_folder(
            file_system->get_current_directory(),
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
    Operation op;
    op.type = OperationType::CREATE;
    op.node = newNode;
    op.old_parent = file_system->get_current_directory();
    undo_stack.push(op);
    save_data(); // 立即保存

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
               file_system->get_current_directory(),
               name.toStdString()) != nullptr)
    {
        name = QString("新建文件 (%1).txt").arg(index);
        ++index;
    }

    // 直接创建，不再弹输入框
    FileNode* newNode =
        file_system->create_file(
            file_system->get_current_directory(),
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


    // 👇 加上这几行：
    Operation op;
    op.type = OperationType::CREATE;
    op.node = newNode;
    op.old_parent = file_system->get_current_directory();
    undo_stack.push(op);
    save_data();



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
            QMessageBox::warning(
                this,
                "提示",
                "请先选择要删除的文件或文件夹！"
            );
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
                // 👇 加上这几行：
                Operation op;
                op.type = OperationType::DELETE;
                op.node = selectedNode;
                op.old_parent = file_system->get_current_directory();
                undo_stack.push(op);
                save_data();
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
                // 👇 加上这几行：
                Operation op;
                op.type = OperationType::RENAME;
                op.node = selectedNode;
                op.old_name = selectedNode->get_name(); // 这里需要记录原来的名字，你能获取到原名字
                op.new_name = newName.toStdString();
                undo_stack.push(op);
                save_data();

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

    Operation op;
    op.type = OperationType::RENAME;
    op.node = node;
    op.old_name = oldName.toStdString(); // 旧名字
    op.new_name = newName.toStdString(); // 新名字
    undo_stack.push(op);

    save_data(); // 立即保存，确保重启后撤销记录还在
    // 重命名成功后刷新界面
    refresh_file_list();
    refresh_tree();

    // 如果改的是当前目录或者其他可能影响路径显示的节点，
    // 同步刷新当前路径栏
    ui->lineEdit_path->setText(
        QString::fromStdString(
            file_system->get_path(
                file_system->get_current_directory()
                )
            )
        );
}


void MainWindow::on_list_files_itemPressed(QListWidgetItem *item)
{
    if (item == nullptr)
        return;

    // 回收站中的项目禁止通过单击进入重命名
    if (recycle_mode)
    {
        last_clicked_item = nullptr;
        last_click_time = 0;
        return;
    }

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
                file_system->get_current_directory()
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

        // 👇 【复制模式的撤销入栈】加在这里
        Operation op;
        op.type = OperationType::COPY;
        op.node = new_node; // 记录刚刚复制出来的新节点
        undo_stack.push(op);
        save_data(); // 立即持久化
        // 👆 入栈结束

        refresh_tree();
        refresh_file_list();

        // 复制模式下，剪贴板继续保留
        // 可以继续粘贴到其他目录
    }
    else if (clipboard_mode == ClipboardMode::Move)
    {
        // 👇 【1. 必须加这一行！】在移动前，先记录下原父节点
        FileNode* old_parent = clipboard_node->get_parent();

        bool success =
            file_system->move_node(
                clipboard_node,
                file_system->get_current_directory()
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
        // 👇 【移动模式的撤销入栈】加在这里
        Operation op;
        op.type = OperationType::MOVE;
        op.node = clipboard_node;                  // 被移动的节点
        op.old_parent = old_parent;                // 原父节点（刚才提前存好的）
        op.new_parent = file_system->get_current_directory(); // 目标父节点
        undo_stack.push(op);
        save_data(); // 立即持久化
        // 👆 入栈结束
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
    forward_stack.push(
        file_system->get_path(
            file_system->get_current_directory()
            )
    );
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
    back_stack.push(
        file_system->get_path(
            file_system->get_current_directory()
            )
    );
    // 取出前进栈的历史路径
    std::string targetPath = forward_stack.top();
    forward_stack.pop();
    // 跳转
    navigate_to(targetPath);
}


void MainWindow::on_btn_up_clicked()
{
    FileNode* current =
        file_system->get_current_directory();

    if (current == nullptr ||
        current->get_parent() == nullptr)
    {
        ui->statusbar->showMessage(
            "已经是根目录了",
            2000
            );
        return;
    }

    FileNode* parent =
        current->get_parent();

    // 保存当前位置，用于“后退”
    back_stack.push(
        file_system->get_path(current)
        );

    // 新导航发生后，清空前进栈
    while (!forward_stack.empty())
        forward_stack.pop();

    // 统一走 navigate_to
    navigate_to(
        file_system->get_path(parent)
        );
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
    search_recursive(
        file_system->get_current_directory(),
        keyword,
        results
        );

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
void MainWindow::navigate_to(const std::string& path)
{
    // 让 FileSystem 自己修改“当前目录”
    bool success =
        file_system->change_directory_by_path(path);

    if (!success)
    {
        FileNode* current =
            file_system->get_current_directory();

        ui->lineEdit_path->setText(
            QString::fromStdString(
                file_system->get_path(current)
                )
            );

        ui->statusbar->showMessage(
            "历史路径不存在或已被删除！",
            2000
            );

        return;
    }

    FileNode* current =
        file_system->get_current_directory();

    const std::string currentPath =
        file_system->get_path(current);

    // 最近访问
    QString qPath =
        QString::fromStdString(currentPath);

    history_list.removeAll(qPath);
    history_list.prepend(qPath);

    if (history_list.size() > 20)
        history_list.removeLast();

    // 刷新界面
    refresh_file_list();
    refresh_tree();

    ui->lineEdit_path->setText(
        QString::fromStdString(currentPath)
        );
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

void MainWindow::on_list_files_itemDoubleClicked(
    QListWidgetItem *item)
{
    if (item == nullptr)
        return;

    FileNode* targetNode =
        item->data(Qt::UserRole)
            .value<FileNode*>();

    if (targetNode == nullptr)
        return;

    if (!targetNode->is_directory())
    {
        QMessageBox::information(
            this,
            "提示",
            "这是一个文件，双击不能进入。"
            );
        return;
    }

    // 回收站里的节点不属于活动文件树，
    // 不能把它当普通目录进入
    if (recycle_mode)
        return;

    FileNode* current =
        file_system->get_current_directory();

    std::string targetPath =
        file_system->get_path(targetNode);

    if (targetPath.empty())
        return;

    // 保存当前位置
    back_stack.push(
        file_system->get_path(current)
        );

    while (!forward_stack.empty())
        forward_stack.pop();

    // 真正的目录切换统一走这里
    navigate_to(targetPath);

    ui->statusbar->showMessage(
        "已进入文件夹：" +
            QString::fromStdString(
                targetNode->get_name()
                ),
        2000
        );
}


void MainWindow::on_tree_dir_itemClicked(
    QTreeWidgetItem *item,
    int column)
{
    Q_UNUSED(column);

    if (item == nullptr)
        return;


    // ==================================================
    // 回收站
    // ==================================================
    int itemType =
        item->data(
                0,
                Qt::UserRole + 1
                ).toInt();

    if (itemType == 1)
    {
        recycle_mode = true;

        // 显示回收站操作
        ui->btn_restore->show();
        ui->btn_permanent_delete->show();
        ui->btn_clear_recycle->show();

        // 隐藏普通文件操作
        ui->btn_new->hide();
        ui->btn_move->hide();
        ui->btn_copy->hide();
        ui->btn_paste->hide();
        ui->btn_rename->hide();
        ui->btn_delete->hide();
        ui->btn_test_pin->hide();

        refresh_file_list();

        ui->lineEdit_path->setText("回收站");

        return;
    }


    // ==================================================
    // 普通目录
    // ==================================================
    recycle_mode = false;

    // 恢复普通操作按钮
    ui->btn_new->show();
    ui->btn_move->show();
    ui->btn_copy->show();
    ui->btn_paste->show();
    ui->btn_rename->show();
    ui->btn_delete->show();
    ui->btn_test_pin->show();

    // 隐藏回收站操作
    ui->btn_restore->hide();
    ui->btn_permanent_delete->hide();
    ui->btn_clear_recycle->hide();


    FileNode* clickedNode =
        item->data(0,Qt::UserRole).value<FileNode*>();

    if (clickedNode == nullptr ||
        !clickedNode->is_directory())
    {
        return;
    }

    FileNode* current =
        file_system->get_current_directory();

    if (clickedNode == current)
        return;

    std::string targetPath =
        file_system->get_path(clickedNode);

    // 记录当前目录，供“后退”使用
    back_stack.push(
        file_system->get_path(current)
        );

    // 发生新的导航后，清空“前进”
    while (!forward_stack.empty())
        forward_stack.pop();

    // 统一交给 navigate_to 完成真正的目录切换
    navigate_to(targetPath);

}


void MainWindow::on_btn_refresh_clicked()
{
    refresh_tree();
    refresh_file_list();
}





void MainWindow::save_node_recursive(FileNode* node, std::ofstream& out) {
    if (node == nullptr) return;

    std::string path = file_system->get_path(node);

    // 写入 8 个字段
    out << (node->is_directory() ? "D" : "F") << "|"
        << path << "|"
        << node->get_created_time() << "|"
        << node->get_modified_time() << "|"
        << (node->is_pinned() ? "1" : "0") << "|"
        << node->get_pin_order() << "|"
        << node->get_size() << "|"
        << (node->is_directory() ? "" : node->get_content()) << "\n"; // 文件夹内容为空

    // 递归遍历子节点
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
    file_system->for_each_recycle_item([&](FileNode* node, const std::string& original_path){
        out << "R|"
            << (node->is_directory() ? "D" : "F") << "|"
            << original_path << "|"
            << node->get_name() << "|"
            << node->get_created_time() << "|"
            << node->get_modified_time() << "|"
            << (node->is_pinned() ? "1" : "0") << "|"
            << node->get_pin_order() << "|"
            << node->get_size() << "|"
            << (node->is_directory() ? "" : node->get_content()) << "\n";
    });
    out.close();
    ui->statusbar->showMessage("数据保存成功！", 2000);
}





void MainWindow::load_data() {
    std::ifstream in("data.txt");
    if (!in.is_open()) return; // 第一次运行没有文件，直接返回

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string type;
        std::getline(ss, type, '|');

        // =======================================================
        // 👇 任务二：回收站持久化加载
        // 格式：R|D或F|原路径|名称|创建时间|修改时间|置顶|pin_order|大小|内容
        // =======================================================
        if (type == "R") {
            std::string is_dir_str, original_path, name, c_time, m_time;
            std::string pinned_str, pin_order_str, size_str, content_str;

            std::getline(ss, is_dir_str, '|');
            std::getline(ss, original_path, '|');
            std::getline(ss, name, '|');
            std::getline(ss, c_time, '|');
            std::getline(ss, m_time, '|');
            std::getline(ss, pinned_str, '|');
            std::getline(ss, pin_order_str, '|');
            std::getline(ss, size_str, '|');
            std::getline(ss, content_str, '|'); // 注意：如果内容包含 '|'，这里会有解析风险，目前假定模拟文件内容不包含 '|'

            bool is_directory = (is_dir_str == "D");

            // 1. 调用底层接口，创建节点并放入回收站链表（传入 nullptr 代表它是回收站顶层节点）
            FileNode* recycledNode = file_system->create_recycle_node_for_load(
                nullptr, name, is_directory, original_path);

            if (recycledNode != nullptr) {
                // 2. 按照文档顺序：先 set_content，再 set_metadata
                if (!is_directory) {
                    file_system->set_content(recycledNode, content_str);
                }

                long long size = 0;
                try { size = std::stoll(size_str); } catch(...) { size = 0; }
                file_system->set_metadata(recycledNode, size, c_time, m_time);

                // 3. 最后恢复置顶状态（你刚才改了底层拦截，这里现在可以成功生效了！）
                bool is_pinned = (pinned_str == "1");
                long long pin_order = -1;
                try { pin_order = std::stoll(pin_order_str); } catch(...) { pin_order = -1; }
                file_system->restore_pin_state(recycledNode, is_pinned, pin_order);
            }
            continue; // 🚨 极其重要：跳过下面普通文件树的挂载逻辑，否则回收站节点会被挂到活动树上！
        }
        // =======================================================


        // =======================================================
        // 👇 任务一：普通文件树持久化加载
        // 格式：D或F|路径|创建时间|修改时间|置顶|pin_order|大小|内容
        // =======================================================
        std::string path, c_time, m_time, pinned_str, pin_order_str, size_str, content_str;
        std::getline(ss, path, '|');
        std::getline(ss, c_time, '|');
        std::getline(ss, m_time, '|');
        std::getline(ss, pinned_str, '|');
        std::getline(ss, pin_order_str, '|');
        std::getline(ss, size_str, '|');
        std::getline(ss, content_str, '|');

        if (path == "/" || path.empty()) continue;

        size_t lastSlash = path.find_last_of('/');
        if (lastSlash == std::string::npos) continue;
        std::string parentPath = path.substr(0, lastSlash);
        if (parentPath.empty()) parentPath = "/";

        std::string name = path.substr(lastSlash + 1);

        // 查找父节点
        FileNode* parentNode = file_system->find_by_path(parentPath);
        if (parentNode == nullptr) continue; // 容错处理

        FileNode* newNode = nullptr;
        if (type == "D") {
            newNode = file_system->create_folder(parentNode, name);
        } else {
            newNode = file_system->create_file(parentNode, name, "");
        }

        if (newNode != nullptr) {
            // 1. 先恢复 Content（仅文件）
            if (type == "F") {
                file_system->set_content(newNode, content_str);
            }

            // 2. 再恢复原始 size 和时间
            long long size = 0;
            try { size = std::stoll(size_str); } catch(...) { size = 0; }
            file_system->set_metadata(newNode, size, c_time, m_time);

            // 3. 最后恢复置顶状态
            bool is_pinned = (pinned_str == "1");
            long long pin_order = -1;
            try { pin_order = std::stoll(pin_order_str); } catch(...) { pin_order = -1; }
            file_system->restore_pin_state(newNode, is_pinned, pin_order);
        }
    }
    in.close();

    // 加载完成后，刷新界面
    refresh_tree();
    refresh_file_list();
    ui->lineEdit_path->setText(QString::fromStdString(file_system->get_path(file_system->get_current_directory())));
}



void MainWindow::closeEvent(QCloseEvent *event) {
    save_data(); // 窗口关闭时，偷偷执行一次保存
    event->accept(); // 接受关闭事件
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


void MainWindow::on_btn_restore_clicked()
{
    if (!recycle_mode)
        return;

    QListWidgetItem* item =
        ui->list_files->currentItem();

    if (item == nullptr)
    {
        QMessageBox::warning(
            this,
            "提示",
            "请先选择要恢复的项目！"
            );
        return;
    }

    FileNode* node =
        item->data(
                Qt::UserRole
                ).value<FileNode*>();

    if (node == nullptr)
        return;


    std::string originalPath =
        file_system->get_recycle_original_path(node);

    // 找到原来的父目录
    size_t lastSlash =
        originalPath.find_last_of('/');

    std::string parentPath;

    if (lastSlash == 0)
    {
        parentPath = "/";
    }
    else
    {
        parentPath =
            originalPath.substr(
                0,
                lastSlash
                );
    }

    FileNode* parent =
        file_system->find_by_path(parentPath);

    if (parent == nullptr)
    {
        QMessageBox::warning(
            this,
            "恢复失败",
            "原来的父目录已经不存在，无法恢复！"
            );
        return;
    }

    bool success =
        file_system->restore_node(
            node,
            parent
            );

    if (!success)
    {
        QMessageBox::warning(
            this,
            "恢复失败",
            "恢复失败！可能存在同名文件或文件夹。"
            );
        return;
    }
    Operation op;
    op.type = OperationType::RESTORE;
    op.node = node;
    undo_stack.push(op);
    save_data();
    refresh_file_list();
    refresh_tree();
}


void MainWindow::on_btn_permanent_delete_clicked()
{
    if (!recycle_mode)
        return;

    QListWidgetItem* item =
        ui->list_files->currentItem();

    if (item == nullptr)
    {
        QMessageBox::warning(
            this,
            "提示",
            "请先选择要永久删除的项目！"
            );
        return;
    }

    FileNode* node =
        item->data(
                Qt::UserRole
                ).value<FileNode*>();

    if (node == nullptr)
        return;


    QMessageBox::StandardButton reply =
        QMessageBox::question(
            this,
            "确认永久删除",
            "确定要永久删除这个项目吗？\n此操作无法撤销。",
            QMessageBox::Yes | QMessageBox::No
            );

    if (reply != QMessageBox::Yes)
        return;


    if (!file_system->permanent_delete(node))
    {
        QMessageBox::warning(
            this,
            "失败",
            "永久删除失败！"
            );
        return;
    }
    // 👇 加上这些清理代码
    while (!undo_stack.empty()) undo_stack.pop();
    clipboard_node = nullptr;
    clipboard_mode = ClipboardMode::None;
    refresh_file_list();
    refresh_tree();
}


void MainWindow::on_btn_clear_recycle_clicked()
{
    if (!recycle_mode)
        return;

    if (file_system->get_recycle_count() == 0)
    {
        QMessageBox::information(
            this,
            "提示",
            "回收站已经是空的。"
            );
        return;
    }


    QMessageBox::StandardButton reply =
        QMessageBox::question(
            this,
            "清空回收站",
            "确定要清空回收站吗？\n所有项目将被永久删除。",
            QMessageBox::Yes | QMessageBox::No
            );

    if (reply != QMessageBox::Yes)
        return;


    file_system->clear_recycle_bin();
    while (!undo_stack.empty()) undo_stack.pop();
    clipboard_node = nullptr;
    clipboard_mode = ClipboardMode::None;
    refresh_file_list();
    refresh_tree();
}
void MainWindow::on_btn_undo_clicked()
{

        if (undo_stack.empty()) {
            QMessageBox::warning(this, "撤销", "没有可以撤销的操作！");
            return;
        }

        // 1. 取出栈顶操作
        Operation op = undo_stack.top();

        // 2. 调用底层执行逆操作
        bool success = file_system->undo(op);

        if (success) {
            // 3. 弹栈
            undo_stack.pop();

            // 4. 安全起见，强制返回根目录（因为撤销可能删除了当前目录）
            file_system->change_directory_by_path("/");
            refresh_tree();
            refresh_file_list();
            ui->lineEdit_path->setText("/");

            // 5. 持久化保存
            save_data();
            ui->statusbar->showMessage("撤销成功！", 2000);
        } else {
            // 如果底层撤销失败，也要弹栈，防止死循环
            undo_stack.pop();
            QMessageBox::warning(this, "撤销失败", "该操作无法撤销。");
        }

}


void MainWindow::on_btn_reset_clicked()
{

        // =======================================================
        // 1. 第一次确认：是否确认重置
        // =======================================================
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "确认重置", "确定要重置文件系统吗？",
                                      QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return; // 用户取消
        }

        // =======================================================
        // 2. 第二次确认：明确警告重置的严重后果，并再次确认
        // =======================================================
        reply = QMessageBox::warning(this, "重置警告",
                                     "重置将清空所有文件、文件夹和回收站，且无法撤销！\n确定要继续吗？",
                                     QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return; // 用户取消
        }

        // =======================================================
        // 3. 调用底层重置
        // 底层会自动清空正常树、回收站，并将当前目录重置为根目录
        // =======================================================
        file_system->reset();

        // =======================================================
        // 4. 清理所有悬空指针（极其关键，防止重置后操作崩溃）
        // 因为所有节点都被真正释放了，必须清理所有存着旧指针的地方！
        // =======================================================

        // 清空撤销栈（std::stack 没有 clear 方法，只能用 while 循环 pop）
        while (!undo_stack.empty()) undo_stack.pop();

        // 清空导航栈
        while (!back_stack.empty()) back_stack.pop();
        while (!forward_stack.empty()) forward_stack.pop();

        // 清空访问历史
        history_list.clear();

        // 清空剪贴板（防止粘贴时访问已释放的野指针）
        clipboard_node = nullptr;
        clipboard_mode = ClipboardMode::None;

        // 强制退出回收站模式
        recycle_mode = false;

        // =======================================================
        // 5. 刷新界面并持久化
        // =======================================================
        refresh_tree();
        refresh_file_list();

        // 重置后底层已自动将目录指回 root，直接读取底层路径
        ui->lineEdit_path->setText(QString::fromStdString(
            file_system->get_path(file_system->get_current_directory())));

        save_data(); // 将重置后的空状态立刻写入 data.txt，覆盖旧存档

        QMessageBox::information(this, "重置成功", "系统已成功重置！");

}

