#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "FileDetailsDelegate.h"
#include <QStandardItemModel>
#include <QMenu>
#include <QActionGroup>
#include <QWidgetAction>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QDate>
#include <QRegularExpression>
#include <QSet>
#include <QHash>
#include <QApplication>
#include <QScrollBar>
#include <QDateTime>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QToolButton>
#include <QLabel>
#include <QShortcut>
#include <QFileInfo>
#include <climits>
#include <algorithm>

static QSet<QString> category_words(QString text)
{
    text.replace(QRegularExpression("([a-z])([A-Z])"), "\\1 \\2");
    const QSet<QString> ignored{"txt", "pdf", "doc", "docx", "file", "files", "folder"};
    const QHash<QString, QString> aliases{{"gaming", "game"}, {"studies", "study"}, {"classes", "class"}, {"photos", "photo"}, {"pictures", "picture"}, {"movies", "movie"}};
    QSet<QString> words;
    for (auto word : text.toCaseFolded().split(QRegularExpression("[^\\p{L}\\p{N}]+"), Qt::SkipEmptyParts)) {
        if (ignored.contains(word) || word.size() < 2) continue;
        if (aliases.contains(word)) word = aliases.value(word);
        else if (QRegularExpression("^[a-z]+$").match(word).hasMatch()) {
            if (word.size() > 4 && word.endsWith("ies")) word = word.left(word.size() - 3) + "y";
            else if (word.size() > 3 && word.endsWith('s') && !word.endsWith("ss")) word.chop(1);
        }
        words.insert(word);
    }
    return words;
}

static int category_score(const QString& names, const QString& content, const QString& directory, const QString& path)
{
    const auto nameWords = category_words(names), bodyWords = category_words(content);
    const auto leafWords = category_words(directory), pathWords = category_words(path);
    int score = 0;
    for (auto word : leafWords) { if (nameWords.contains(word)) score += 8; if (bodyWords.contains(word)) score += 2; }
    for (auto word : pathWords - leafWords) { if (nameWords.contains(word)) score += 2; if (bodyWords.contains(word)) ++score; }
    const QStringList topics{
        "学习 课程 作业 考试 笔记 数学 英语 study school homework note course exam",
        "工作 项目 会议 报告 合同 work project meeting report contract",
        "生活 旅行 旅游 购物 日记 travel life diary shopping",
        "代码 编程 cpp python qt 软件 code programming software",
        "游戏 游戏攻略 攻略 电竞 娱乐 game gaming steam walkthrough",
        "音乐 歌曲 音频 music song audio",
        "照片 图片 摄影 相册 photo picture image photography",
        "视频 电影 剪辑 video movie film",
        "财务 发票 账单 预算 finance invoice bill budget"};
    auto has = [](const QString& text, const QSet<QString>& tokens, const QString& term) {
        return QRegularExpression("^[a-z]+$").match(term).hasMatch() ? tokens.contains(term) : text.contains(term, Qt::CaseInsensitive);
    };
    for (auto topic : topics) {
        auto terms = category_words(topic); bool related = false;
        for (auto term : terms) if (has(directory, leafWords, term)) related = true;
        if (related) for (auto term : terms) { if (has(names, nameWords, term)) score += 3; if (has(content, bodyWords, term)) ++score; }
    }
    // Chinese directory names often form part of a longer title rather than separate tokens.
    if (QRegularExpression("[\\p{Han}]").match(directory).hasMatch() && directory.size() >= 2) {
        if (names.contains(directory, Qt::CaseInsensitive)) score += 6;
        if (content.contains(directory, Qt::CaseInsensitive)) score += 2;
    }
    return score;
}

void MainWindow::setup_integrated_tools()
{
    auto panel = new QWidget(ui->splitter_main);
    auto box = new QVBoxLayout(panel); box->setContentsMargins(0, 0, 0, 0); box->setSpacing(0);
    auto headerRow = new QHBoxLayout; headerRow->setContentsMargins(1, 0, 1, 0);
    file_header_ = new FileHeaderView(Qt::Horizontal, panel);
    file_header_->setObjectName("file_header");
    auto model = new QStandardItemModel(0, 3, file_header_);
    model->setHorizontalHeaderLabels({"名称", "修改日期", "类型"});
    for (int i = 0; i < 3; ++i) model->setHeaderData(i, Qt::Horizontal, "点击按此列排序；再次点击切换升序 / 降序", Qt::ToolTipRole);
    file_header_->setCursor(Qt::PointingHandCursor);
    file_header_->setModel(model); file_header_->setSectionsClickable(true);
    file_header_->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    file_header_->setSectionResizeMode(0, QHeaderView::Stretch);
    file_header_->setSectionResizeMode(1, QHeaderView::Fixed);
    file_header_->setSectionResizeMode(2, QHeaderView::Fixed);
    file_header_->resizeSection(1, 180); file_header_->resizeSection(2, 100);
    file_header_->setFixedHeight(32);
    headerRow->addWidget(file_header_);
    headerRow->addSpacing(style()->pixelMetric(QStyle::PM_ScrollBarExtent));
    box->addLayout(headerRow); box->addWidget(ui->list_files);
    ui->splitter_main->insertWidget(1, panel);
    ui->splitter_main->setStretchFactor(1, 1);
    ui->splitter_main->setSizes({180, 800});
    ui->list_files->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    ui->list_files->setItemDelegate(new FileDetailsDelegate(file_header_, ui->list_files));
    ui->tree_dir->setItemDelegate(new DirectorySectionDelegate(ui->tree_dir));
    ui->tree_dir->setIndentation(14);
    ui->list_files->viewport()->installEventFilter(this);
    ui->tree_dir->viewport()->installEventFilter(this);

    path_stack_ = new QStackedWidget(this); path_stack_->setObjectName("path_stack");
    path_stack_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); path_stack_->setFixedHeight(30);
    auto previousPathItem = ui->horizontalLayout->replaceWidget(ui->lineEdit_path, path_stack_); delete previousPathItem;
    breadcrumb_bar_ = new QWidget(path_stack_); breadcrumb_bar_->setObjectName("breadcrumb_bar");
    auto crumbs = new QHBoxLayout(breadcrumb_bar_); crumbs->setContentsMargins(4, 0, 4, 0); crumbs->setSpacing(0);
    path_stack_->addWidget(breadcrumb_bar_); path_stack_->addWidget(ui->lineEdit_path);
    breadcrumb_bar_->installEventFilter(this); ui->lineEdit_path->installEventFilter(this);
    breadcrumb_bar_->setToolTip("点击目录逐级跳转；点击空白处或按 Alt+D 编辑完整路径");
    connect(ui->lineEdit_path, &QLineEdit::textChanged, this, [this] { if (!ui->lineEdit_path->hasFocus()) QTimer::singleShot(0, this, &MainWindow::sync_breadcrumbs); });
    connect(ui->lineEdit_path, &QLineEdit::returnPressed, this, [this] { QTimer::singleShot(0, this, &MainWindow::sync_breadcrumbs); });
    auto pathShortcut = new QShortcut(QKeySequence("Alt+D"), this);
    connect(pathShortcut, &QShortcut::activated, this, &MainWindow::edit_path);

    for (auto pair : {qMakePair(ui->btn_cut, QString("cut")), qMakePair(ui->btn_copy, QString("copy")),
                      qMakePair(ui->btn_paste, QString("paste")), qMakePair(ui->btn_rename, QString("rename")),
                      qMakePair(ui->btn_delete, QString("delete"))}) {
        QIcon icon(":/icons/icons/" + pair.second + ".svg");
        if (!icon.isNull()) { pair.first->setToolTip(pair.first->text()); pair.first->setAccessibleName(pair.first->text()); pair.first->setText({}); pair.first->setIcon(icon); pair.first->setIconSize(QSize(20, 20)); }
        pair.first->setFixedSize(42, 28);
    }
    ui->btn_new->setFixedSize(84, 28);
    ui->btn_undo->setFixedSize(ui->btn_new->size()); ui->btn_reset->setFixedSize(ui->btn_new->size());
    auto sortButton = new QPushButton("排序", this); sortButton->setObjectName("btn_sort");
    auto classifyButton = new QPushButton("自动归类", this); classifyButton->setObjectName("btn_classify");
    classify_button_ = classifyButton;
    search_options_button_ = new QPushButton("搜索选项", this); search_options_button_->setObjectName("btn_search_options");
    for (auto button : {sortButton, classifyButton, search_options_button_}) button->setFixedHeight(28);
    // Reuse the existing row; recycle actions retain their original visibility logic.
    while (auto item = ui->horizontalLayout_2->takeAt(0)) delete item;
    for (auto widget : QList<QWidget*>{ui->btn_new, ui->btn_cut, ui->btn_copy, ui->btn_paste, ui->btn_rename,
         ui->btn_delete, sortButton, ui->btn_undo, classifyButton, search_options_button_, ui->btn_restore,
         ui->btn_permanent_delete, ui->btn_clear_recycle}) ui->horizontalLayout_2->addWidget(widget);
    ui->horizontalLayout_2->addStretch(); ui->horizontalLayout_2->addWidget(ui->btn_reset);
    connect(classifyButton, &QPushButton::clicked, this, &MainWindow::auto_classify);
    search_options_button_->hide(); ui->lineEdit_search->installEventFilter(this);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* current) {
        if (current == ui->lineEdit_search) search_options_button_->show();
        else if (!search_mode_ && current && current != search_options_button_ && !search_options_button_->isAncestorOf(current) && !QApplication::activePopupWidget()) search_options_button_->hide();
    });

    auto sortMenu = new QMenu(sortButton); sortButton->setMenu(sortMenu);
    auto sortGroup = new QActionGroup(sortMenu);
    QStringList sorts{"相关度", "名称", "大小", "修改日期", "类型"};
    for (int i = 0; i < sorts.size(); ++i) {
        auto action = sortMenu->addAction(sorts[i]); action->setCheckable(true); action->setChecked(i == file_sort_); sortGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, i] { file_sort_ = i; sort_descending_ = i == 3; apply_file_sort(); });
    }
    sortMenu->addSeparator();
    sortMenu->addAction("升序", this, [this] { sort_descending_ = false; apply_file_sort(); });
    sortMenu->addAction("降序", this, [this] { sort_descending_ = true; apply_file_sort(); });
    connect(file_header_, &QHeaderView::sectionClicked, this, [this, sortGroup](int column) {
        int mode = column == 0 ? 1 : column == 1 ? 3 : 4;
        sort_descending_ = file_sort_ == mode ? !sort_descending_ : mode == 3;
        file_sort_ = mode; sortGroup->actions()[mode]->setChecked(true); apply_file_sort();
    });

    auto options = new QMenu(search_options_button_); search_options_button_->setMenu(options);
    auto changed = [this] { if (search_mode_) run_smart_search(); };
    auto range = options->addMenu("范围"); auto rangeGroup = new QActionGroup(range);
    for (int i = 0; i < 2; ++i) {
        auto action = range->addAction(i ? "整个文件系统" : "当前目录及子目录"); action->setCheckable(true); action->setChecked(i == 0); rangeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, i, changed] { search_global_ = i; changed(); });
    }
    auto addBounds = [&](const QString& title, QString* lower, QString* upper, bool date) {
        auto menu = options->addMenu(title); auto action = new QWidgetAction(menu);
        auto widget = new QWidget(menu); auto form = new QFormLayout(widget);
        auto low = new QLineEdit(*lower, widget); auto high = new QLineEdit(*upper, widget);
        low->setObjectName(date ? "search_date_after" : "search_size_min"); high->setObjectName(date ? "search_date_before" : "search_size_max");
        low->setPlaceholderText(date ? "YYYY-MM-DD，留空不限" : "字节，留空不限"); high->setPlaceholderText(low->placeholderText());
        auto apply = new QPushButton("应用", widget); apply->setObjectName(date ? "apply_search_date" : "apply_search_size");
        form->addRow("起始", low); form->addRow("截止", high); form->addRow(apply);
        action->setDefaultWidget(widget); menu->addAction(action);
        connect(menu, &QMenu::aboutToShow, widget, [low, high, lower, upper] { low->setText(*lower); high->setText(*upper); });
        connect(apply, &QPushButton::clicked, this, [this, low, high, lower, upper, date, menu, options, changed] {
            QString l = low->text().trimmed(), h = high->text().trimmed(); bool valid = true;
            if (date) valid = (l.isEmpty() || (l.size() == 10 && QDate::fromString(l, "yyyy-MM-dd").isValid())) &&
                (h.isEmpty() || (h.size() == 10 && QDate::fromString(h, "yyyy-MM-dd").isValid())) && (l.isEmpty() || h.isEmpty() || l <= h);
            else { bool a = true, b = true; auto x = l.isEmpty() ? 0 : l.toLongLong(&a); auto y = h.isEmpty() ? LLONG_MAX : h.toLongLong(&b); valid = a && b && x >= 0 && y >= x; }
            if (!valid) { QMessageBox::warning(this, "条件无效", "请检查起止范围；日期格式为 YYYY-MM-DD，大小为非负字节数。"); return; }
            *lower = l; *upper = h; menu->close(); options->close(); changed();
        });
    };
    addBounds("修改日期", &search_after_, &search_before_, true);
    auto types = options->addMenu("类型"); auto typeGroup = new QActionGroup(types);
    for (int i = 0; i < 3; ++i) {
        auto action = types->addAction(QStringList{"全部", "目录", "TXT 文件"}[i]); action->setCheckable(true); action->setChecked(i == 0); typeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, i, changed] { search_type_ = i; changed(); });
    }
    addBounds("大小", &search_min_, &search_max_, false);
    auto body = options->addAction("包含 TXT 正文"); body->setCheckable(true);
    connect(body, &QAction::toggled, this, [this, changed](bool checked) { search_body_ = checked; changed(); });
    connect(options, &QMenu::aboutToShow, this, [this, rangeGroup, typeGroup, body] {
        rangeGroup->actions()[search_global_ ? 1 : 0]->setChecked(true);
        typeGroup->actions()[search_type_]->setChecked(true);
        QSignalBlocker blocker(body); body->setChecked(search_body_);
    });
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == ui->list_files->viewport() || watched == ui->tree_dir->viewport()) && event->type() == QEvent::MouseButtonPress) {
        auto mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            bool blank = watched == ui->list_files->viewport() ? !ui->list_files->itemAt(mouse->position().toPoint()) : !ui->tree_dir->itemAt(mouse->position().toPoint());
            if (blank) {
                if (rename_click_timer) rename_click_timer->stop(); last_clicked_item = nullptr; last_click_time = 0;
                ui->list_files->clearSelection(); ui->list_files->setCurrentItem(nullptr);
                if (watched == ui->tree_dir->viewport()) { ui->tree_dir->clearSelection(); ui->tree_dir->setCurrentItem(nullptr); }
            }
        }
    }
    if (watched == breadcrumb_bar_ && event->type() == QEvent::MouseButtonPress) { edit_path(); return true; }
    if (watched == ui->lineEdit_path) {
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) { sync_breadcrumbs(); return true; }
        if (event->type() == QEvent::FocusOut) QTimer::singleShot(0, this, [this] { if (!ui->lineEdit_path->hasFocus() && !QApplication::activeModalWidget()) sync_breadcrumbs(); });
    }
    if (watched == ui->lineEdit_search) {
        if (event->type() == QEvent::FocusIn) search_options_button_->show();
        if (event->type() == QEvent::FocusOut) QTimer::singleShot(0, this, [this] {
            if (!search_mode_ && ui->lineEdit_search->text().isEmpty() && !ui->lineEdit_search->hasFocus() && !search_options_button_->hasFocus() && !QApplication::activePopupWidget()) search_options_button_->hide();
        });
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::edit_path()
{
    if (!file_system || recycle_mode || privacy_overview_mode) return;
    auto path = QString::fromStdString(file_system->get_path(file_system->get_current_directory()));
    ui->lineEdit_path->setText(path == "/" ? "/此电脑" : "/此电脑" + path);
    path_stack_->setCurrentWidget(ui->lineEdit_path); ui->lineEdit_path->setFocus(); ui->lineEdit_path->selectAll();
}

void MainWindow::sync_breadcrumbs()
{
    if (!file_system || !breadcrumb_bar_) return;
    auto layout = static_cast<QHBoxLayout*>(breadcrumb_bar_->layout());
    while (auto item = layout->takeAt(0)) { if (item->widget()) { item->widget()->hide(); item->widget()->deleteLater(); } delete item; }
    const auto currentPath = QString::fromStdString(file_system->get_path(file_system->get_current_directory()));
    QString display = recycle_mode ? "回收站" : privacy_overview_mode ? "隐私文件" : currentPath == "/" ? "/此电脑" : "/此电脑" + currentPath;
    { QSignalBlocker blocker(ui->lineEdit_path); ui->lineEdit_path->setText(display); }
    auto go = [this](QString path) { ui->lineEdit_path->setText(path == "/" ? "/此电脑" : "/此电脑" + path); ui->lineEdit_path->returnPressed(); };
    auto add = [&](const QString& title, const QString& path) {
        auto button = new QToolButton(breadcrumb_bar_); button->setObjectName("breadcrumb");
        button->setText(button->fontMetrics().elidedText(title, Qt::ElideRight, 110)); button->setToolTip(title); button->setProperty("path", path); button->setAutoRaise(true);
        button->setFixedWidth(qMin(128, button->fontMetrics().horizontalAdvance(button->text()) + 20)); button->setFixedHeight(28); layout->addWidget(button);
        button->setCursor(Qt::PointingHandCursor); button->show();
        connect(button, &QToolButton::clicked, this, [go, path] { go(path); }); return button;
    };
    if (recycle_mode || privacy_overview_mode) add(display, "/")->setEnabled(false);
    else {
        add("此电脑", "/"); auto parts = currentPath.split('/', Qt::SkipEmptyParts); QString path;
        QList<QPair<QString, QString>> segments;
        for (auto part : parts) { path += "/" + part; segments.append(qMakePair(part, path)); }
        auto arrow = [&] { auto label = new QLabel(" › ", breadcrumb_bar_); label->setFixedWidth(14); layout->addWidget(label); label->show(); };
        int begin = 0;
        int requiredWidth = 76;
        for (auto segment : segments) requiredWidth += qMin(128, breadcrumb_bar_->fontMetrics().horizontalAdvance(segment.first) + 20) + 14;
        if (segments.size() > 3 || (segments.size() > 2 && requiredWidth > path_stack_->width())) {
            arrow(); auto overflow = new QToolButton(breadcrumb_bar_); overflow->setText("…"); overflow->setToolTip("上级目录"); overflow->setAutoRaise(true); overflow->setPopupMode(QToolButton::InstantPopup);
            auto menu = new QMenu(overflow);
            begin = segments.size() - 2;
            for (int i = 0; i < begin; ++i) { auto segment = segments[i]; menu->addAction(segment.first, this, [go, segment] { go(segment.second); }); }
            overflow->setFixedWidth(32); overflow->setMenu(menu); layout->addWidget(overflow); overflow->show();
        }
        for (int i = begin; i < segments.size(); ++i) { arrow(); add(segments[i].first, segments[i].second); }
    }
    layout->addStretch(); path_stack_->setCurrentWidget(breadcrumb_bar_);
}

QList<QPair<FileNode*, int>> MainWindow::search_hits(bool indexed) const
{
    QList<FileNode*> nodes;
    auto start = search_global_ ? file_system->get_root() : file_system->get_current_directory();
    if (indexed && !search_index_valid_) {
        search_index_nodes_.clear(); search_name_index_.clear();
        file_system->traverse(file_system->get_root(), [&](FileNode* n) {
            if (n == file_system->get_root() || !can_access_node(n)) return;
            search_index_nodes_.append(n);
            auto name = QString::fromStdString(n->get_name()).toCaseFolded();
            for (int i = 0; i + 1 < name.size(); ++i) search_name_index_[name.mid(i, 2)].insert(n);
        });
        search_index_valid_ = true;
    }
    if (indexed) nodes = search_index_nodes_;
    else file_system->traverse(start, [&](FileNode* n) { if (n != start && can_access_node(n)) nodes.append(n); });
    auto tokens = ui->lineEdit_search->text().trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QSet<FileNode*> candidates; bool restricted = false;
    if (indexed && !search_body_) for (auto token : tokens) {
        token = token.toCaseFolded();
        for (int i = 0; i + 1 < token.size(); ++i) {
            auto posting = search_name_index_.value(token.mid(i, 2));
            if (!restricted) { candidates = posting; restricted = true; } else candidates.intersect(posting);
        }
    }
    QList<QPair<FileNode*, int>> hits;
    if (restricted) nodes = candidates.values();
    auto low = search_min_.isEmpty() ? 0 : search_min_.toLongLong(); auto high = search_max_.isEmpty() ? LLONG_MAX : search_max_.toLongLong();
    for (auto n : nodes) {
        if (restricted && !candidates.contains(n)) continue;
        if (n == start || !can_access_node(n)) continue;
        if (!search_global_) { auto ancestor = n->get_parent(); while (ancestor && ancestor != start) ancestor = ancestor->get_parent(); if (!ancestor) continue; }
        auto name = QString::fromStdString(n->get_name());
        if ((search_type_ == 1 && !n->is_directory()) || (search_type_ == 2 && (n->is_directory() || !name.endsWith(".txt", Qt::CaseInsensitive)))) continue;
        auto modified = QString::fromStdString(n->get_modified_time()).left(10);
        if (n->get_size() < low || n->get_size() > high || (!search_after_.isEmpty() && modified < search_after_) || (!search_before_.isEmpty() && modified > search_before_)) continue;
        int score = 1;
        for (auto token : tokens) {
            if (name.contains(token, Qt::CaseInsensitive)) score += 100;
            else if (search_body_ && !n->is_directory() && name.endsWith(".txt", Qt::CaseInsensitive) && QString::fromStdString(n->get_content()).contains(token, Qt::CaseInsensitive)) ++score;
            else { score = 0; break; }
        }
        if (score) hits.append(qMakePair(n, score));
    }
    return hits;
}

void MainWindow::run_smart_search()
{
    if (!ai_search_conditions_.isEmpty()) {
        auto obj = ai_search_conditions_; ai_search_conditions_ = {};
        if (obj.value("suffix").toString().remove('.').compare("txt", Qt::CaseInsensitive) == 0) search_type_ = 2;
        for (auto pair : {qMakePair(QString("min_bytes"), &search_min_), qMakePair(QString("max_bytes"), &search_max_)}) {
            auto value = obj.value(pair.first); *pair.second = value.isDouble() && value.toDouble() >= 0 && value.toDouble() < 9e15 ? QString::number(static_cast<qlonglong>(value.toDouble())) : QString();
        }
        search_after_ = obj.value("after").toString(); search_before_ = obj.value("before").toString();
        if (!QDate::fromString(search_after_, "yyyy-MM-dd").isValid()) search_after_.clear();
        if (!QDate::fromString(search_before_, "yyyy-MM-dd").isValid()) search_before_.clear();
        if ((!search_before_.isEmpty() && search_after_ > search_before_) || (!search_max_.isEmpty() && search_min_.toLongLong() > search_max_.toLongLong())) { search_after_.clear(); search_before_.clear(); search_min_.clear(); search_max_.clear(); }
    }
    if (rename_click_timer) rename_click_timer->stop(); last_clicked_item = nullptr;
    if (recycle_mode) { ui->statusbar->showMessage("请在普通目录中搜索", 3000); return; }
    if (ui->lineEdit_search->text().trimmed().isEmpty()) { refresh_file_list(); return; }
    search_mode_ = true; search_options_button_->show();
    auto results = search_hits(true); QSignalBlocker blocker(ui->list_files); ui->list_files->clear();
    for (auto hit : results) {
        addToListWidget(hit.first);
        auto item = ui->list_files->item(ui->list_files->count() - 1);
        item->setData(Qt::UserRole + 5, hit.second); item->setToolTip(QString::fromStdString(file_system->get_path(hit.first)));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    }
    apply_file_sort(); ui->statusbar->showMessage(QString("找到 %1 个项目").arg(results.size()), 3000);
}

void MainWindow::apply_file_sort()
{
    if (!file_header_) return;
    QSignalBlocker blocker(ui->list_files); auto selected = ui->list_files->currentItem();
    bool wasSelected = selected && selected->isSelected();
    QList<QListWidgetItem*> rows;
    while (ui->list_files->count()) rows.append(ui->list_files->takeItem(0));
    std::stable_sort(rows.begin(), rows.end(), [this](QListWidgetItem* a, QListWidgetItem* b) {
        auto x = a->data(Qt::UserRole).value<FileNode*>(); auto y = b->data(Qt::UserRole).value<FileNode*>();
        if (!x || !y) return a->text() < b->text();
        if (!search_mode_ && x->is_pinned() != y->is_pinned()) return x->is_pinned();
        if (!search_mode_ && file_sort_ == 0) return x->is_pinned() && y->is_pinned() && x->get_pin_order() < y->get_pin_order();
        int compare = 0;
        if (file_sort_ == 0 && search_mode_) compare = b->data(Qt::UserRole + 5).toInt() - a->data(Qt::UserRole + 5).toInt();
        else if (file_sort_ == 2) compare = x->get_size() < y->get_size() ? -1 : x->get_size() > y->get_size() ? 1 : 0;
        else if (file_sort_ == 3) compare = x->get_modified_time().compare(y->get_modified_time());
        else if (file_sort_ == 4) compare = int(y->is_directory()) - int(x->is_directory());
        if (!compare) compare = QString::fromStdString(x->get_name()).compare(QString::fromStdString(y->get_name()), Qt::CaseInsensitive);
        return sort_descending_ ? compare > 0 : compare < 0;
    });
    for (auto item : rows) ui->list_files->addItem(item);
    if (selected) { ui->list_files->setCurrentItem(selected); selected->setSelected(wasSelected); }
    int column = file_sort_ == 1 ? 0 : file_sort_ == 3 ? 1 : file_sort_ == 4 ? 2 : -1;
    file_header_->setSortIndicatorShown(column >= 0);
    if (column >= 0) file_header_->setSortIndicator(column, sort_descending_ ? Qt::DescendingOrder : Qt::AscendingOrder);
}

void MainWindow::auto_classify()
{
    auto item = ui->list_files->currentItem();
    if (!item || !item->isSelected()) {
        QStringList recent;
        for (int i = qMax(0, classification_history_.size() - 10); i < classification_history_.size(); ++i)
            recent.append(classification_history_privacy_.value(i) && !(has_privacy_access && show_privacy_mode) ? "隐私项目归类记录（已隐藏）" : classification_history_[i]);
        QMessageBox::information(this, "最近自动归类", recent.isEmpty() ? "暂无自动归类记录。" : recent.join('\n')); return;
    }
    auto node = item->data(Qt::UserRole).value<FileNode*>();
    if (recycle_mode || !file_system->contains_active_node(node) || !can_access_node(node)) { QMessageBox::warning(this, "无法归类", "请选择有权访问的普通项目。"); return; }
    QString names, content; bool allowed = true;
    file_system->traverse(node, [&](FileNode* n) {
        if (!can_access_node(n)) { allowed = false; return; }
        names += (n->is_directory() ? QString::fromStdString(n->get_name()) : QFileInfo(QString::fromStdString(n->get_name())).completeBaseName()) + ' ';
        if (content.size() < 16000) content += QString::fromStdString(n->get_content()).left(16000 - content.size()) + ' ';
    });
    if (!allowed) { QMessageBox::warning(this, "无法归类", "所选目录包含尚未授权访问的隐私项目。"); return; }
    struct Target { FileNode* node; int score; };
    QList<Target> targets;
    file_system->traverse(file_system->get_root(), [&](FileNode* d) {
        if (!d->is_directory() || d == node->get_parent() || !can_access_node(d) || !file_system->can_move(node, d) || file_system->has_name_conflict(d, node->get_name())) return;
        int score = category_score(names, content, QString::fromStdString(d->get_name()), QString::fromStdString(file_system->get_path(d)));
        if (score) targets.append({d, score});
    });
    std::sort(targets.begin(), targets.end(), [this](const Target& a, const Target& b) { return a.score != b.score ? a.score > b.score : file_system->get_path(a.node) < file_system->get_path(b.node); });
    if (targets.isEmpty()) { QMessageBox::information(this, "自动归类", "没有合适的位置。请保留当前位置或手动移动。"); return; }
    QDialog dialog(this); dialog.setObjectName("classification_dialog"); dialog.setWindowTitle("自动归类 — 选择位置"); dialog.resize(520, 300);
    auto layout = new QVBoxLayout(&dialog); auto choices = new QListWidget(&dialog); choices->setObjectName("classification_targets");
    for (auto target : targets) { auto row = new QListWidgetItem(QString::fromStdString(file_system->get_path(target.node)), choices); row->setData(Qt::UserRole, QVariant::fromValue(target.node)); row->setToolTip(QString("名称、正文与目录语义匹配 %1 项").arg(target.score)); }
    choices->setCurrentRow(0); layout->addWidget(choices);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("移动到所选位置"); buttons->button(QDialogButtonBox::Ok)->setObjectName("confirm_classification");
    layout->addWidget(buttons); connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || !choices->currentItem()) return;
    auto target = choices->currentItem()->data(Qt::UserRole).value<FileNode*>();
    if (!file_system->contains_active_node(node) || !file_system->contains_active_node(target) || !can_access_node(node) || !can_access_node(target)) return;
    Operation op; op.type = OperationType::MOVE; op.node = node; op.old_parent = node->get_parent(); op.new_parent = target; op.old_path = file_system->get_path(node);
    bool privateHistory = is_privacy_protected(node) || is_privacy_protected(target);
    if (!file_system->move_node(node, target)) { QMessageBox::warning(this, "归类失败", "目标无效或存在同名项目，文件未移动。"); return; }
    op.new_path = file_system->get_path(node); undo_stack.push(op);
    classification_history_.append(QDateTime::currentDateTime().toString("MM-dd HH:mm") + "  " + QString::fromStdString(op.old_path + " → " + op.new_path));
    classification_history_privacy_.append(privateHistory);
    if (!save_data()) QMessageBox::warning(this, "保存失败", "移动已完成，但保存失败，请撤销或重试保存。");
    refresh_tree(); refresh_file_list();
}
