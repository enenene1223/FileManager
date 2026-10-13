#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QListWidgetItem>

#include <QIcon>
#include <QMenu>
#include <QAction>
#include <QLineEdit>

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
#include <functional>
#include <vector>

#include <QShortcut> // 快捷键
#include <QKeySequence>

#include <QWidgetAction>// 右键菜单
#include <QHBoxLayout>
#include <QCursor>
#include <QStyle>

#include <QCryptographicHash>
#include <QFile>
#include <QTextStream>
#include <QInputDialog>
#include <QMessageBox>

#include <QDialog> // 文件属性
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <QPainter> // ui
#include <QPixmap>
#include <QSize>
#include <QColor>

#include <QDebug>// debug
#include <QDir>
#include <QByteArray>

// 路径显示
static QString toDisplayPath(const QString& path)
{
    if (path == "/")
        return "/此电脑";

    return "/此电脑" + path;
}


static QIcon makeFileNodeIcon(const FileNode* node)
{
    if (node == nullptr)
        return QIcon();

    // Qt 资源路径：qresource 前缀 /icons + 文件路径 icons/...
    const QString basePath = node->is_directory()
                                 ? ":/icons/icons/folder.svg"
                                 : ":/icons/icons/file-text.svg";

    const int iconSize = 32;

    QPixmap canvas(iconSize, iconSize);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPixmap base = QIcon(basePath).pixmap(28, 28);
    painter.drawPixmap(2, 2, base);

    // 右下角叠加锁定状态
    if (node->is_locked())
    {
        QPixmap lock =
            QIcon(":/icons/icons/lock.svg").pixmap(13, 13);

        painter.fillRect(
            QRect(18, 18, 14, 14),
            QColor(255, 255, 255, 230)
            );

        painter.drawPixmap(18, 18, lock);
    }

    // 左上角叠加置顶状态
    if (node->is_pinned())
    {
        QPixmap pin =
            QIcon(":/icons/icons/pin.svg").pixmap(12, 12);

        painter.drawPixmap(0, 0, pin);
    }

    painter.end();

    return QIcon(canvas);
}


MainWindow::MainWindow(QString username, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , current_user(username)
{
    // 生成界面
    ui->setupUi(this);

    ui->list_files->setIconSize(QSize(24, 24));
    ui->tree_dir->setIconSize(QSize(20, 20));

    ui->list_files->setMouseTracking(true);


    // ===============================================
    // 目录区
    // ===============================================
    // 左侧目录树作为辅助导航区，右侧文件列表作为主要操作区
    ui->tree_dir->setMinimumWidth(100);
    ui->tree_dir->setMaximumWidth(QWIDGETSIZE_MAX);

    ui->splitter_main->setStretchFactor(0, 0);
    ui->splitter_main->setStretchFactor(1, 1);

    // 默认约 20% : 80%
    ui->splitter_main->setSizes({180, 800});

    // 左侧不能被拖到完全消失
    ui->splitter_main->setCollapsible(0, false);
    ui->splitter_main->setCollapsible(1, false);

    // 左侧目录树使用两列：名称 + 置顶图标
    ui->tree_dir->setColumnCount(2);

    // ===============================================
    // 搜索栏
    // ===============================================
    // 搜索框提示文字
    ui->lineEdit_search->setPlaceholderText("搜索当前目录...");

    // 在搜索框右侧添加搜索图标
    QAction *searchAction = ui->lineEdit_search->addAction(
        QIcon::fromTheme("edit-find"),
        QLineEdit::TrailingPosition
        );

    // 点击搜索图标，调用现有搜索功能
    connect(searchAction, &QAction::triggered,
            this, &MainWindow::on_btn_search_clicked);

    // 按 Enter 也可以搜索
    connect(ui->lineEdit_search, &QLineEdit::returnPressed,
            this, &MainWindow::on_btn_search_clicked);

    // 隐藏原来的独立搜索按钮
    ui->btn_search->hide();
    QHeaderView *header = ui->tree_dir->header();

    header->setStretchLastSection(false);
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::Fixed);
    header->resizeSection(1, 32);

    ui->tree_dir->setHeaderHidden(true);


    // ===============================================
    // 双击重命名
    // ===============================================
    // 关闭 QListWidget 默认的双击编辑，
    // 只允许我们在“新建”完成后主动进入重命名状态
    ui->list_files->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // 两次间隔较长的单击才可能是重命名；延迟执行，以便双击事件取消它。
    rename_click_timer = new QTimer(this);
    rename_click_timer->setSingleShot(true);
    connect(rename_click_timer, &QTimer::timeout, this, [this]() {
        QListWidgetItem *item = last_clicked_item;
        if (!recycle_mode && item != nullptr &&
            ui->list_files->currentItem() == item &&
            (item->flags() & Qt::ItemIsEditable)) {
            ui->list_files->editItem(item);
        }
        last_clicked_item = nullptr;
        last_click_time = 0;
    });

    // ===============================================
    // 路径栏
    // ===============================================
    // 路径栏按 Enter 后执行路径跳转
    connect(ui->lineEdit_path, &QLineEdit::returnPressed,
            this, [this]() {

                // 回收站不是普通文件夹路径
                if (recycle_mode) {
                    QMessageBox::information(
                        this, "提示",
                        "请先从左侧目录树进入普通文件夹。"
                        );
                    ui->lineEdit_path->setText("回收站");
                    return;
                }

                // 当前实际路径
                std::string currentPath =
                    file_system->get_path(
                        file_system->get_current_directory()
                        );

                // 获取用户输入并去除首尾空白
                QString input =
                    ui->lineEdit_path->text().trimmed();

                // 路径为空
                if (input.isEmpty()) {
                    QMessageBox::warning(
                        this, "路径错误",
                        "路径不能为空！"
                        );

                    ui->lineEdit_path->setText(
                        toDisplayPath(QString::fromStdString(currentPath))
                        );
                    return;
                }

                // 统一使用 / 作为分隔符
                input.replace('\\', '/');

                // 将显示路径转换为底层真实路径
                if (input == "/此电脑") {
                    input = "/";
                }
                else if (input.startsWith("/此电脑/")) {
                    input.remove(0, QString("/此电脑").length());
                }

                std::string targetPath = input.toStdString();

                // 只接受绝对路径
                if (targetPath.front() != '/') {
                    QMessageBox::warning(
                        this, "路径错误",
                        "请输入以 / 开头的完整路径！"
                        );

                    ui->lineEdit_path->setText(
                        toDisplayPath(QString::fromStdString(currentPath))
                        );
                    return;
                }

                // 检查路径是否存在
                FileNode* targetNode =
                    file_system->find_by_path(targetPath);

                if (targetNode == nullptr) {
                    QMessageBox::warning(
                        this, "路径不存在",
                        "找不到指定路径：\n" + input
                        );

                    ui->lineEdit_path->setText(
                        toDisplayPath(QString::fromStdString(currentPath))
                        );
                    return;
                }

                // 检查是否为文件夹
                if (!targetNode->is_directory()) {
                    QMessageBox::warning(
                        this, "无法跳转",
                        "目标路径是文件，不是文件夹！"
                        );

                    ui->lineEdit_path->setText(
                        toDisplayPath(QString::fromStdString(currentPath))
                        );
                    return;
                }

                // 获取规范化后的目标路径
                // 例如 /A/../B 会变成 /B
                std::string normalizedPath =
                    file_system->get_path(targetNode);

                // 若已经在目标目录，不产生新的历史记录
                if (normalizedPath == currentPath) {
                    ui->lineEdit_path->setText(
                        toDisplayPath(QString::fromStdString(currentPath))
                        );
                    return;
                }

                // 保存当前位置到后退栈
                back_stack.push(currentPath);

                // 新导航清空前进栈
                while (!forward_stack.empty()) {
                    forward_stack.pop();
                }

                // 复用原有统一导航函数
                navigate_to(normalizedPath);

                ui->statusbar->showMessage(
                    "已跳转至：" +
                        QString::fromStdString(normalizedPath),
                    3000
                    );
            });

    // 路径栏提示文字
    ui->lineEdit_path->setPlaceholderText(
        "输入文件夹绝对路径，按 Enter 跳转"
        );

    // 鼠标悬停提示
    ui->lineEdit_path->setToolTip(
        "支持输入虚拟文件系统路径，例如：/学习/计算机网络\n"
        "按 Enter 跳转，路径不存在时不会改变当前位置。"
        );

    // 初始化底层系统
    file_system = new FileSystem();


    //自动加载上一次保存的数据
    load_data();
    // 加载完成后，刷新界面
    refresh_tree();
    refresh_file_list();

    ui->lineEdit_path->setText(
        toDisplayPath(QString::fromStdString(
            file_system->get_path(file_system->get_current_directory())
            ))
        );


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


    // ===============================================
    // 快捷键系统
    // ===============================================
    // F2：重命名
    auto* renameShortcut =
        new QShortcut(QKeySequence(Qt::Key_F2), ui->list_files);

    renameShortcut->setContext(Qt::WidgetWithChildrenShortcut);

    connect(renameShortcut, &QShortcut::activated,
            this, [this]() {
                if (!recycle_mode &&
                    ui->list_files->hasFocus())
                    on_btn_rename_clicked();
            });

    // Ctrl+C：复制
    auto* copyShortcut =
        new QShortcut(QKeySequence::Copy, ui->list_files);

    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);

    connect(copyShortcut, &QShortcut::activated,
            this, [this]() {
                if (!recycle_mode &&
                    ui->list_files->hasFocus())
                    on_btn_copy_clicked();
            });

    // Ctrl+X：剪切
    auto* cutShortcut =
        new QShortcut(QKeySequence::Cut, ui->list_files);

    cutShortcut->setContext(Qt::WidgetWithChildrenShortcut);

    connect(cutShortcut, &QShortcut::activated,
            this, [this]() {
                if (!recycle_mode &&
                    ui->list_files->hasFocus())
                    on_btn_cut_clicked();
            });

    // Ctrl+V：粘贴
    auto* pasteShortcut =
        new QShortcut(QKeySequence::Paste, ui->list_files);

    pasteShortcut->setContext(Qt::WidgetWithChildrenShortcut);

    connect(pasteShortcut, &QShortcut::activated,
            this, [this]() {
                if (!recycle_mode &&
                    ui->list_files->hasFocus())
                    on_btn_paste_clicked();
            });

    // Ctrl+D：删除
    auto* deleteShortcut =
        new QShortcut(
            QKeySequence(Qt::CTRL | Qt::Key_D), ui->list_files);

    deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);

    connect(deleteShortcut, &QShortcut::activated,
            this, [this]() {
                if (!recycle_mode &&
                    ui->list_files->hasFocus())
                {
                    // Ctrl+D 直接删除，不弹确认窗口
                    delete_selected_item(false);
                }
            });

    // Ctrl+Z：撤销
    auto* undoShortcut =
        new QShortcut(QKeySequence::Undo, ui->list_files);

    undoShortcut->setContext(Qt::WidgetWithChildrenShortcut);

    connect(undoShortcut, &QShortcut::activated,
            this, [this]() {
                if (!recycle_mode &&
                    ui->list_files->hasFocus())
                    on_btn_undo_clicked();
            });



    // ===============================================
    // 右键菜单
    // ===============================================
    ui->list_files->setContextMenuPolicy(
        Qt::CustomContextMenu);

    connect(ui->list_files,
            &QListWidget::customContextMenuRequested,
            this, [this](const QPoint& pos)
            {
                // 先取消可能正在等待的重命名
                rename_click_timer->stop();
                last_clicked_item = nullptr;
                last_click_time = 0;

                QListWidgetItem* item =
                    ui->list_files->itemAt(pos);

                // 右键选中鼠标所在的项目
                if (item != nullptr)
                {
                    ui->list_files->setCurrentItem(item);
                }
                else
                {
                    // 右键空白处：清除原来的选中状态
                    ui->list_files->clearSelection();
                    ui->list_files->setCurrentItem(nullptr);
                }

                QMenu menu(this);

                // ===========================================
                // 场景一：普通目录空白处
                // ===========================================
                if (!recycle_mode && item == nullptr)
                {

                    // 显示子菜单
                    QMenu* displayMenu = menu.addMenu("显示");

                    displayMenu->setIcon(
                        QIcon(":/icons/icons/eye.svg")
                        );

                    // 隐私文件选项
                    QAction* privacyAction =
                        displayMenu->addAction(show_privacy_mode ? "隐藏隐私文件" : "显示隐私文件");

                    privacyAction->setCheckable(true);
                    privacyAction->setChecked(show_privacy_mode);
                    privacyAction->setEnabled(true); // 👈 解除封印

                    // 👈 绑定槽函数
                    connect(privacyAction, &QAction::triggered, this, &MainWindow::on_action_show_privacy_triggered);
                    menu.addSeparator();


                    QMenu* sortMenu = menu.addMenu("排序方式");

                    // 排序功能下一步接入
                    QAction* sortName =
                        sortMenu->addAction("按名称排序");
                    sortName->setEnabled(false);

                    menu.addSeparator();

                    menu.addAction("刷新", this,
                                   &MainWindow::on_btn_refresh_clicked);

                    QAction* pasteAction =
                        menu.addAction("粘贴", this,
                                       &MainWindow::on_btn_paste_clicked);

                    pasteAction->setEnabled(
                        clipboard_node != nullptr &&
                        clipboard_mode != ClipboardMode::None);

                    QAction* undoAction =
                        menu.addAction("撤销", this,
                                       &MainWindow::on_btn_undo_clicked);

                    undoAction->setEnabled(!undo_stack.empty());

                    menu.addSeparator();

                    QMenu* newMenu = menu.addMenu("新建");

                    newMenu->addAction("文件",
                                       this,
                                       &MainWindow::on_btn_new_file_clicked);

                    newMenu->addAction("文件夹",
                                       this,
                                       &MainWindow::on_btn_new_folder_clicked);
                }

                // ===========================================
                // 场景二：普通目录选中项目
                // ===========================================
                else if (!recycle_mode && item != nullptr)
                {
                    FileNode* node =
                        item->data(Qt::UserRole).value<FileNode*>();

                    if (node == nullptr)
                        return;

                    // 横排快捷操作区域
                    QWidgetAction* toolbarAction =
                        new QWidgetAction(&menu);

                    QWidget* toolbar = new QWidget(&menu);
                    QHBoxLayout* layout =
                        new QHBoxLayout(toolbar);

                    layout->setContentsMargins(4, 4, 4, 4);
                    layout->setSpacing(6);

                    // 创建横排操作按钮
                    auto addTool = [&](const QString& text,
                                       QStyle::StandardPixmap iconType,
                                       auto callback)
                    {
                        QToolButton* button =
                            new QToolButton(toolbar);

                        button->setText(text);
                        button->setIcon(
                            style()->standardIcon(iconType));

                        button->setToolButtonStyle(
                            Qt::ToolButtonTextUnderIcon);

                        button->setAutoRaise(true);
                        button->setMinimumWidth(56);

                        layout->addWidget(button);

                        connect(button, &QToolButton::clicked,
                                this, [&, callback]()
                                {
                                    menu.close();
                                    callback();
                                });
                    };

                    addTool("剪切",
                            QStyle::SP_DialogSaveButton,
                            [this]() {
                                on_btn_cut_clicked();
                            });

                    addTool("复制",
                            QStyle::SP_FileIcon,
                            [this]() {
                                on_btn_copy_clicked();
                            });

                    addTool("重命名",
                            QStyle::SP_FileDialogDetailedView,
                            [this]() {
                                on_btn_rename_clicked();
                            });

                    addTool("删除",
                            QStyle::SP_TrashIcon,
                            [this]() {
                                on_btn_delete_clicked();
                            });

                    toolbarAction->setDefaultWidget(toolbar);
                    menu.addAction(toolbarAction);

                    menu.addSeparator();

                    QAction* openAction =
                        menu.addAction("打开");

                    connect(openAction, &QAction::triggered,
                            this, [this, node]()
                            {
                                if (!node->is_directory())
                                {
                                    QMessageBox::information(
                                        this, "提示",
                                        "当前尚不支持打开文件内容。");
                                    return;
                                }

                                if (file_system->is_locked(node)) {
                                    bool ok;
                                    QString pass = QInputDialog::getText(this, "文件已锁定", "该文件夹已锁定，请输入隐私密码：", QLineEdit::Password, "", &ok);
                                    if (!ok) return;

                                    if (verify_privacy_password(pass)) {
                                        has_privacy_access = true; // 密码正确，授予权限
                                        refresh_file_list(); // 刷新一下让界面显示该文件
                                    } else {
                                        QMessageBox::warning(this, "错误", "隐私密码错误！");
                                        return; // 密码错误，不执行打开操作
                                    }
                                }
                                std::string currentPath =
                                    file_system->get_path(
                                        file_system->get_current_directory());

                                std::string targetPath =
                                    file_system->get_path(node);

                                if (currentPath == targetPath)
                                    return;

                                back_stack.push(currentPath);

                                while (!forward_stack.empty())
                                    forward_stack.pop();

                                navigate_to(targetPath);
                            });

                    // 仅文件夹显示固定操作
                    if (node->is_directory())
                    {
                        QAction* pinAction = menu.addAction(
                            node->is_pinned()
                                ? "取消固定"
                                : "固定");

                        connect(pinAction, &QAction::triggered,
                                this, [this, node]()
                                {
                                    toggle_pin(node);
                                    save_data();
                                });
                    }

                    menu.addSeparator();

                    // 上锁 / 解锁
                    QAction* lockAction = menu.addAction(
                        file_system->is_locked(node) ? "解锁" : "上锁"
                        );
                    lockAction->setEnabled(true);

                    // 👈 绑定你自己的槽函数
                    connect(lockAction, &QAction::triggered, this, &MainWindow::on_action_lock_clicked);

                    // 交给队友实现业务函数
                   /* connect(lockAction, &QAction::triggered,
                            this, [this, node]()
                            {
                                on_action_lock_clicked(node);
                            });*/

                    // 查看选中节点的属性
                    QAction* propertyAction = menu.addAction("属性");

                    connect(propertyAction, &QAction::triggered,
                            this, [this, node]()
                            {
                                show_node_properties(node);
                            });

                }

                // ===========================================
                // 场景三：回收站空白处
                // ===========================================
                else if (recycle_mode && item == nullptr)
                {
                    QMenu* sortMenu = menu.addMenu("排序方式");

                    QAction* sortName =
                        sortMenu->addAction("按名称排序");
                    sortName->setEnabled(false);

                    menu.addSeparator();

                    menu.addAction("清空回收站",
                                   this,
                                   &MainWindow::on_btn_clear_recycle_clicked);
                }

                // ===========================================
                // 场景四：回收站选中项目
                // ===========================================
                else if (recycle_mode && item != nullptr)
                {
                    menu.addAction("永久删除",
                                   this,
                                   &MainWindow::on_btn_permanent_delete_clicked);

                    menu.addAction("还原",
                                   this,
                                   &MainWindow::on_btn_restore_clicked);
                }

                // 在鼠标点击位置弹出菜单
                menu.exec(
                    ui->list_files->viewport()->mapToGlobal(pos));
            });


}



void MainWindow::show_node_properties(FileNode* node)
{
    if (node == nullptr)
        return;

    const bool isDirectory = node->is_directory();

    QString name =
        QString::fromStdString(node->get_name());

    // 计算文件或文件夹大小
    long long totalBytes = 0;
    int fileCount = 0;
    int folderCount = 0;

    if (isDirectory)
    {
        // 遍历整个子树，统计所有子文件和子文件夹
        file_system->traverse(node, [&](FileNode* current)
                              {
                                  if (current == node)
                                      return;

                                  if (current->is_directory())
                                  {
                                      ++folderCount;
                                  }
                                  else
                                  {
                                      ++fileCount;

                                      totalBytes +=
                                          std::max(0LL, current->get_size());
                                  }
                              });
    }
    else
    {
        totalBytes = std::max(0LL, node->get_size());
    }

    // 格式化文件大小
    QString sizeText =
        QString("%1 字节").arg(totalBytes);

    if (totalBytes >= 1024)
    {
        double value =
            static_cast<double>(totalBytes) / 1024.0;

        QString unit = "KiB";

        if (value >= 1024.0)
        {
            value /= 1024.0;
            unit = "MiB";
        }

        if (value >= 1024.0)
        {
            value /= 1024.0;
            unit = "GiB";
        }

        sizeText += QString(" (%1 %2)")
                        .arg(value, 0, 'f', 2)
                        .arg(unit);
    }

    // 获取路径
    QString fullPath = toDisplayPath(
        QString::fromStdString(
            file_system->get_path(node)
            )
        );

    QString location = toDisplayPath(
        QString::fromStdString(
            file_system->get_path(node->get_parent())
            )
        );

    // 创建属性对话框
    QDialog dialog(this);

    dialog.setWindowTitle(
        QString(isDirectory
                    ? "文件夹属性 - "
                    : "文件属性 - ") + name
        );

    dialog.setMinimumWidth(460);

    auto* mainLayout = new QVBoxLayout(&dialog);

    auto* form = new QFormLayout();

    form->setLabelAlignment(Qt::AlignRight);

    form->setFieldGrowthPolicy(
        QFormLayout::ExpandingFieldsGrow
        );

    mainLayout->addLayout(form);

    // 添加一行只读属性
    auto addRow = [&](const QString& title,
                      const QString& value)
    {
        auto* label = new QLabel(value, &dialog);

        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);

        label->setTextInteractionFlags(
            Qt::TextSelectableByMouse
            );

        form->addRow(title, label);
    };

    // 基本属性
    addRow("名称：", name);

    addRow("类型：",
           isDirectory ? "文件夹" : "TXT 文本文件");

    addRow("所在位置：", location);

    addRow("完整路径：", fullPath);

    addRow("大小：", sizeText);

    // 文件夹特有属性
    if (isDirectory)
    {
        addRow(
            "包含：",
            QString("%1 个文件，%2 个子文件夹")
                .arg(fileCount)
                .arg(folderCount)
            );
    }

    // 时间
    addRow(
        "创建时间：",
        QString::fromStdString(
            node->get_created_time()
            )
        );

    addRow(
        "修改时间：",
        QString::fromStdString(
            node->get_modified_time()
            )
        );

    // 状态
    addRow(
        "已置顶：",
        node->is_pinned() ? "是" : "否"
        );

    addRow(
        "已锁定：",
        node->is_locked() ? "是" : "否"
        );

    // 确定按钮
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok,
        &dialog
        );

    connect(
        buttons,
        &QDialogButtonBox::accepted,
        &dialog,
        &QDialog::accept
        );

    mainLayout->addWidget(buttons);

    // 显示模态窗口
    dialog.exec();
}



MainWindow::~MainWindow()
{
    delete ui;
    delete file_system; // 记得释放内存
}


void MainWindow::refresh_file_list()
{
    if (rename_click_timer) rename_click_timer->stop();
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

                item->setIcon(makeFileNodeIcon(node));

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
        // 👈 【加这里】
        if (file_system->is_locked(child) && !has_privacy_access) {
            child = child->get_next_sibling();
            continue;
        }
        if (child->is_pinned())
            addToListWidget(child);

        child = child->get_next_sibling();
    }

    // 第二趟：普通项目
    child =
        currentDirectory->get_first_child();

    while (child != nullptr)
    {
        // 👈 【加这里】
        if (file_system->is_locked(child) && !has_privacy_access) {
            child = child->get_next_sibling();
            continue;
        }

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
        // 左侧树只显示文件夹
        if (child->is_directory()) {
            if (file_system->is_locked(child) && !has_privacy_access) {
                child = child->get_next_sibling();
                continue;
            }

            QTreeWidgetItem* item = new QTreeWidgetItem(parentItem);
            item->setText(0, QString::fromStdString(child->get_name()));
            item->setIcon(0, makeFileNodeIcon(child));

            // 把底层的 FileNode 指针存进 QTreeWidgetItem 的 UserRole 里
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

    recycleItem->setIcon(
        0,
        QIcon(":/icons/icons/recycle-bin.svg")
        );

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
                if (file_system->is_locked(node) && !has_privacy_access) {
                    return;
                }
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

    for (FileNode* node : std::as_const(pinnedNodes))
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
        // 文件或文件夹图标
        item->setIcon(0, makeFileNodeIcon(node));

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
    for (const QString& path : std::as_const(history_list))
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
        if (file_system->is_locked(node) && !has_privacy_access)
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

        item->setIcon(0, makeFileNodeIcon(node));

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

    rootItem->setIcon(
        0,
        QIcon(":/icons/icons/computer.svg")
        );

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


void MainWindow::delete_selected_item(bool needConfirm)
{
    if (recycle_mode)
        return;

    QListWidgetItem* item = ui->list_files->currentItem();

    if (item == nullptr) {
        QMessageBox::warning(
            this, "提示",
            "请先选择要删除的文件或文件夹！");
        return;
    }

    FileNode* selectedNode =
        item->data(Qt::UserRole).value<FileNode*>();

    if (selectedNode == nullptr)
        return;

    // 只有要求确认时才显示弹窗
    if (needConfirm) {
        auto reply = QMessageBox::question(
            this,
            "确认删除",
            "确定要删除选中的项吗？（将移入回收站）",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
            );

        if (reply != QMessageBox::Yes)
            return;
    }

    FileNode* oldParent = selectedNode->get_parent();

    if (!file_system->delete_node(selectedNode)) {
        QMessageBox::warning(
            this, "删除失败",
            "无法删除选中的文件或文件夹！");
        return;
    }

    Operation op;
    op.type = OperationType::DELETE;
    op.node = selectedNode;
    op.old_parent = oldParent;

    undo_stack.push(op);

    save_data();
    refresh_file_list();
    refresh_tree();
}

void MainWindow::on_btn_delete_clicked()
{
    // 工具栏按钮仍保留确认
    delete_selected_item(true);
}


void MainWindow::on_btn_rename_clicked()
{

    if (recycle_mode)
        return;

    QListWidgetItem* item = ui->list_files->currentItem();

    if (item == nullptr) {
        QMessageBox::warning(
            this, "提示",
            "请先选择要重命名的文件或文件夹！");
        return;
    }

    if (!(item->flags() & Qt::ItemIsEditable))
        return;

    // 避免与慢速单击重命名计时器冲突
    rename_click_timer->stop();
    last_clicked_item = nullptr;
    last_click_time = 0;

    // 启动行内编辑
    ui->list_files->editItem(item);
}



void MainWindow::on_list_files_itemChanged(QListWidgetItem *item)
{
    if (item == nullptr || recycle_mode)
        return;

    FileNode* node =
        item->data(Qt::UserRole).value<FileNode*>();

    if (node == nullptr)
        return;

    QString newName = item->text();
    QString oldName =
        QString::fromStdString(node->get_name());

    if (newName == oldName)
        return;

    // 重命名失败：恢复原名称并退出编辑
    auto restoreName = [this, item, oldName](
                           const QString& message)
    {
        QSignalBlocker blocker(ui->list_files);
        item->setText(oldName);

        QMessageBox::warning(
            this, "重命名失败", message);
    };

    // 1. 名称不能为空
    if (newName.trimmed().isEmpty())
    {
        restoreName("文件名不能为空！");
        return;
    }

    // 2. 文件必须保留 .txt 后缀
    if (!node->is_directory() &&
        !newName.endsWith(".txt", Qt::CaseSensitive))
    {
        restoreName(
            "文件必须保留 .txt 后缀，否则文件将无法正常使用。"
            );
        return;
    }

    // 3. 文件名不能只有后缀
    if (!node->is_directory() && newName == ".txt")
    {
        restoreName("文件名不能只有 .txt 后缀！");
        return;
    }

    // 4. 底层重命名
    bool success =
        file_system->rename_node(
            node, newName.toStdString());

    if (!success)
    {
        restoreName(
            "名称非法或当前目录已经存在同名文件/文件夹。"
            );
        return;
    }

    // 5. 撤销记录
    Operation op;
    op.type = OperationType::RENAME;
    op.node = node;
    op.old_name = oldName.toStdString();
    op.new_name = newName.toStdString();

    undo_stack.push(op);

    // 6. 保存并刷新
    save_data();
    refresh_file_list();
    refresh_tree();

    ui->lineEdit_path->setText(
        toDisplayPath(QString::fromStdString(
            file_system->get_path(
                file_system->get_current_directory()
                )
            ))
        );
}



void MainWindow::on_list_files_itemPressed(QListWidgetItem *item)
{
    // itemPressed 也会在双击的第一下触发，不能在这里直接 editItem。
    rename_click_timer->stop();
    if (item == nullptr || recycle_mode) {
        last_clicked_item = nullptr;
        last_click_time = 0;
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (last_clicked_item == item &&
        now - last_click_time > QApplication::doubleClickInterval()) {
        // 暂缓重命名：如果这实际上是双击，doubleClicked 会取消计时器。
        rename_click_timer->start(QApplication::doubleClickInterval() + 50);
        return;
    }

    last_clicked_item = item;
    last_click_time = now;
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
    if (file_system->is_locked(node) ) {
        bool ok;
        QString pass = QInputDialog::getText(this, "文件已锁定", "该文件已锁定，请输入隐私密码解锁：", QLineEdit::Password, "", &ok);
        if (!ok) return;
        if (verify_privacy_password(pass)) {
            has_privacy_access = true; // 通过验证，临时授权
            refresh_file_list();
        } else {
            QMessageBox::warning(this, "错误", "隐私密码错误！");
            return;
        }
    }
    clipboard_node = node;
    clipboard_mode = ClipboardMode::Copy;
}


void MainWindow::on_btn_cut_clicked()
{
    QListWidgetItem *item = ui->list_files->currentItem();

    if (item == nullptr)
        return;

    FileNode *node =
        item->data(Qt::UserRole).value<FileNode *>();

    if (node == nullptr)
        return;
    if (file_system->is_locked(node) ) {
        bool ok;
        QString pass = QInputDialog::getText(this, "文件已锁定", "该文件已锁定，请输入隐私密码解锁：", QLineEdit::Password, "", &ok);
        if (!ok) return;
        if (verify_privacy_password(pass)) {
            has_privacy_access = true; // 通过验证，临时授权
            refresh_file_list();
        } else {
            QMessageBox::warning(this, "错误", "隐私密码错误！");
            return;
        }
    }
    clipboard_node = node;
    clipboard_mode = ClipboardMode::Cut;
}



void MainWindow::on_btn_paste_clicked()
{
    if (clipboard_node == nullptr ||
        clipboard_mode == ClipboardMode::None)
        return;

    FileNode* source = clipboard_node;
    FileNode* target = file_system->get_current_directory();

    const bool isCopy = (clipboard_mode == ClipboardMode::Copy);

    // 1. 检查剪贴板源节点是否仍在活动文件树中
    bool sourceExists = false;

    file_system->traverse(
        file_system->get_root(),
        [&](FileNode* node) {
            if (node == source)
                sourceExists = true;
        });

    if (!sourceExists || source == file_system->get_root())
    {
        QMessageBox::warning(
            this, "粘贴失败",
            "源文件或文件夹已失效。");

        clipboard_node = nullptr;
        clipboard_mode = ClipboardMode::None;
        return;
    }
    // 2. 防止在同一目录中覆盖自己
    if (source->get_parent() == target)
    {
        QMessageBox::warning(
            this, "粘贴失败",
            "不能在同一目录中粘贴同名项目。");
        return;
    }

    // 3. 防止将文件夹复制/移动到自身或子目录
    for (FileNode* p = target;
         p != nullptr;
         p = p->get_parent())
    {
        if (p == source)
        {
            QMessageBox::warning(
                this, "粘贴失败",
                "不能将文件夹粘贴到自身或其子目录。");
            return;
        }
    }

    // -------------------------------------------------
    // 第一阶段：预检查所有冲突
    // 此阶段不对文件树做任何修改
    // -------------------------------------------------
    bool hasConflict = false;

    std::function<bool(FileNode*, FileNode*)> checkConflicts;

    checkConflicts = [&](FileNode* src,
                         FileNode* destParent) -> bool
    {
        FileNode* existing = file_system->find_child(
            destParent, src->get_name());

        // 无冲突：整个子树都可以直接复制/移动
        if (existing == nullptr)
            return true;

        hasConflict = true;

        QString name = QString::fromStdString(
            src->get_name());

        QString path = QString::fromStdString(
            file_system->get_path(existing));

        // 同名但类型不同，直接拒绝
        if (src->is_directory() !=
            existing->is_directory())
        {
            QMessageBox::warning(
                this, "类型冲突",
                QString("目标位置已存在不同类型的同名项目：\n"
                        "%1\n\n文件与文件夹不能互相替换。")
                    .arg(path));
            return false;
        }

        if (!src->is_directory())
        {
            // 文件冲突：询问替换
            auto result = QMessageBox::question(
                this,
                "同名文件",
                QString("目标位置已经存在文件：\n"
                        "%1\n\n"
                        "是否替换？\n"
                        "选择“是”：旧文件移入回收站，"
                        "新文件替换。\n"
                        "选择“否”：取消本次粘贴。")
                    .arg(path),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);

            return result == QMessageBox::Yes;
        }

        // 文件夹冲突：询问合并
        auto result = QMessageBox::question(
            this,
            "同名文件夹",
            QString("目标位置已经存在文件夹：\n"
                    "%1\n\n"
                    "是否合并两个文件夹？\n"
                    "合并时会保留双方不重名的内容，"
                    "重名文件将单独询问。\n\n"
                    "选择“否”将取消本次粘贴。")
                .arg(path),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (result != QMessageBox::Yes)
            return false;

        // 递归检查文件夹内部的冲突
        for (FileNode* child = src->get_first_child();
             child != nullptr;
             child = child->get_next_sibling())
        {
            if (!checkConflicts(child, existing))
                return false;
        }

        return true;
    };

    // 所有需要确认的冲突，必须先得到同意
    if (!checkConflicts(source, target))
    {
        ui->statusbar->showMessage(
            "用户取消粘贴，原有文件未改变。", 3000);
        return;
    }

    // -------------------------------------------------
    // 第二阶段：执行粘贴
    // 用户已经确认全部冲突
    // -------------------------------------------------
    std::function<bool(FileNode*, FileNode*)> pasteRecursive;

    pasteRecursive = [&](FileNode* src,
                         FileNode* destParent) -> bool
    {
        FileNode* existing = file_system->find_child(
            destParent, src->get_name());

        // 情况 A：目标不存在同名项
        if (existing == nullptr)
        {
            if (isCopy)
                return file_system->copy_node(
                           src, destParent) != nullptr;

            return file_system->move_node(
                src, destParent);
        }

        // 情况 B：同名文件，执行替换
        if (!src->is_directory())
        {
            // 旧文件先移入回收站
            if (!file_system->delete_node(existing))
                return false;

            // 将新文件复制/移动到目标目录
            if (isCopy)
                return file_system->copy_node(
                           src, destParent) != nullptr;

            return file_system->move_node(
                src, destParent);
        }

        // 情况 C：同名文件夹，递归合并
        // existing 保留为合并后的目标文件夹
        FileNode* child = src->get_first_child();

        while (child != nullptr)
        {
            // 移动节点前保存下一个兄弟节点
            FileNode* next = child->get_next_sibling();

            if (!pasteRecursive(child, existing))
                return false;

            child = next;
        }

        // 移动模式：所有内容已经进入目标文件夹
        // 此时源文件夹为空，将其放入回收站
        if (!isCopy)
            return file_system->delete_node(src);

        // 复制模式：源文件夹保持不变
        return true;
    };

    // 记录普通粘贴所需的撤销信息
    FileNode* oldParent = source->get_parent();

    bool success = pasteRecursive(source, target);

    if (!success)
    {
        // 理论上预检查通过后不应发生普通名称冲突
        // 这里处理底层接口意外失败的情况
        save_data();
        refresh_tree();
        refresh_file_list();

        QMessageBox::critical(
            this, "粘贴异常",
            "执行粘贴时发生错误。\n"
            "部分内容可能已经改变，请检查目标文件夹"
            "和回收站。");
        return;
    }

    // -------------------------------------------------
    // 第三阶段：维护撤销栈、剪贴板和界面
    // -------------------------------------------------

    if (hasConflict)
    {
        // 当前撤销系统不能完整撤销递归合并与替换
        // 清除旧历史，避免用户误认为可以撤销本次操作
        while (!undo_stack.empty())
            undo_stack.pop();
    }
    else
    {
        Operation op;

        if (isCopy)
        {
            // 普通复制：寻找新创建的节点
            FileNode* created = file_system->find_child(
                target, source->get_name());

            op.type = OperationType::COPY;
            op.node = created;
        }
        else
        {
            op.type = OperationType::MOVE;
            op.node = source;
            op.old_parent = oldParent;
            op.new_parent = target;
        }

        undo_stack.push(op);
    }

    // 移动完成后清空剪贴板
    // 复制完成后保留，可以继续粘贴
    if (!isCopy)
    {
        clipboard_node = nullptr;
        clipboard_mode = ClipboardMode::None;
    }

    save_data();
    refresh_tree();
    refresh_file_list();

    ui->statusbar->showMessage(
        hasConflict
            ? "粘贴成功：同名内容已按确认结果处理。"
            : "粘贴成功！",
        3000);
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
    QString keyword = ui->lineEdit_search->text().trimmed();
    if (keyword.isEmpty()) {
        QMessageBox::warning(this, "提示", "请在搜索框中输入关键字！");
        return;
    }

    QList<FileNode*> results;

    file_system->traverse(
        file_system->get_current_directory(),
        [&results, &keyword,this](FileNode* node)
        {
            // 👈 加这里：锁定且无权限，跳过不搜索
            if (file_system->is_locked(node) && !has_privacy_access) return;
            QString name =
                QString::fromStdString(
                    node->get_name()
                    );

            if (name.contains(
                    keyword,
                    Qt::CaseInsensitive))
            {
                results.append(node);
            }
        }
    );

    if (results.isEmpty()) {
        QMessageBox::information(this, "搜索", "未找到匹配的内容！");
        return;
    }

    // 把搜索结果展示在右侧列表中
    ui->list_files->clear();

    for (FileNode* node : std::as_const(results)) {
        // 显示完整路径，方便区分不同文件夹下的同名文件
        QString displayPath = QString::fromStdString(file_system->get_path(node));
        QListWidgetItem* item = new QListWidgetItem(displayPath);
        item->setData(Qt::UserRole, QVariant::fromValue(node));
        ui->list_files->addItem(item);
    }
    ui->statusbar->showMessage(QString("共找到 %1 个结果").arg(results.size()), 3000);
}


// 统一的跳转逻辑
void MainWindow::navigate_to(
    const std::string& path,
    bool refreshTree,
    bool recordHistory)
{
    bool success =
        file_system->change_directory_by_path(path);

    if (!success)
    {
        FileNode* current =
            file_system->get_current_directory();

        ui->lineEdit_path->setText(
            toDisplayPath(QString::fromStdString(
                file_system->get_path(file_system->get_current_directory())
                ))
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

    // 只有指定为有效访问时，才更新最近访问
    if (recordHistory)
    {
        QString qPath =
            QString::fromStdString(currentPath);

        // 已存在的项目不移动位置
        if (!history_list.contains(qPath))
        {
            if (history_list.size() < 3)
            {
                // 未满时依次从下方加入
                history_list.append(qPath);
            }
            else
            {
                // 已满后原位替换，只改变一个槽位
                history_list[history_replace_index] = qPath;

                history_replace_index =
                    (history_replace_index + 1) % 3;
            }
        }
    }

    // 更新右侧文件列表
    refresh_file_list();

    // 需要时才重建左侧目录树
    if (refreshTree)
        refresh_tree();

    ui->lineEdit_path->setText(
        toDisplayPath(QString::fromStdString(
            file_system->get_path(file_system->get_current_directory())
            ))
        );
}


void MainWindow::on_list_files_itemDoubleClicked(
    QListWidgetItem *item)
{
    // 双击进入文件夹优先于“再次单击重命名”。
    rename_click_timer->stop();
    last_clicked_item = nullptr;
    last_click_time = 0;
    if (item == nullptr)
        return;

    FileNode* targetNode =
        item->data(Qt::UserRole)
            .value<FileNode*>();

    if (targetNode == nullptr)
        return;

    if (file_system->is_locked(targetNode) ) {
        bool ok;
        QString pass = QInputDialog::getText(this, "文件已锁定", "请输入隐私密码：", QLineEdit::Password, "", &ok);
        if (!ok) return;
        if (verify_privacy_password(pass)) {
            has_privacy_access = true; // 通过验证
            refresh_file_list();
        } else {
            QMessageBox::warning(this, "错误", "密码错误！");
            return;
        }
    }
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
        ui->btn_cut->hide();
        ui->btn_copy->hide();
        ui->btn_paste->hide();
        ui->btn_rename->hide();
        ui->btn_delete->hide();

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
    ui->btn_cut->show();
    ui->btn_copy->show();
    ui->btn_paste->show();
    ui->btn_rename->show();
    ui->btn_delete->show();

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
    {
        // 从回收站返回当前目录时，
        // 虽然底层目录没有变化，但右侧界面需要恢复普通目录内容
        refresh_file_list();

        ui->lineEdit_path->setText(
            toDisplayPath(QString::fromStdString(
                file_system->get_path(file_system->get_current_directory())
                ))
            );

        return;
    }

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
    navigate_to(targetPath, false, false);

}


void MainWindow::on_btn_refresh_clicked()
{
    refresh_tree();
    refresh_file_list();
}


void MainWindow::save_node_recursive(FileNode* node, std::ofstream& out) {
    if (node == nullptr) return;

    std::string path = file_system->get_path(node);

    // 👇 【新增：判断是否需要加密内容】
    std::string contentToSave = "";
    if (!node->is_directory()) {
        if (node->is_locked()) {
            // 如果文件被锁定，保存加密后的内容
            contentToSave = encryptContent(node->get_content());
        } else {
            // 没锁，按原样保存
            contentToSave = node->get_content();
        }
    }
    // 写入 8 个字段
    out << (node->is_directory() ? "D" : "F") << "|"
        << path << "|"
        << node->get_created_time() << "|"
        << node->get_modified_time() << "|"
        << (node->is_pinned() ? "1" : "0") << "|"
        << node->get_pin_order() << "|"
        << node->get_size() << "|"
       //<< (node->is_directory() ? "" : node->get_content()) << "|"
         << contentToSave << "|"  // 👈 替换原来的 content
        << (node->is_locked() ? "1" : "0") << "\n";

    // 递归遍历子节点
    FileNode* child = node->get_first_child();
    while (child != nullptr) {
        save_node_recursive(child, out);
        child = child->get_next_sibling();
    }
}


void MainWindow::save_data()
{
    QString filename = "data_" + current_user + ".txt";
    std::ofstream out(filename.toStdString());

    if (!out.is_open())
    {
        QMessageBox::warning(
            this,
            "错误",
            "无法创建保存文件！"
            );
        return;
    }

    // 保存普通文件树
    save_node_recursive(
        file_system->get_root(),
        out
        );

    // 回收站顶层保持旧 R 格式；子节点新增 RC|深度|... 格式。
    // 深度 1 代表顶层回收站文件夹的直接孩子。
    file_system->for_each_recycle_item(
        [&](FileNode* root, const std::string& original_path)
        {
            std::function<void(FileNode*, int)> writeRecycle =
                [&](FileNode* node, int depth)
            {
                if (depth == 0)
                    out << "R|";
                else
                    out << "RC|" << depth << "|";

                out << (node->is_directory() ? "D" : "F") << "|"
                    << (depth == 0 ? original_path : "") << "|"
                    << node->get_name() << "|"
                    << node->get_created_time() << "|"
                    << node->get_modified_time() << "|"
                    << (node->is_pinned() ? "1" : "0") << "|"
                    << node->get_pin_order() << "|"
                    << node->get_size() << "|"
                    << (node->is_directory() ? "" : node->get_content()) << "|"
                    << (node->is_locked() ? "1" : "0") << "\n";

                for (FileNode* child = node->get_first_child();
                     child != nullptr; child = child->get_next_sibling())
                    writeRecycle(child, depth + 1);
            };
            writeRecycle(root, 0);
        });

    // 保存最近访问记录
    for (const QString& path : std::as_const(history_list))
    {
        out << "H|"
            << path.toStdString()
            << "\n";
    }

    // 保存下一个历史替换槽位
    out << "HIDX|"
        << history_replace_index
        << "\n";

    out.close();

    ui->statusbar->showMessage(
        "数据保存成功！",
        2000
        );
}


void MainWindow::load_data()
{
    QString filename = "data_" + current_user + ".txt";
    std::ifstream in(filename.toStdString());

    // 第一次运行时没有 data.txt，直接返回
    if (!in.is_open())
        return;

    // 防止重复调用 load_data() 时历史记录重复
    history_list.clear();

    std::string line;
    std::vector<FileNode*> recycleParents; // 索引为 RC 的深度

    while (std::getline(in, line))
    {
        if (line.empty())
            continue;

        std::stringstream ss(line);

        std::string type;
        std::getline(ss, type, '|');


        // =======================================================
        // 最近访问记录
        // 格式：H|完整路径
        // =======================================================
        if (type == "H")
        {
            std::string historyPath;

            // 路径本身不使用 '|'，
            // 所以直接读取这一行剩余内容
            std::getline(ss, historyPath);

            if (!historyPath.empty())
            {
                history_list.append(
                    QString::fromStdString(historyPath)
                    );
            }

            continue;
        }

        if (type == "HIDX")
        {
            std::string indexStr;
            std::getline(ss, indexStr);

            try
            {
                history_replace_index =
                    std::stoi(indexStr);

                // 防止 data.txt 被改坏
                if (history_replace_index < 0 ||
                    history_replace_index >= 3)
                {
                    history_replace_index = 0;
                }
            }
            catch (...)
            {
                history_replace_index = 0;
            }

            continue;
        }

        // =======================================================
        // 回收站数据
        // 格式：
        // R|D或F|原路径|名称|创建时间|修改时间
        //  |置顶|pin_order|大小|内容
        // =======================================================
        if (type == "R" || type == "RC")
        {
            int recycleDepth = 0;
            if (type == "RC") {
                std::string depthStr;
                std::getline(ss, depthStr, '|');
                try { recycleDepth = std::stoi(depthStr); }
                catch (...) { continue; }
                if (recycleDepth < 1 || recycleDepth > 1000 ||
                    static_cast<size_t>(recycleDepth - 1) >= recycleParents.size() ||
                    recycleParents[recycleDepth - 1] == nullptr)
                    continue;
            } else {
                recycleParents.clear();
            }
            std::string is_dir_str;
            std::string original_path;
            std::string name;
            std::string c_time;
            std::string m_time;
            std::string pinned_str;
            std::string pin_order_str;
            std::string size_str;
            std::string content_str;
            std::string locked_str;

            std::getline(ss, is_dir_str, '|');
            std::getline(ss, original_path, '|');
            std::getline(ss, name, '|');
            std::getline(ss, c_time, '|');
            std::getline(ss, m_time, '|');
            std::getline(ss, pinned_str, '|');
            std::getline(ss, pin_order_str, '|');
            std::getline(ss, size_str, '|');
            std::getline(ss, content_str, '|');
            std::getline(ss, locked_str, '|');

            bool is_directory =
                (is_dir_str == "D");

            FileNode* recycledNode =
                file_system->create_recycle_node_for_load(
                    recycleDepth == 0 ? nullptr : recycleParents[recycleDepth - 1],
                    name,
                    is_directory,
                    original_path
                    );

            if (recycledNode != nullptr)
            {
                long long size = 0;
                try { size = std::stoll(size_str); }
                catch (...) { size = 0; }

                // 回收站节点不属于正常文件树，必须使用专用恢复接口。
                file_system->restore_recycle_data(
                    recycledNode, is_directory ? "" : content_str,
                    size, c_time, m_time);

                bool is_pinned =
                    (pinned_str == "1");

                long long pin_order = -1;

                try
                {
                    pin_order =
                        std::stoll(pin_order_str);
                }
                catch (...)
                {
                    pin_order = -1;
                }

                file_system->restore_pin_state(
                    recycledNode,
                    is_pinned,
                    pin_order
                    );

                // 先恢复内容及元数据，最后设置锁定状态
                file_system->restore_recycle_lock_state(
                    recycledNode, locked_str == "1"
                    );

                if (recycleParents.size() <= static_cast<size_t>(recycleDepth))
                    recycleParents.resize(recycleDepth + 1, nullptr);
                recycleParents[recycleDepth] = recycledNode;
                recycleParents.resize(recycleDepth + 1);
            }

            // 回收站节点不能继续走普通文件树加载逻辑
            continue;
        }


        // =======================================================
        // 普通文件树数据
        // 格式：
        // D或F|路径|创建时间|修改时间
        //    |置顶|pin_order|大小|内容
        // =======================================================
        std::string path;
        std::string c_time;
        std::string m_time;
        std::string pinned_str;
        std::string pin_order_str;
        std::string size_str;
        std::string content_str;
        std::string locked_str;

        std::getline(ss, path, '|');
        std::getline(ss, c_time, '|');
        std::getline(ss, m_time, '|');
        std::getline(ss, pinned_str, '|');
        std::getline(ss, pin_order_str, '|');
        std::getline(ss, size_str, '|');
        std::getline(ss, content_str, '|');
        std::getline(ss, locked_str, '|');

        if (path == "/" || path.empty())
            continue;

        size_t lastSlash =
            path.find_last_of('/');

        if (lastSlash == std::string::npos)
            continue;

        std::string parentPath =
            path.substr(0, lastSlash);

        if (parentPath.empty())
            parentPath = "/";

        std::string name =
            path.substr(lastSlash + 1);

        FileNode* parentNode =
            file_system->find_by_path(parentPath);

        if (parentNode == nullptr)
            continue;

        FileNode* newNode = nullptr;

        if (type == "D")
        {
            newNode =
                file_system->create_folder(
                    parentNode,
                    name
                    );
        }
        else if (type == "F")
        {
            std::string finalContent = "";

            // 👇 【新增：判断是否需要解密】
            if (locked_str == "1") {
                // 如果是锁定文件，说明文件里存的是加密后的 Base64 字符串，需要先解密
                finalContent = decryptContent(content_str);
            } else {
                // 普通文件，直接使用明文
                finalContent = content_str;
            }

            // 把最终内容设置进节点
            file_system->set_content(newNode, finalContent);
        }
        else
        {
            // 未知记录类型直接忽略
            continue;
        }

        if (newNode != nullptr)
        {
            // 文件内容会影响 size 和修改时间，
            // 所以先恢复内容。
            if (type == "F")
            {
                file_system->set_content(
                    newNode,
                    content_str
                    );
            }

            long long size = 0;

            try
            {
                size =
                    std::stoll(size_str);
            }
            catch (...)
            {
                size = 0;
            }

            // 再恢复原始大小和时间
            file_system->set_metadata(
                newNode,
                size,
                c_time,
                m_time
                );

            bool is_pinned =
                (pinned_str == "1");

            long long pin_order = -1;

            try
            {
                pin_order =
                    std::stoll(pin_order_str);
            }
            catch (...)
            {
                pin_order = -1;
            }

            // 最后恢复置顶状态和顺序
            file_system->restore_pin_state(
                newNode,
                is_pinned,
                pin_order
                );

            // 先恢复内容及元数据，最后设置锁定状态
            file_system->restore_lock_state(
                newNode, locked_str == "1"
                );
        }
    }

    in.close();
}


// 重写 closeEvent（极其核心的防崩溃逻辑！）
void MainWindow::closeEvent(QCloseEvent *event)
{
    save_data(); // 1. 无论怎么关，先保存当前用户的数据！

    if (is_logout_action) {
        // === 2. 如果是注销，必须彻底清理所有旧内存，防止新用户登录后指针悬空崩溃 ===
        delete file_system; // 释放底层文件系统
        file_system = nullptr;

        // 清空所有栈和历史记录
        while (!undo_stack.empty()) undo_stack.pop();
        while (!back_stack.empty()) back_stack.pop();
        while (!forward_stack.empty()) forward_stack.pop();
        history_list.clear();

        // 清空剪贴板
        clipboard_node = nullptr;
        clipboard_mode = ClipboardMode::None;
        recycle_mode = false;

        is_logout_action = false; // 恢复标记，为下一次登录做准备
    }

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


void MainWindow::addToListWidget(FileNode* node)
{
    if (node == nullptr)
        return;

    QString name =
        QString::fromStdString(node->get_name());

    QListWidgetItem* item = new QListWidgetItem(name);

    // 保留原有的底层节点指针
    item->setData(
        Qt::UserRole,
        QVariant::fromValue(node)
        );

    // 保留行内重命名
    item->setFlags(
        item->flags() | Qt::ItemIsEditable
        );

    // SVG 图标与状态
    item->setIcon(makeFileNodeIcon(node));

    // 悬停提示
    QString tooltip =
        node->is_directory() ? "类型：文件夹" : "类型：TXT 文本文件";

    tooltip += "\n名称：" + name;

    if (node->is_pinned())
        tooltip += "\n状态：已置顶";

    if (node->is_locked())
        tooltip += "\n状态：已锁定";

    item->setToolTip(tooltip);

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
    if (undo_stack.empty())
    {
        QMessageBox::warning(
            this,
            "撤销",
            "没有可以撤销的操作！"
            );
        return;
    }

    // 保存撤销前所在目录
    std::string oldCurrentPath =
        file_system->get_path(
            file_system->get_current_directory()
            );

    // 取出栈顶操作
    Operation op =
        undo_stack.top();

    // 执行逆操作
    bool success =
        file_system->undo(op);

    if (success)
    {
        undo_stack.pop();

        // 如果撤销前所在目录仍然存在，
        // 就继续停留在原目录。
        // 只有目录已经不存在时才退回根目录。
        FileNode* oldCurrentNode =
            file_system->find_by_path(
                oldCurrentPath
                );

        if (oldCurrentNode != nullptr &&
            oldCurrentNode->is_directory())
        {
            file_system->change_directory_by_path(
                oldCurrentPath
                );
        }
        else
        {
            file_system->change_directory_by_path("/");
        }

        refresh_tree();
        refresh_file_list();

        ui->lineEdit_path->setText(
            toDisplayPath(QString::fromStdString(
                file_system->get_path(file_system->get_current_directory())
                ))
            );

        save_data();

        ui->statusbar->showMessage(
            "撤销成功！",
            2000
            );
    }
    else
    {
        // 防止无法撤销的操作一直卡在栈顶
        undo_stack.pop();

        QMessageBox::warning(
            this,
            "撤销失败",
            "该操作无法撤销。"
            );
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

        // 清空撤销栈
        while (!undo_stack.empty())
            undo_stack.pop();

        // 清空导航栈
        while (!back_stack.empty())
            back_stack.pop();

        while (!forward_stack.empty())
            forward_stack.pop();

        // 清空访问历史及替换状态
        history_list.clear();
        history_replace_index = 0;

        // 清空剪贴板
        clipboard_node = nullptr;
        clipboard_mode = ClipboardMode::None;

        // 清除列表项相关旧指针
        last_clicked_item = nullptr;
        last_click_time = 0;

        // 退出回收站模式
        recycle_mode = false;

        // 恢复普通操作按钮
        ui->btn_new->show();
        ui->btn_cut->show();
        ui->btn_copy->show();
        ui->btn_paste->show();
        ui->btn_rename->show();
        ui->btn_delete->show();

        // 隐藏回收站操作
        ui->btn_restore->hide();
        ui->btn_permanent_delete->hide();
        ui->btn_clear_recycle->hide();

        // =======================================================
        // 5. 刷新界面并持久化
        // =======================================================
        refresh_tree();
        refresh_file_list();

        // 重置后底层已自动将目录指回 root，直接读取底层路径
        ui->lineEdit_path->setText(toDisplayPath(QString::fromStdString(
            file_system->get_path(file_system->get_current_directory()))));

        save_data(); // 将重置后的空状态立刻写入 data.txt，覆盖旧存档

        QMessageBox::information(this, "重置成功", "系统已成功重置！");

}

void MainWindow::on_btn_logout_clicked()
{
    // 注销按钮：清理环境，回到登录界面

        if (QMessageBox::question(this, "注销", "确定要切换账号吗？") != QMessageBox::Yes) return;

        is_logout_action = true; // 标记为注销
        qApp->setProperty("is_logout_requested", true); // 通知 main.cpp 继续循环

        this->close(); // 触发 closeEvent

}


void MainWindow::on_btn_exit_clicked()
{
    // 退出按钮：彻底结束程序

        if (QMessageBox::question(this, "退出", "确定要彻底退出程序吗？") != QMessageBox::Yes) return;

        is_logout_action = false; // 标记为退出
        qApp->setProperty("is_logout_requested", false);

        this->close();

}

// 哈希函数（与 LoginDialog 内完全一致）
QString MainWindow::hashPassword(const QString& password) {
    QString salt = "FileManager_Salt_2024";
    return QString(QCryptographicHash::hash((password + salt).toUtf8(), QCryptographicHash::Sha256).toHex());
}

// 验证隐私密码（从 users.txt 读取当前用户的隐私密码哈希比对）
bool MainWindow::verify_privacy_password(const QString& password) {
    QFile file("users.txt");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QStringList parts = in.readLine().split("|");
        if (parts.size() >= 3 && parts[0] == current_user) {
            file.close();
            return parts[2] == hashPassword(password);
        }
    }
    file.close();
    return false;
}

void MainWindow::on_action_lock_clicked()
{
    QListWidgetItem* item = ui->list_files->currentItem();
    if (!item) return;
    FileNode* node = item->data(Qt::UserRole).value<FileNode*>();
    if (!node) return;

    // 如果是锁定状态，要解锁（需要隐私密码）
    if (file_system->is_locked(node)) {
        bool ok;
        QString pass = QInputDialog::getText(this, "解锁", "请输入隐私密码：", QLineEdit::Password, "", &ok);
        if (ok && verify_privacy_password(pass)) {
            file_system->set_locked(node, false); // 解锁
            refresh_file_list();
            save_data(); // 触发持久化
            ui->statusbar->showMessage("已解锁！", 2000);
        } else if (ok) {
            QMessageBox::warning(this, "错误", "隐私密码错误！");
        }
    }
    // 如果不是锁定状态，直接上锁
    else {
        file_system->set_locked(node, true); // 上锁
        refresh_file_list();
        save_data(); // 触发持久化
        ui->statusbar->showMessage("已上锁！", 2000);
    }
}
void MainWindow::on_action_show_privacy_triggered()
{
    // 如果当前是“显示”状态，点击后变成“隐藏”
    if (show_privacy_mode) {
        show_privacy_mode = false;
        has_privacy_access = false; // 收回权限
        refresh_file_list();
        ui->statusbar->showMessage("已隐藏隐私文件", 2000);
        return;
    }

    // 如果当前是“隐藏”状态，必须验证密码才能显示
    bool ok;
    QString pass = QInputDialog::getText(this, "显示隐私文件", "请输入隐私密码查看隐私文件：", QLineEdit::Password, "", &ok);
    if (ok && verify_privacy_password(pass)) {
        show_privacy_mode = true;
        has_privacy_access = true; // 授予当前会话权限
        refresh_file_list();
        ui->statusbar->showMessage("隐私文件已显示", 3000);
    } else if (ok) {
        QMessageBox::warning(this, "错误", "隐私密码错误！");
    }
}

// 简单的异或加密 + Base64编码
std::string MainWindow::encryptContent(const std::string& content)
{
    if (content.empty()) return content;

    std::string encrypted = content;
    const char key = 0x5A; // 加密密钥

    for (char& c : encrypted) {
        c ^= key;
    }

    // 使用 Qt 的 Base64 编码，防止出现 '|' 破坏文件格式
    QByteArray data(encrypted.c_str(), static_cast<int>(encrypted.size()));
    return data.toBase64().toStdString();
}

// 先 Base64 解码，再进行异或解密
std::string MainWindow::decryptContent(const std::string& encryptedContent)
{
    if (encryptedContent.empty()) return encryptedContent;

    // Base64 解码
    QByteArray data = QByteArray::fromBase64(
        QByteArray::fromStdString(encryptedContent));

    std::string decrypted(data.constData(), data.size());
    const char key = 0x5A;

    for (char& c : decrypted) {
        c ^= key;
    }

    return decrypted;
}