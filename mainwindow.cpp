#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "FileDetailsDelegate.h"
#include <QScopeGuard>

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

#include <QPlainTextEdit> // 文本编辑
#include <QPushButton>

#include <QSaveFile> // save
#include <ostream>
#include <memory>
#include <QFileInfo>
#include "PrivacyCrypto.h"

// 路径显示
static QString toDisplayPath(const QString& path)
{
    if (path == "/")
        return "/此电脑";

    return "/此电脑" + path;
}



// 文件内容存档辅助函数
// Base64 用来确保文件内容不会破坏
// 将任意字节内容编码为 Base64
static std::string encodePlainContent(
    const std::string& content)
{
    QByteArray bytes(
        content.data(),
        static_cast<qsizetype>(content.size())
        );

    return bytes.toBase64().toStdString();
}

// 将 Base64 解码为原始字节
// 返回 false 表示内容格式不合法
static bool decodeBase64Content(
    const std::string& encoded,
    std::string& output)
{
    QByteArray input =
        QByteArray::fromStdString(encoded);

    QByteArray decoded =
        QByteArray::fromBase64(input);

    // 本项目保存时使用标准 Base64，
    // 因此重新编码后应与原始字符串一致。
    if (decoded.toBase64() != input)
        return false;

    output = decoded.toStdString();
    return true;
}

// 处理旧存档行末可能存在的 CR 字符
static void removeTrailingCR(std::string& value)
{
    if (!value.empty() && value.back() == '\r')
        value.pop_back();
}


// 图标
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



class TxtEditorDialog : public QDialog
{
public:
    TxtEditorDialog(
        FileSystem* fileSystem,
        FileNode* fileNode,
        std::function<bool()> saveCallback,
        QWidget* parent = nullptr
        )
        : QDialog(parent)
        , file_system_(fileSystem)
        , file_node_(fileNode)
        , save_callback_(std::move(saveCallback))
    {
        setWindowTitle(
            "TXT 编辑器 - " +
            QString::fromStdString(file_node_->get_name())
            );

        resize(720, 520);
        setMinimumSize(420, 300);

        auto* layout = new QVBoxLayout(this);

        // 多行文本编辑框
        editor_ = new QPlainTextEdit(this);

        editor_->setPlaceholderText(
            "在这里输入 TXT 文件内容..."
            );

        editor_->setPlainText(
            QString::fromStdString(
                file_node_->get_content()
                )
            );

        // 初次加载内容不算用户修改
        editor_->document()->setModified(false);

        layout->addWidget(editor_);

        // 保存和关闭按钮
        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Save |
                QDialogButtonBox::Close,
            this
            );

        layout->addWidget(buttons);

        QPushButton* saveButton =
            buttons->button(QDialogButtonBox::Save);

        QPushButton* closeButton =
            buttons->button(QDialogButtonBox::Close);

        saveButton->setText("保存");
        closeButton->setText("关闭");

        connect(
            saveButton,
            &QPushButton::clicked,
            this,
            [this]()
            {
                if (saveChanges())
                {
                    QMessageBox::information(
                        this,
                        "保存",
                        "文件内容已更新，并已执行数据保存。"
                        );
                }
            }
            );

        connect(
            closeButton,
            &QPushButton::clicked,
            this,
            &QDialog::close
            );
    }

protected:
    void closeEvent(QCloseEvent* event) override
    {
        if (!editor_->document()->isModified())
        {
            event->accept();
            return;
        }

        QMessageBox::StandardButton choice =
            QMessageBox::warning(
                this,
                "内容尚未保存",
                "文件内容已经修改，是否保存？",
                QMessageBox::Save |
                    QMessageBox::Discard |
                    QMessageBox::Cancel,
                QMessageBox::Save
                );

        if (choice == QMessageBox::Cancel)
        {
            event->ignore();
            return;
        }

        if (choice == QMessageBox::Save)
        {
            if (!saveChanges())
            {
                event->ignore();
                return;
            }
        }

        // Discard 直接关闭，内存中的原内容不变
        event->accept();
    }

    void reject() override
    {
        // Escape 键也应经过未保存检查
        close();
    }

private:
    bool saveChanges()
    {
        if (file_system_ == nullptr ||
            file_node_ == nullptr)
            return false;

        // 把编辑器中的 Unicode 文本转换为 UTF-8
        std::string newContent =
            editor_->toPlainText().toStdString();

        // 通过 FileSystem 修改内容，
        // 自动更新大小和修改时间
        if (!file_system_->set_content(
                file_node_, newContent))
        {
            QMessageBox::warning(
                this,
                "保存失败",
                "无法更新文件内容。"
                );
            return false;
        }


        // 保存到当前用户的数据文件。
        // 只有真正提交成功，才标记编辑器为已保存。
        if (!save_callback_ || !save_callback_())
        {
            QMessageBox::warning(
                this,
                "保存失败",
                "文件内容已更新到当前程序内存，"
                "但写入磁盘失败。\n"
                "请检查保存路径和磁盘状态，然后重试保存。"
                );

            // 保留未保存标记，方便用户重新尝试
            return false;
        }

        editor_->document()->setModified(false);
        return true;

    }

private:
    FileSystem* file_system_ = nullptr;
    FileNode* file_node_ = nullptr;
    std::function<bool()> save_callback_;
    QPlainTextEdit* editor_ = nullptr;
};



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
        QIcon(":/icons/icons/search.svg"),
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
    setup_integrated_tools();
    auto aiAction = menuBar()->addAction("DeepSeek 辅助");
    connect(aiAction, &QAction::triggered, this, &MainWindow::open_ai_tools);

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
        if (!recycle_mode &&
            !privacy_overview_mode &&
            item != nullptr &&
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

                // 回收站和隐私总览不是普通文件夹路径
                if (recycle_mode || privacy_overview_mode) {
                    QMessageBox::information(
                        this, "提示",
                        "请先从左侧目录树进入普通文件夹。"
                        );
                    ui->lineEdit_path->setText(
                        recycle_mode ? "回收站" : "隐私文件"
                        );
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

                // 导航前先检查隐私权限。
                // 不允许先修改后退栈，再发现目标不可访问。
                if (!can_access_node(targetNode))
                {
                    QMessageBox::warning(
                        this,
                        "访问受限",
                        "目标文件夹受到隐私保护，请先验证密码。"
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
            !privacy_overview_mode &&
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
            !privacy_overview_mode &&
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
            !privacy_overview_mode &&
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
            !privacy_overview_mode &&
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
            !privacy_overview_mode &&
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

    connect(undoShortcut, &QShortcut::activated,this, [this]() {
        if (!recycle_mode &&
            !privacy_overview_mode &&
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
                // 隐私总览目前只负责定位，
                // 不直接提供剪切、删除等普通右键操作
                if (privacy_overview_mode)
                    return;

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
                    privacyAction->setEnabled(true); // 解除封印

                    // 绑定槽函数
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
                        button->setToolTip(text);
                        button->setAccessibleName(text);
                        QString resource = text == "剪切" ? "cut" : text == "复制" ? "copy" : text == "重命名" ? "rename" : "delete";
                        QIcon icon(":/icons/icons/" + resource + ".svg");
                        if (icon.isNull()) { button->setIcon(style()->standardIcon(iconType)); button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon); }
                        else { button->setIcon(icon); button->setText({}); button->setToolButtonStyle(Qt::ToolButtonIconOnly); }

                        button->setAutoRaise(true);
                        button->setMinimumWidth(36);

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
                                // 1. 统一权限检查
                                if (!can_access_node(node))
                                {
                                    QMessageBox::warning(
                                        this,
                                        "访问受限",
                                        "请先通过“显示 → 隐私文件”验证权限。"
                                        );
                                    return;
                                }

                                // 2. 普通 TXT 文件：进入内置编辑器
                                if (!node->is_directory())
                                {
                                    open_text_editor(node);
                                    return;
                                }

                                // 3. 获取当前目录和目标目录路径
                                std::string currentPath =
                                    file_system->get_path(
                                        file_system->get_current_directory()
                                        );

                                std::string targetPath =
                                    file_system->get_path(node);

                                // 4. 已经在目标目录，无需重复跳转
                                if (currentPath == targetPath)
                                    return;

                                // 5. 保存后退历史
                                back_stack.push(currentPath);

                                // 清空前进历史
                                while (!forward_stack.empty())
                                    forward_stack.pop();

                                // 6. 统一执行目录跳转
                                navigate_to(targetPath);
                            });

                    menu.addAction("自动归类", this, &MainWindow::auto_classify);

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



void MainWindow::open_text_editor(FileNode* node)
{
    if (node == nullptr)
        return;

    // 回收站不直接编辑文件内容
    if (file_system->is_in_recycle_bin(node))
    {
        QMessageBox::information(
            this,
            "无法编辑",
            "请先将文件从回收站恢复。"
            );
        return;
    }

    if (node->is_directory())
    {
        QMessageBox::information(
            this,
            "提示",
            "文件夹不能使用 TXT 编辑器打开。"
            );
        return;
    }

    // 自身锁定和继承保护统一检查
    if (!can_access_node(node))
    {
        QMessageBox::warning(
            this,
            "访问受限",
            "该文件受到隐私保护，请先验证隐私密码。"
            );
        return;
    }

    // 编辑器关闭前不会返回，
    // 避免同时修改底层节点
    TxtEditorDialog dialog(
        file_system,
        node,
        [this]() -> bool
        {
            return save_data();
        },
        this
        );

    dialog.exec();

    // 文件大小和修改时间可能发生变化
    refresh_file_list();
    refresh_tree();
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
    discard_privacy_key();
    delete ui;
    delete file_system; // 记得释放内存
}



bool MainWindow::is_privacy_protected(FileNode* node) const
{
    // 从当前节点一直检查到根目录
    for (FileNode* p = node;
         p != nullptr;
         p = p->get_parent())
    {
        if (p->is_locked())
            return true;
    }

    return false;
}

bool MainWindow::can_access_node(FileNode* node) const
{
    if (node == nullptr)
        return false;

    // 异常情况下即使节点锁标志消失，也不能把尚未解密的 E1 当成普通文本。
    if (!node->get_sealed_content().empty() && crypto_session_key_.empty())
        return false;

    // 普通节点不需要授权
    if (!is_privacy_protected(node))
        return true;

    // 受保护节点必须同时满足：
    // 已验证隐私密码 + 处于显示隐私文件模式
    return show_privacy_mode && has_privacy_access;
}



void MainWindow::refresh_file_list()
{
    search_index_valid_ = false; search_index_nodes_.clear(); search_name_index_.clear();
    search_mode_ = false;
    if (search_options_button_ && !ui->lineEdit_search->hasFocus()) search_options_button_->hide();
    auto sortOnExit = qScopeGuard([this] { apply_file_sort(); });
    if (rename_click_timer) rename_click_timer->stop();
    last_clicked_item = nullptr;
    last_click_time = 0;

    QSignalBlocker blocker(ui->list_files);

    ui->list_files->clear();


    // ==================================================
    // 隐私文件
    // ==================================================
    // 只显示主动上锁的节点，不重复列出继承保护的子节点
    if (privacy_overview_mode)
    {
        if (!show_privacy_mode || !has_privacy_access)
        {
            privacy_overview_mode = false;
            return;
        }

        file_system->traverse(
            file_system->get_root(),
            [this](FileNode* node)
            {
                if (node == nullptr ||
                    node == file_system->get_root())
                    return;

                // 只有节点自身上锁才纳入总览
                if (!node->is_locked())
                    return;

                // 显示完整路径，避免同名文件混淆
                QString path = toDisplayPath(
                    QString::fromStdString(
                        file_system->get_path(node)
                        )
                    );

                QListWidgetItem* item =
                    new QListWidgetItem(path);

                item->setData(
                    Qt::UserRole,
                    QVariant::fromValue(node)
                    );

                item->setIcon(makeFileNodeIcon(node));

                // 总览中不允许直接行内重命名
                item->setFlags(
                    item->flags() & ~Qt::ItemIsEditable
                    );

                item->setToolTip(
                    "隐私文件\n原始位置：" + path
                    );

                ui->list_files->addItem(item);
            }
            );

        return;
    }


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

        if (!can_access_node(child))
        {
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
        if (!can_access_node(child))
        {
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
            if (!can_access_node(child))
            {
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
    // 第二部分：隐私文件
    // ==================================================
    // 类型 2 表示特殊的隐私文件视图
    QTreeWidgetItem* privacyItem =
        new QTreeWidgetItem(ui->tree_dir);

    privacyItem->setText(0, "隐私文件");

    privacyItem->setIcon(
        0,
        QIcon(":/icons/icons/lock.svg")
        );

    privacyItem->setData(
        0,
        Qt::UserRole + 1,
        2
        );

    privacyItem->setToolTip(
        0,
        "验证隐私密码后，集中查看所有主动上锁的文件和文件夹。"
        );


    // ==================================================
    // 第三部分：置顶项目
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
                if (!can_access_node(node)) {
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
    // 第四部分：最近访问
    // ==================================================
    // history_list 内部仍然保存完整路径
    // 左侧只显示最后一级
    for (const QString& path : std::as_const(history_list))
    {
        if (path == "/") continue;
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
        if (!can_access_node(node))
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
    // 第五部分：真实目录树
    // ==================================================
    auto separator = new QTreeWidgetItem(ui->tree_dir);
    separator->setData(0, Qt::UserRole + 1, 99);
    separator->setFlags(Qt::NoItemFlags);
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
    if (recycle_mode || privacy_overview_mode)
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

    if (recycle_mode || privacy_overview_mode)
        return;

    QListWidgetItem* item = ui->list_files->currentItem();

    if (item == nullptr) {
        QMessageBox::warning(
            this, "提示",
            "请先选择要重命名的文件或文件夹！");
        return;
    }

    FileNode* node =
        item->data(Qt::UserRole).value<FileNode*>();

    if (node == nullptr || !can_access_node(node))
    {
        QMessageBox::warning(
            this,
            "重命名失败",
            "文件不存在或当前没有访问权限。"
            );
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
    if (item == nullptr || recycle_mode || privacy_overview_mode)
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


    // 提交新文件名之前，再检查一次当前隐私权限。
    if (!can_access_node(node))
    {
        QSignalBlocker blocker(ui->list_files);

        // 恢复原名称，避免界面显示与底层不一致
        item->setText(oldName);

        QMessageBox::warning(
            this,
            "重命名失败",
            "当前文件受到隐私保护，请先验证隐私密码。"
            );

        return;
    }

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
    if (item == nullptr ||
        recycle_mode ||
        privacy_overview_mode)
    {
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
    if (recycle_mode || privacy_overview_mode)
        return;

    QListWidgetItem *item = ui->list_files->currentItem();

    if (item == nullptr)
        return;

    FileNode *node =
        item->data(Qt::UserRole).value<FileNode *>();

    if (node == nullptr)
        return;

    if (!can_access_node(node))
    {
        QMessageBox::warning(
            this,
            "访问受限",
            "请先通过“显示 → 隐私文件”验证权限。"
            );
        return;
    }

    clipboard_node = node;
    clipboard_mode = ClipboardMode::Copy;
}


void MainWindow::on_btn_cut_clicked()
{
    if (recycle_mode || privacy_overview_mode)
        return;

    QListWidgetItem *item = ui->list_files->currentItem();

    if (item == nullptr)
        return;

    FileNode *node =
        item->data(Qt::UserRole).value<FileNode *>();

    if (node == nullptr)
        return;

    if (!can_access_node(node))
    {
        QMessageBox::warning(
            this,
            "访问受限",
            "请先通过“显示 → 隐私文件”验证权限。"
            );
        return;
    }

    clipboard_node = node;
    clipboard_mode = ClipboardMode::Cut;
}


void MainWindow::on_btn_paste_clicked()
{
    if (recycle_mode || privacy_overview_mode)
        return;

    if (clipboard_node == nullptr ||
        clipboard_mode == ClipboardMode::None)
        return;

    FileNode* source = clipboard_node;
    FileNode* target = file_system->get_current_directory();

    const bool isCopy =
        (clipboard_mode == ClipboardMode::Copy);

    // 1. 先检查源节点是否仍存在。
    bool sourceExists = false;

    file_system->traverse(
        file_system->get_root(),
        [&](FileNode* node)
        {
            if (node == source)
                sourceExists = true;
        }
        );

    if (!sourceExists || source == file_system->get_root())
    {
        QMessageBox::warning(
            this,
            "粘贴失败",
            "源文件或文件夹已失效。"
            );

        clipboard_node = nullptr;
        clipboard_mode = ClipboardMode::None;
        return;
    }

    // 2. 确认节点有效后，才检查访问权限。
    if (!can_access_node(source) ||
        !can_access_node(target))
    {
        QMessageBox::warning(
            this,
            "粘贴失败",
            "源文件或目标目录没有访问权限。"
            );
        return;
    }

    // 复制整个文件夹时，必须检查其所有子节点。
    // 不能因为顶层文件夹未上锁，就跳过内部的隐私文件。
    if (isCopy)
    {
        bool containsInaccessibleNode = false;

        file_system->traverse(
            source,
            [this, &containsInaccessibleNode](FileNode* node)
            {
                if (!can_access_node(node))
                {
                    containsInaccessibleNode = true;
                }
            }
            );

        if (containsInaccessibleNode)
        {
            QMessageBox::warning(
                this,
                "复制失败",
                "源文件夹包含未授权的隐私文件。\n"
                "请先验证隐私密码并显示隐私文件。"
                );
            return;
        }
    }

    // 3. 防止在同一目录中覆盖自己
    if (source->get_parent() == target)
    {
        QMessageBox::warning(
            this, "粘贴失败",
            "不能在同一目录中粘贴同名项目。");
        return;
    }

    // 4. 防止将文件夹复制/移动到自身或子目录
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
        // 递归合并可能深入源文件夹内部。
        // 与直接移动整个文件夹不同，展开合并时必须有访问权限。
        if (!can_access_node(src) ||
            !can_access_node(destParent))
        {
            QMessageBox::warning(
                this,
                "粘贴失败",
                "递归合并涉及未授权的隐私目录或文件。"
                );
            return false;
        }

        FileNode* existing = file_system->find_child(
            destParent, src->get_name()
            );

        // 没有同名项时，后续沿用原有复制/移动逻辑。
        if (existing == nullptr)
            return true;

        // 已存在同名项：不能未经授权直接替换或合并。
        if (!can_access_node(existing))
        {
            QMessageBox::warning(
                this,
                "粘贴失败",
                "目标位置存在受隐私保护的同名项目。\n"
                "请先验证隐私密码，不能直接覆盖或合并。"
                );
            return false;
        }

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
    if (back_stack.empty())
    {
        ui->statusbar->showMessage(
            "已经没有可以后退的记录了。", 2000);
        return;
    }

    const std::string targetPath = back_stack.top();

    FileNode* target =
        file_system->find_by_path(targetPath);

    // 预先检查，不允许失败时破坏历史栈
    if (target == nullptr ||
        !target->is_directory() ||
        !can_access_node(target))
    {
        QMessageBox::warning(
            this,
            "无法后退",
            "历史目录不存在或当前没有访问权限。"
            );
        return;
    }

    const std::string currentPath =
        file_system->get_path(
            file_system->get_current_directory()
            );

    if (!file_system->change_directory_by_path(targetPath))
        return;

    back_stack.pop();
    forward_stack.push(currentPath);

    // 统一更新普通目录界面
    navigate_to(targetPath);
}




void MainWindow::on_btn_forward_clicked()
{
    if (forward_stack.empty())
    {
        ui->statusbar->showMessage(
            "没有可以前进的记录。", 2000);
        return;
    }

    const std::string targetPath = forward_stack.top();

    FileNode* target =
        file_system->find_by_path(targetPath);

    if (target == nullptr ||
        !target->is_directory() ||
        !can_access_node(target))
    {
        QMessageBox::warning(
            this,
            "无法前进",
            "历史目录不存在或当前没有访问权限。"
            );
        return;
    }

    const std::string currentPath =
        file_system->get_path(
            file_system->get_current_directory()
            );

    if (!file_system->change_directory_by_path(targetPath))
        return;

    forward_stack.pop();
    back_stack.push(currentPath);

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
    run_smart_search();
}

// 统一的跳转逻辑
void MainWindow::navigate_to(
    const std::string& path,
    bool refreshTree,
    bool recordHistory)
{
    FileNode* targetNode = file_system->find_by_path(path);

    if (targetNode == nullptr ||
        !targetNode->is_directory() ||
        !can_access_node(targetNode))
    {
        QMessageBox::warning(
            this,
            "无法访问",
            "目标目录不存在或当前没有访问权限。"
            );

        FileNode* current =
            file_system->get_current_directory();

        if (current != nullptr)
        {
            ui->lineEdit_path->setText(
                toDisplayPath(QString::fromStdString(
                    file_system->get_path(current)
                    ))
                );
        }

        return;
    }

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

    // 已经成功进入普通目录，统一退出特殊视图
    recycle_mode = false;
    privacy_overview_mode = false;

    // 显示普通文件操作
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


    const std::string currentPath =
        file_system->get_path(current);

    // 只有指定为有效访问时，才更新最近访问
    if (recordHistory && currentPath != "/")
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

    // 回收站里的节点不属于活动文件树，不能把它当普通目录进入
    if (recycle_mode)
        return;

    // 从隐私文件总览定位到原始文件夹
    if (privacy_overview_mode)
    {
        if (!can_access_node(targetNode))
            return;

        FileNode* destination = targetNode;

        // 文件不能作为目录进入，定位到所在文件夹
        if (!targetNode->is_directory())
            destination = targetNode->get_parent();

        if (destination == nullptr)
            return;

        std::string targetPath =
            file_system->get_path(destination);

        std::string currentPath =
            file_system->get_path(
                file_system->get_current_directory()
                );

        // 返回普通目录操作状态
        privacy_overview_mode = false;
        recycle_mode = false;

        ui->btn_new->show();
        ui->btn_cut->show();
        ui->btn_copy->show();
        ui->btn_paste->show();
        ui->btn_rename->show();
        ui->btn_delete->show();

        if (targetPath != currentPath)
        {
            back_stack.push(currentPath);

            while (!forward_stack.empty())
                forward_stack.pop();
        }

        navigate_to(targetPath);

        return;
    }

    if (!can_access_node(targetNode))
    {
        QMessageBox::warning(
            this,
            "访问受限",
            "请先显示隐私文件并验证密码。"
            );
        return;
    }

    if (search_mode_)
    {
        FileNode* destination = targetNode->is_directory() ? targetNode : targetNode->get_parent();
        std::string previous = file_system->get_path(file_system->get_current_directory());
        if (previous != file_system->get_path(destination)) { back_stack.push(previous); while (!forward_stack.empty()) forward_stack.pop(); }
        navigate_to(file_system->get_path(destination));
        for (int i = 0; i < ui->list_files->count(); ++i) if (ui->list_files->item(i)->data(Qt::UserRole).value<FileNode*>() == targetNode) ui->list_files->setCurrentRow(i);
        return;
    }

    if (!targetNode->is_directory())
    {
        open_text_editor(targetNode);
        return;
    }

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

    int itemType =
        item->data(
                0,
                Qt::UserRole + 1
                ).toInt();


    // ==================================================
    // 隐私文件
    // ==================================================
    if (itemType == 2)
    {
        // 尚未获得隐私权限时，验证密码
        if (!show_privacy_mode || !has_privacy_access)
        {
            bool ok = false;

            QString password = QInputDialog::getText(
                this,
                "隐私文件",
                "请输入隐私密码：",
                QLineEdit::Password,
                "",
                &ok
                );

            if (!ok)
                return;

            if (!verify_privacy_password(password))
            {
                QMessageBox::warning(
                    this,
                    "验证失败",
                    "隐私密码错误！"
                    );
                return;
            }

            if (!unlock_encrypted_content(password))
                return;

            show_privacy_mode = true;
            has_privacy_access = true;
        }

        privacy_overview_mode = true;
        recycle_mode = false;

        // 总览暂时作为只读导航视图，
        // 避免普通文件操作按钮误操作
        ui->btn_new->hide();
        ui->btn_cut->hide();
        ui->btn_copy->hide();
        ui->btn_paste->hide();
        ui->btn_rename->hide();
        ui->btn_delete->hide();

        ui->btn_restore->hide();
        ui->btn_permanent_delete->hide();
        ui->btn_clear_recycle->hide();

        refresh_file_list();

        ui->lineEdit_path->setText("隐私文件");
        ui->statusbar->showMessage(
            "已进入隐私文件总览。",
            3000
            );

        return;
    }


    // 普通目录节点先验证权限。
    // 不能在确认可访问之前退出回收站或隐私总览。
    if (itemType != 1 && itemType != 2)
    {
        FileNode* candidate =
            item->data(0, Qt::UserRole).value<FileNode*>();

        if (candidate == nullptr ||
            !candidate->is_directory() ||
            !can_access_node(candidate))
        {
            QMessageBox::warning(
                this,
                "无法访问",
                "目标目录不存在或当前没有访问权限。"
                );

            return;
        }
    }


    // 点击其他目录项时，退出隐私总览
    privacy_overview_mode = false;


    // ==================================================
    // 回收站
    // ==================================================
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


void MainWindow::save_node_recursive(FileNode* node, std::ostream& out) {
    if (node == nullptr) return;

    std::string path = file_system->get_path(node);

    std::string contentToSave;
    std::string contentEncoding = "P1";

    if (!node->is_directory())
    {
        // 自身上锁或祖先文件夹上锁，都需要保护
        if (is_privacy_protected(node))
        {
            if (!node->get_sealed_content().empty())
            {
                contentEncoding = "E1";
                contentToSave = node->get_sealed_content();
            }
            else
            {
                // 仅为还未输入密码的旧 X1 存档保留原样兼容。
                contentEncoding = "X1";
                contentToSave = encryptContent(node->get_content());
            }
        }
        else
        {
            contentEncoding = "P1";
            contentToSave = encodePlainContent(
                node->get_content()
                );
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
        << contentToSave << "|"
        << (node->is_locked() ? "1" : "0") << "|"
        << contentEncoding << "|\n";

    // 递归遍历子节点
    FileNode* child = node->get_first_child();
    while (child != nullptr) {
        save_node_recursive(child, out);
        child = child->get_next_sibling();
    }
}


bool MainWindow::save_data()
{
    // 存档缺损时禁止自动把“不完整的内存树”写回原始文件。
    if (data_load_blocked_)
    {
        QMessageBox::critical(
            this, "禁止覆盖异常存档",
            "上次载入存档时发现错误，因此自动保存已被禁止。\n"
            "原存档仍在磁盘中，请先备份并修复。\n\n" +
            data_load_error_);
        return false;
    }

    // 密文更新成功后才开始生成磁盘存档；失败不写原文件。
    if (!prepare_encrypted_content_for_save())
        return false;

    const QString filename =
        "data_" + current_user + ".txt";

    // 第一步：先完整生成存档内容
    // 此时不打开、不修改原始 TXT 文件
    std::ostringstream out;

    // 新版存档标记：存在该标记时必须验证结尾 CHK1。
    out << "FMHDR|1|\n";
    // 盐公开存储，真正密钥不落盘。原有存档不强制包含 KDF1。
    if (!crypto_salt_.empty())
        out << "KDF1|" << encodePlainContent(crypto_salt_)
            << "|" << crypto_iterations_ << "|\n";

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
                    std::string contentToSave;
                    std::string contentEncoding = "P1";

                    if (!node->is_directory())
                    {
                        if (file_system->is_recycle_privacy_protected(node))
                        {
                            if (!node->get_sealed_content().empty())
                            {
                                contentEncoding = "E1";
                                contentToSave = node->get_sealed_content();
                            }
                            else
                            {
                                contentEncoding = "X1"; // 旧版未迁移记录
                                contentToSave = encryptContent(node->get_content());
                            }
                        }
                        else
                        {
                            contentEncoding = "P1";
                            contentToSave = encodePlainContent(
                                node->get_content()
                                );
                        }
                    }

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
                    << contentToSave << "|"
                    << (node->is_locked() ? "1" : "0") << "|"
                    << ((depth == 0 &&
                         file_system->get_recycle_inherited_privacy(node))
                            ? "1" : "0") << "|"
                    << contentEncoding << "|\n";

                for (FileNode* child = node->get_first_child();
                     child != nullptr; child = child->get_next_sibling())
                    writeRecycle(child, depth + 1);
            };
            writeRecycle(root, 0);
        });

    // 保存最近访问记录
    for (const QString& path : std::as_const(history_list))
    {
        if (path == "/") continue;
        out << "H|"
            << path.toStdString()
            << "\n";
    }

    // 保存下一个历史替换槽位
    out << "HIDX|"
        << history_replace_index
        << "\n";


    // 检查内存序列化是否成功
    if (!out.good())
    {
        QMessageBox::critical(
            this,
            "保存失败",
            "生成存档内容失败，原数据文件未被修改。"
            );
        return false;
    }

    // 取出完整的存档字节
    std::string serialized = out.str();

    // 校验码只用于检测数据意外损坏（不是防恶意篡改的 MAC）。
    // 对校验行前的字节逐字节计算 SHA-256，旧版无校验码存档仍可读取。
    const QByteArray body(serialized.data(),
                          static_cast<qsizetype>(serialized.size()));
    const QByteArray digest = QCryptographicHash::hash(
        body, QCryptographicHash::Sha256).toHex();
    serialized += "CHK1|" + digest.toStdString() + "|\n";

    QSaveFile file(filename);

    // 禁止在无法使用原子替换时退化为直接写原文件
    file.setDirectWriteFallback(false);

    // 第二步：打开安全保存文件
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(
            this,
            "保存失败",
            "无法打开安全存档文件：\n" +
                file.errorString()
            );
        return false;
    }

    // 第三步：写入完整的存档
    const qint64 expected =
        static_cast<qint64>(serialized.size());

    const qint64 written = file.write(
        serialized.data(),
        expected
        );

    if (written != expected)
    {
        const QString reason = file.errorString();

        file.cancelWriting();

        QMessageBox::critical(
            this,
            "保存失败",
            "存档数据写入不完整。\n" + reason +
                "\n原数据文件未被本次保存替换。"
            );
        return false;
    }

    // 第四步：提交，成功后才替换原存档
    if (!file.commit())
    {
        QMessageBox::critical(
            this,
            "保存失败",
            "安全提交存档失败：\n" +
                file.errorString() +
                "\n请检查磁盘空间及文件权限。"
            );
        return false;
    }

    ui->statusbar->showMessage(
        "数据保存成功！",
        2000
        );

    return true;

}


void MainWindow::load_data()
{
    const QString filename = "data_" + current_user + ".txt";

    auto loadingError = [this, &filename](const QString& reason)
    {
        data_load_blocked_ = true;
        data_load_error_ = reason;
        QMessageBox::critical(
            this,
            "存档加载失败，已启用保护",
            "检测到存档不完整或格式错误：\n" + reason +
            "\n\n原存档：" + filename +
            "\n程序不会自动覆盖它。请先复制备份，修复后再重新打开程序。"
        );
    };

    // 首次运行确实没有存档，允许初始化空文件系统。
    // 已存在却打不开的文件不能误判为“首次运行”。
    if (!QFileInfo::exists(filename))
        return;

    QFile archive(filename);
    if (!archive.open(QIODevice::ReadOnly))
    {
        loadingError("无法读取已有存档：" + archive.errorString());
        return;
    }

    const QByteArray bytes = archive.readAll();
    const auto readError = archive.error();
    const QString readErrorMessage = archive.errorString();
    archive.close();

    if (readError != QFileDevice::NoError)
    {
        loadingError("存档读取中断：" + readErrorMessage);
        return;
    }
    if (bytes.isEmpty())
    {
        loadingError("存档文件为空，无法确认是否丢失了数据。");
        return;
    }

    // 逐行切分原始字节，保留每行在文件中的偏移，以验证校验码。
    std::vector<QByteArray> lines;
    std::vector<qsizetype> offsets;
    qsizetype pos = 0;
    while (pos < bytes.size())
    {
        const qsizetype newline = bytes.indexOf('\n', pos);
        const qsizetype end = (newline < 0 ? bytes.size() : newline);
        QByteArray line = bytes.mid(pos, end - pos);
        if (line.endsWith('\r'))
            line.chop(1);
        lines.push_back(line);
        offsets.push_back(pos);
        pos = (newline < 0 ? bytes.size() : newline + 1);
    }

    if (lines.empty())
    {
        loadingError("没有任何可解析的存档记录。");
        return;
    }

    // 新版存档末尾有 CHK1|<64位十六进制SHA256>| 校验行。
    // 旧存档没有 CHK1 时仍接受，但会做完整结构校验。
    bool hasChecksum = false;
    if (lines.back().startsWith("CHK1|"))
    {
        hasChecksum = true;
        const QByteArray checksumRow = lines.back();
        if (!checksumRow.endsWith('|') ||
            checksumRow.size() != 5 + 64 + 1 ||
            !bytes.endsWith('\n'))
        {
            loadingError("存档校验行格式错误或文件尾部被截断。");
            return;
        }
        const QByteArray expected = checksumRow.mid(5, 64);
        const QByteArray computed = QCryptographicHash::hash(
            bytes.left(offsets.back()), QCryptographicHash::Sha256).toHex();
        if (expected != computed)
        {
            loadingError("SHA-256 校验失败：存档可能已损坏或被修改。");
            return;
        }
        lines.pop_back();
    }

    const bool hasNewHeader = !lines.empty() && lines.front() == "FMHDR|1|";
    if (hasNewHeader)
    {
        if (!hasChecksum)
        {
            loadingError("新版存档缺少 CHK1 校验行，可能被截断。");
            return;
        }
        lines.erase(lines.begin());
    }
    // KDF1 必须紧跟 FMHDR；旧存档可不带 KDF1。
    std::string stagedCryptoSalt;
    int stagedCryptoIterations = PrivacyCrypto::kIterations;
    if (!lines.empty() && lines.front().startsWith("KDF1|"))
    {
        const QList<QByteArray> kdf = lines.front().split('|');
        std::string decodedSalt;
        bool roundsOk = false;
        const int rounds = (kdf.size() == 4)
            ? kdf[2].toInt(&roundsOk) : 0;
        if (kdf.size() != 4 || !kdf[3].isEmpty() || !roundsOk ||
            rounds < 100000 || rounds > 10000000 ||
            !decodeBase64Content(kdf[1].toStdString(), decodedSalt) ||
            decodedSalt.size() != PrivacyCrypto::kSaltSize)
        {
            loadingError("KDF1 密钥派生参数损坏。请恢复备份。");
            return;
        }
        stagedCryptoSalt = std::move(decodedSalt);
        stagedCryptoIterations = rounds;
        lines.erase(lines.begin());
    }
    if (lines.empty())
    {
        loadingError("存档中没有文件系统数据。");
        return;
    }

    // 只在临时 FileSystem 里恢复节点。任何一行出错时，原有
    // file_system、历史记录均保持原样，且阻止保存覆盖源文件。
    std::unique_ptr<FileSystem> staging(new FileSystem());
    QStringList stagedHistory;
    int stagedHistoryIndex = 0;
    std::vector<FileNode*> recycleParents;
    bool rootSeen = false;
    bool indexSeen = false;
    bool damaged = false;
    QString reason;

    enum class LoadPhase { Active, Recycle, History };
    LoadPhase phase = LoadPhase::Active;

    const auto parseNumber = [](const std::string& s, long long& result) -> bool
    {
        if (s.empty()) return false;
        try {
            std::size_t count = 0;
            result = std::stoll(s, &count, 10);
            return count == s.size();
        } catch (...) {
            return false;
        }
    };

    const auto validFlag = [](const std::string& s) -> bool
    {
        return s == "0" || s == "1";
    };

    const auto splitFields = [](const QByteArray& row)
    {
        std::vector<std::string> fields;
        const std::string s = row.toStdString();
        std::size_t start = 0;
        while (true) {
            const std::size_t sep = s.find('|', start);
            if (sep == std::string::npos) {
                fields.push_back(s.substr(start));
                break;
            }
            fields.push_back(s.substr(start, sep - start));
            start = sep + 1;
        }
        // D/F/R/RC 记录以竖线结尾；仅去除该格式终止符。
        if (!s.empty() && s.back() == '|' && !fields.empty())
            fields.pop_back();
        return fields;
    };

    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const auto f = splitFields(lines[i]);
        const QString location = QString("第 %1 行：").arg(i + 1);
        auto fail = [&](const QString& message) {
            damaged = true;
            reason = location + message;
        };

        if (f.empty() || f[0].empty()) {
            fail("空白或不完整的存档记录。");
            break;
        }
        const std::string& type = f[0];
        if (indexSeen) {
            fail("HIDX 必须是最后一条普通记录。");
            break;
        }

        if (type == "H")
        {
            phase = LoadPhase::History;
            if (f.size() != 2 || f[1].empty() || f[1][0] != '/' ||
                stagedHistory.size() >= 3)
            {
                fail("访问历史记录格式错误。");
                break;
            }
            stagedHistory.append(QString::fromStdString(f[1]));
            continue;
        }

        if (type == "HIDX")
        {
            phase = LoadPhase::History;
            long long value = -1;
            if (f.size() != 2 || !parseNumber(f[1], value) ||
                value < 0 || value > 2)
            {
                fail("历史替换槽位 HIDX 格式错误。");
                break;
            }
            stagedHistoryIndex = static_cast<int>(value);
            indexSeen = true;
            continue;
        }

        if (type == "R" || type == "RC")
        {
            if (phase == LoadPhase::History || !rootSeen) {
                fail("回收站记录顺序不正确。"); break;
            }
            phase = LoadPhase::Recycle;
            const bool nested = (type == "RC");
            const std::size_t base = nested ? 2 : 1;
            // R: 13 个字段，RC: 14 个字段；兼容早期无锁标志/编码列的 R 记录。
            if ((!nested && (f.size() < 10 || f.size() > 13)) ||
                (nested && f.size() != 14))
            {
                fail("回收站记录字段数错误。"); break;
            }
            const auto field = [&](std::size_t k) -> std::string {
                return k < f.size() ? f[k] : std::string();
            };
            long long depth = 0;
            if (nested && (!parseNumber(f[1], depth) || depth < 1 || depth > 1000 ||
                static_cast<std::size_t>(depth - 1) >= recycleParents.size() ||
                recycleParents[static_cast<std::size_t>(depth - 1)] == nullptr ||
                !recycleParents[static_cast<std::size_t>(depth - 1)]->is_directory()))
            {
                fail("回收站子节点深度或父目录无效。"); break;
            }
            const std::string kind = field(base);
            const std::string originalPath = field(base + 1);
            const std::string name = field(base + 2);
            const std::string created = field(base + 3);
            const std::string modified = field(base + 4);
            const std::string pinned = field(base + 5);
            const std::string orderText = field(base + 6);
            const std::string sizeText = field(base + 7);
            const std::string contentText = field(base + 8);
            const std::string locked = field(base + 9);
            const std::string inherited = field(base + 10);
            const std::string encoding = field(base + 11);
            long long size = -1, order = -1;
            if ((kind != "D" && kind != "F") || name.empty() ||
                name == "." || name == ".." ||
                name.find('/') != std::string::npos ||
                name.find('\\') != std::string::npos ||
                !validFlag(pinned) ||
                (!locked.empty() && !validFlag(locked)) ||
                (!inherited.empty() && !validFlag(inherited)) ||
                !parseNumber(sizeText, size) || size < 0 ||
                !parseNumber(orderText, order) || order < -1 ||
                (nested && !originalPath.empty()) ||
                (!nested && (originalPath.empty() || originalPath[0] != '/')))
            {
                fail("回收站节点属性、文件名或路径不合法。"); break;
            }
            // 只有普通文件包含实际字节；回收站旧格式未带编码时视为原文。
            std::string content;
            std::string sealedContent;
            if (kind == "F") {
                if (encoding == "P1") {
                    if (!decodeBase64Content(contentText, content)) {
                        fail("回收站普通文件 Base64 已损坏。"); break;
                    }
                } else if (encoding == "X1") {
                    std::string checked;
                    if (!decodeBase64Content(contentText, checked)) {
                        fail("回收站隐私文件 Base64 已损坏。"); break;
                    }
                    content = decryptContent(contentText);
                } else if (encoding == "E1") {
                    std::string raw;
                    if (stagedCryptoSalt.empty() ||
                        !decodeBase64Content(contentText, raw) ||
                        raw.size() < PrivacyCrypto::kNonceSize + PrivacyCrypto::kTagSize) {
                        fail("回收站 E1 密文封装或 KDF1 数据损坏。"); break;
                    }
                    // 不需要密码即可加载目录树；内容在授权后验证 GCM 标签。
                    sealedContent = contentText;
                } else if (encoding.empty()) {
                    content = contentText;
                } else {
                    fail("回收站含未知的内容编码。"); break;
                }
                if (encoding != "E1" &&
                    static_cast<unsigned long long>(size) != content.size()) {
                    fail("回收站文件大小与解码内容不一致。"); break;
                }
            } else if (!contentText.empty() ||
                       (!encoding.empty() && encoding != "P1")) {
                fail("回收站目录含不合法的文件内容。"); break;
            }
            FileNode* parent = nested
                ? recycleParents[static_cast<std::size_t>(depth - 1)] : nullptr;
            FileNode* node = staging->create_recycle_node_for_load(
                parent, name, kind == "D", originalPath, inherited == "1");
            if (node == nullptr ||
                !staging->restore_recycle_data(node, content, size, created, modified) ||
                !staging->restore_pin_state(node, pinned == "1", order) ||
                !staging->restore_recycle_lock_state(node, locked == "1") ||
                (!sealedContent.empty() &&
                 !staging->set_sealed_content(node, sealedContent)))
            {
                fail("回收站节点恢复失败。"); break;
            }
            if (!nested) recycleParents.clear();
            if (recycleParents.size() <= static_cast<std::size_t>(depth))
                recycleParents.resize(static_cast<std::size_t>(depth) + 1, nullptr);
            recycleParents[static_cast<std::size_t>(depth)] = node;
            recycleParents.resize(static_cast<std::size_t>(depth) + 1);
            continue;
        }

        if (type == "D" || type == "F")
        {
            if (phase != LoadPhase::Active) {
                fail("活动文件树记录出现在回收站或历史记录之后。"); break;
            }
            // 兼容旧版没有锁标志、编码列的行；新版恰好 10 列。
            if (f.size() < 8 || f.size() > 10) {
                fail("活动文件树记录字段数错误。"); break;
            }
            const auto field = [&](std::size_t k) -> std::string {
                return k < f.size() ? f[k] : std::string();
            };
            const std::string path = field(1);
            const std::string created = field(2);
            const std::string modified = field(3);
            const std::string pinned = field(4);
            const std::string orderText = field(5);
            const std::string sizeText = field(6);
            const std::string contentText = field(7);
            const std::string locked = field(8);
            const std::string encoding = field(9);
            long long size = -1, order = -1;
            if (path.empty() || path.front() != '/' || !validFlag(pinned) ||
                (!locked.empty() && !validFlag(locked)) ||
                !parseNumber(sizeText, size) || size < 0 ||
                !parseNumber(orderText, order) || order < -1)
            {
                fail("文件路径或节点属性格式错误。"); break;
            }
            if (path == "/") {
                // KDF1 行在解析前已移除，因此根目录仍必须是第 1 个活动节点。
                if (rootSeen || i != 0 || type != "D") {
                    fail("根目录记录重复或位置错误。"); break;
                }
                rootSeen = true;
                continue;
            }
            if (!rootSeen) {
                fail("缺少文件树根目录记录。"); break;
            }
            const std::size_t slash = path.find_last_of('/');
            const std::string parentPath = slash == 0 ? "/" : path.substr(0, slash);
            const std::string name = path.substr(slash + 1);
            if (name.empty() || name == "." || name == ".." ||
                name.find('\\') != std::string::npos ||
                name.find('/') != std::string::npos ||
                path.find("//") != std::string::npos)
            {
                fail("文件名或路径结构不合法。"); break;
            }
            FileNode* parent = staging->find_by_path(parentPath);
            if (parent == nullptr || !parent->is_directory()) {
                fail("文件的父目录不存在，可能有记录丢失或顺序被破坏。"); break;
            }
            FileNode* node = nullptr;
            std::string sealedContent;
            if (type == "D") {
                if (!contentText.empty() ||
                    (!encoding.empty() && encoding != "P1")) {
                    fail("目录记录包含非法内容。"); break;
                }
                node = staging->create_folder(parent, name);
            } else {
                std::string content;
                if (encoding == "P1") {
                    if (!decodeBase64Content(contentText, content)) {
                        fail("普通文件内容 Base64 已损坏。"); break;
                    }
                } else if (encoding == "X1") {
                    std::string checked;
                    if (!decodeBase64Content(contentText, checked)) {
                        fail("隐私文件内容 Base64 已损坏。"); break;
                    }
                    content = decryptContent(contentText);
                } else if (encoding == "E1") {
                    std::string raw;
                    if (stagedCryptoSalt.empty() ||
                        !decodeBase64Content(contentText, raw) ||
                        raw.size() < PrivacyCrypto::kNonceSize + PrivacyCrypto::kTagSize) {
                        fail("文件 E1 密文封装或 KDF1 数据损坏。"); break;
                    }
                    sealedContent = contentText;
                } else if (encoding.empty()) {
                    // 兼容旧版：主动锁定的文件是 XOR+Base64，其余为明文。
                    content = locked == "1" ? decryptContent(contentText) : contentText;
                } else {
                    fail("文件使用了未知编码格式。"); break;
                }
                if (encoding != "E1" &&
                    static_cast<unsigned long long>(size) != content.size()) {
                    fail("文件大小与解码内容不一致。"); break;
                }
                node = staging->create_file(parent, name, content);
            }
            if (node == nullptr ||
                !staging->set_metadata(node, size, created, modified) ||
                !staging->restore_pin_state(node, pinned == "1", order) ||
                !staging->restore_lock_state(node, locked == "1") ||
                (!sealedContent.empty() &&
                 !staging->set_sealed_content(node, sealedContent)))
            {
                fail("无法恢复文件或文件夹，可能存在重名或非法名称。"); break;
            }
            continue;
        }

        fail("遇到未知的存档记录类型：" + QString::fromStdString(type));
        break;
    }

    if (!damaged && !rootSeen) {
        damaged = true;
        reason = "未找到文件系统根目录记录。";
    }
    // 新存档的结尾必须包含 HIDX；这可防止某些结构性截断。
    if (!damaged && hasChecksum && !indexSeen) {
        damaged = true;
        reason = "新版存档缺少 HIDX 历史记录结尾。";
    }
    if (damaged) {
        loadingError(reason);
        return;
    }

    // 全部记录验证及构建成功，此时才正式切换文件系统。
    FileSystem* oldSystem = file_system;
    file_system = staging.release();
    delete oldSystem;
    history_list = stagedHistory;
    history_list.removeAll("/");
    history_replace_index = stagedHistoryIndex;
    crypto_salt_ = stagedCryptoSalt;
    crypto_iterations_ = stagedCryptoIterations;
    data_load_blocked_ = false;
    data_load_error_.clear();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!save_data())
    {
        QMessageBox::StandardButton choice =
            QMessageBox::warning(
                this,
                "退出前保存失败",
                "当前数据尚未成功写入磁盘。\n"
                "直接退出可能丢失本次修改。\n\n"
                "是否仍然退出？",
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No
                );

        if (choice != QMessageBox::Yes)
        {
            is_logout_action = false;
            qApp->setProperty("is_logout_requested", false);

            event->ignore();
            return;
        }
    }

    if (is_logout_action) {
        // === 2. 如果是注销，必须彻底清理所有旧内存，防止新用户登录后指针悬空崩溃 ===
        discard_privacy_key();
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
    else if (is_privacy_protected(node))
    {
        tooltip += "\n状态：受上级文件夹保护";
    }

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

    // 检查回收站项目及恢复目标目录的访问权限
    if (!can_access_node(parent))
    {
        QMessageBox::warning(
            this,
            "恢复失败",
            "原位置所在文件夹受到隐私保护，"
            "请先显示隐私文件。"
            );
        return;
    }

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
    // 回收站的 E1 原本可能继承父目录保护。如果原目录后来解除保护，
    // 恢复时给整棵子树补上锁标志，防止密文被当成普通文件覆盖。
    bool anySealed = false;
    std::function<void(FileNode*)> scanSealed = [&](FileNode* current) {
        if (!current->get_sealed_content().empty()) anySealed = true;
        for (FileNode* child = current->get_first_child(); child;
             child = child->get_next_sibling())
            scanSealed(child);
    };
    scanSealed(node);
    if (anySealed && !is_privacy_protected(node))
        file_system->set_locked(node, true);

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
    save_data();
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
    save_data();
    refresh_file_list();
    refresh_tree();
}


void MainWindow::on_btn_undo_clicked()
{
    if (recycle_mode || privacy_overview_mode)
        return;

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


    // 取出栈顶操作，但先不要弹出
    Operation op = undo_stack.top();

    // 统一提示函数
    auto showUndoDenied = [this](const QString& message)
    {
        QMessageBox::warning(
            this,
            "无法撤销",
            message
            );
    };

    // 先确认所有会被使用的指针仍然有效。
    // 验证完成前，不得调用 node->get_parent() 等函数。
    bool nodeValid = false;
    bool parentValid = false;

    switch (op.type)
    {
    case OperationType::DELETE:
        nodeValid =
            file_system->contains_recycle_root(op.node);
        parentValid =
            file_system->contains_active_node(op.old_parent);
        break;

    case OperationType::MOVE:
        nodeValid =
            file_system->contains_active_node(op.node);
        parentValid =
            file_system->contains_active_node(op.old_parent);
        break;

    case OperationType::CREATE:
    case OperationType::RENAME:
    case OperationType::COPY:
    case OperationType::RESTORE:
        nodeValid =
            file_system->contains_active_node(op.node);
        parentValid = true;
        break;

    default:
        break;
    }

    // 无效的撤销记录不应继续留在栈顶
    if (!nodeValid || !parentValid)
    {
        undo_stack.pop();

        showUndoDenied(
            "撤销记录引用的文件或目录已经失效，"
            "该记录已从撤销历史中移除。"
            );
        return;
    }

    // 以下开始，相关节点已经完成有效性检查。
    switch (op.type)
    {
    case OperationType::DELETE:
        // 从回收站恢复整个项目，不要求查看项目内部。
        // 但恢复目标目录如果受到保护，必须先授权。
        if (!can_access_node(op.old_parent))
        {
            showUndoDenied(
                "原目录受到隐私保护，"
                "请先显示隐私文件并验证密码。"
                );
            return;
        }
        break;

    case OperationType::RENAME:
        // 重命名直接修改活动节点的属性。
        if (!can_access_node(op.node))
        {
            showUndoDenied(
                "该文件受到隐私保护，"
                "请先验证隐私密码再撤销重命名。"
                );
            return;
        }
        break;

    case OperationType::MOVE:
        // 移回目标目录，需要拥有目标目录的访问权限。
        if (!can_access_node(op.old_parent))
        {
            showUndoDenied(
                "原目标目录受到隐私保护，"
                "请先验证隐私密码。"
                );
            return;
        }

        // 节点本身没有主动上锁、只是从当前父目录
        // 继承保护时，不允许未经授权直接移出。
        // 否则移入未锁定目录后，内容可能暴露。
        if (!can_access_node(op.node) &&
            !op.node->is_locked())
        {
            showUndoDenied(
                "移动的项目正在继承上级目录的隐私保护。\n"
                "请先验证隐私密码再撤销移动。"
                );
            return;
        }
        break;

    case OperationType::CREATE:
    case OperationType::COPY:
    case OperationType::RESTORE:
        // 相当于把整个箱子移入回收站，
        // 不需要打开箱子查看内容。
        break;

    default:
        showUndoDenied("不支持该撤销类型。");
        return;
    }

    // 安全检查通过后，再执行底层撤销
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
            oldCurrentNode->is_directory() &&
            can_access_node(oldCurrentNode))
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
        PrivacyCrypto::wipe(crypto_session_key_);
        crypto_salt_.clear();
        crypto_iterations_ = PrivacyCrypto::kIterations;

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
        privacy_overview_mode = false;
        show_privacy_mode = false;
        has_privacy_access = false;

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
    if (recycle_mode)
        return;

    QListWidgetItem* item =
        ui->list_files->currentItem();

    if (item == nullptr)
        return;

    FileNode* node =
        item->data(Qt::UserRole).value<FileNode*>();

    if (node == nullptr || !can_access_node(node))
        return;

    bool currentlyLocked = file_system->is_locked(node);

    // 解锁必须在已验证隐私密码的显示模式下进行
    if (currentlyLocked)
    {
        if (!show_privacy_mode || !has_privacy_access)
        {
            QMessageBox::warning(
                this,
                "访问受限",
                "请先通过“显示 → 隐私文件”验证密码。"
                );
            return;
        }
    }

    // 新上锁时需要真正 AES 密钥，否则不允许继续生成旧版 XOR 密文。
    if (!currentlyLocked && crypto_session_key_.empty())
    {
        QMessageBox::information(this, "需要隐私密码",
            "首次上锁前需要验证隐私密码以启用 AES-256-GCM。\n"
            "请先通过“显示 → 隐私文件”验证密码，再重新执行上锁。");
        return;
    }

    // 修改锁定状态
    if (!file_system->set_locked(node, !currentlyLocked))
    {
        QMessageBox::warning(
            this,
            "操作失败",
            "无法修改文件锁定状态。"
            );
        return;
    }

    // 立即保存并同步两个界面
    save_data();
    refresh_tree();
    refresh_file_list();

    ui->statusbar->showMessage(
        currentlyLocked ? "已解锁！" : "已上锁，项目已按隐私显示规则处理。",
        3000
        );
}



void MainWindow::on_action_show_privacy_triggered()
{
    // 当前已经显示隐私文件：关闭显示
    if (show_privacy_mode)
    {
        // 必须在撤销授权前将修改过的明文重新认证加密。
        if (!save_data())
        {
            QMessageBox::warning(this, "无法隐藏隐私内容",
                "安全保存失败。为避免丢失修改，仍保留当前授权，请修复保存错误后重试。");
            return;
        }
        discard_privacy_key();
        show_privacy_mode = false;
        has_privacy_access = false;
        privacy_overview_mode = false;

        // 退出隐私总览后，恢复普通操作按钮
        ui->btn_new->show();
        ui->btn_cut->show();
        ui->btn_copy->show();
        ui->btn_paste->show();
        ui->btn_rename->show();
        ui->btn_delete->show();

        // 不再保留受保护文件的剪贴板引用
        if (clipboard_node != nullptr &&
            is_privacy_protected(clipboard_node))
        {
            clipboard_node = nullptr;
            clipboard_mode = ClipboardMode::None;
        }

        // 如果目前身处锁定文件夹内部，
        // 收回授权后自动返回根目录
        FileNode* current =
            file_system->get_current_directory();

        if (current != nullptr &&
            is_privacy_protected(current))
        {
            file_system->change_directory_by_path("/");
        }

        // 清理导航栈，避免保留之前的隐私导航路径
        while (!back_stack.empty())
            back_stack.pop();

        while (!forward_stack.empty())
            forward_stack.pop();

        refresh_tree();
        refresh_file_list();

        ui->lineEdit_path->setText(
            toDisplayPath(QString::fromStdString(
                file_system->get_path(
                    file_system->get_current_directory()
                    )
                ))
            );

        ui->statusbar->showMessage(
            "已隐藏隐私文件，隐私访问授权已撤销。",
            3000
            );

        return;
    }

    // 从隐藏变为显示，需要验证密码
    bool ok = false;

    QString password = QInputDialog::getText(
        this,
        "显示隐私文件",
        "请输入隐私密码：",
        QLineEdit::Password,
        "",
        &ok
        );

    if (!ok)
        return;

    if (!verify_privacy_password(password))
    {
        QMessageBox::warning(
            this,
            "验证失败",
            "隐私密码错误！"
            );
        return;
    }

    // 不覆盖原密文：先用输入密码解开所有 E1 项并校验认证标签。
    if (!unlock_encrypted_content(password))
        return;

    // 验证通过，开放本次会话的隐私访问
    show_privacy_mode = true;
    has_privacy_access = true;

    refresh_tree();
    refresh_file_list();

    ui->statusbar->showMessage(
        "隐私文件已显示，现在可以正常访问。",
        3000
        );
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

// =========== AES-256-GCM / PBKDF2 密钥与内容保护 ===========
void MainWindow::discard_privacy_key()
{
    if (file_system)
        file_system->wipe_sealed_plaintext();
    PrivacyCrypto::wipe(crypto_session_key_);
}

bool MainWindow::unlock_encrypted_content(const QString& password)
{
    // KDF salt 可公开保存在 KDF1 中；只在密码通过 users.txt 验证后派生密钥。
    std::string salt = crypto_salt_;
    if (salt.empty() && !PrivacyCrypto::randomBytes(salt, PrivacyCrypto::kSaltSize))
    {
        QMessageBox::critical(this, "加密失败", "无法生成安全随机盐。请检查 OpenSSL。 ");
        return false;
    }
    QByteArray utf8 = password.toUtf8();
    std::string rawPassword(utf8.constData(), static_cast<std::size_t>(utf8.size()));
    std::string key;
    const bool derived = PrivacyCrypto::deriveKey(
        rawPassword, salt, crypto_iterations_, key);
    PrivacyCrypto::wipe(rawPassword);
    utf8.fill('\0');
    if (!derived) {
        QMessageBox::critical(this, "加密失败", "PBKDF2 派生密钥失败。");
        return false;
    }

    // 两阶段：全部解密及认证成功后，才修改任何 FileNode。
    std::vector<std::pair<FileNode*, std::string>> decoded;
    bool verified = true;
    auto readEncrypted = [&](FileNode* node) {
        if (!verified || node->is_directory() || node->get_sealed_content().empty())
            return;
        std::string raw, plain;
        if (!decodeBase64Content(node->get_sealed_content(), raw) ||
            !PrivacyCrypto::decrypt(raw, key, plain) ||
            plain.size() != static_cast<std::size_t>(node->get_size()))
        {
            PrivacyCrypto::wipe(plain);
            verified = false;
            return;
        }
        decoded.emplace_back(node, std::move(plain));
    };
    file_system->traverse(file_system->get_root(), readEncrypted);
    std::function<void(FileNode*)> readRecycle = [&](FileNode* node) {
        readEncrypted(node);
        for (FileNode* child = node->get_first_child(); child; child = child->get_next_sibling())
            readRecycle(child);
    };
    file_system->for_each_recycle_item(
        [&](FileNode* root, const std::string&) { readRecycle(root); });

    if (!verified)
    {
        for (auto& item : decoded) PrivacyCrypto::wipe(item.second);
        PrivacyCrypto::wipe(key);
        QMessageBox::critical(this, "解密失败",
            "AES-GCM 认证失败：可能是隐私密码已改变、加密内容被篡改或存档损坏。\n"
            "未修改任何文件内容，原有密文仍然保留。请检查备份与密码变更记录。");
        return false;
    }

    for (auto& item : decoded)
    {
        if (!file_system->set_transient_plain_content(item.first, item.second))
        {
            // 仅内部状态错误才会出现；所有解码数据来自有效节点。
            file_system->wipe_sealed_plaintext();
            PrivacyCrypto::wipe(key);
            QMessageBox::critical(this, "解密失败", "明文加载出现内部错误。");
            return false;
        }
        PrivacyCrypto::wipe(item.second);
    }
    crypto_salt_ = salt;
    PrivacyCrypto::wipe(crypto_session_key_);
    crypto_session_key_.swap(key);
    return true;
}

bool MainWindow::prepare_encrypted_content_for_save()
{
    // 不进行部分更新：先准备所有新 E1 数据，再统一替换旧密文。
    std::vector<std::pair<FileNode*, std::string>> replacements;
    std::vector<FileNode*> toClear;
    bool success = true;
    QString error;
    auto prepare = [&](FileNode* node, bool protectedNode) {
        if (!success || node->is_directory()) return;
        if (protectedNode)
        {
            if (!crypto_session_key_.empty())
            {
                if (node->get_size() < 0 ||
                    node->get_content().size() != static_cast<std::size_t>(node->get_size()))
                {
                    success = false;
                    error = "加密文件缺少已解密内容，已阻止保存。";
                    return;
                }
                std::string packed;
                if (!PrivacyCrypto::encrypt(node->get_content(), crypto_session_key_, packed))
                {
                    success = false;
                    error = "OpenSSL AES-256-GCM 加密失败。";
                    return;
                }
                replacements.emplace_back(node, encodePlainContent(packed));
            }
            // 没有密码时 E1 原密文直接保留；旧 X1 只兼容读取及原样写回。
        }
        else if (!node->get_sealed_content().empty())
        {
            if (crypto_session_key_.empty())
            {
                success = false;
                error = "存在尚未解密却不再受保护的文件。请先验证隐私密码。";
                return;
            }
            toClear.push_back(node);
        }
    };
    file_system->traverse(file_system->get_root(), [&](FileNode* node) {
        prepare(node, is_privacy_protected(node));
    });
    std::function<void(FileNode*)> visitRecycle = [&](FileNode* node) {
        prepare(node, file_system->is_recycle_privacy_protected(node));
        for (FileNode* child = node->get_first_child(); child; child = child->get_next_sibling())
            visitRecycle(child);
    };
    file_system->for_each_recycle_item(
        [&](FileNode* root, const std::string&) { visitRecycle(root); });
    if (!success)
    {
        QMessageBox::critical(this, "加密存档失败", error +
            "\n为了保护旧数据，本次未覆盖存档。");
        return false;
    }
    for (const auto& item : replacements)
        file_system->set_sealed_content(item.first, item.second);
    for (FileNode* node : toClear)
        file_system->clear_sealed_content(node);
    return true;
}
