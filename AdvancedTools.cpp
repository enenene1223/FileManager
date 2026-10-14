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
#include <climits>
#include <algorithm>

void MainWindow::setup_integrated_tools()
{
    auto panel = new QWidget(ui->splitter_main);
    auto box = new QVBoxLayout(panel); box->setContentsMargins(0, 0, 0, 0); box->setSpacing(0);
    auto headerRow = new QHBoxLayout; headerRow->setContentsMargins(1, 0, 1, 0);
    file_header_ = new QHeaderView(Qt::Horizontal, panel);
    file_header_->setObjectName("file_header");
    auto model = new QStandardItemModel(0, 3, file_header_);
    model->setHorizontalHeaderLabels({"名称", "修改日期", "类型"});
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
    if (watched == ui->lineEdit_search) {
        if (event->type() == QEvent::FocusIn) search_options_button_->show();
        if (event->type() == QEvent::FocusOut) QTimer::singleShot(0, this, [this] {
            if (!search_mode_ && ui->lineEdit_search->text().isEmpty() && !ui->lineEdit_search->hasFocus() && !search_options_button_->hasFocus() && !QApplication::activePopupWidget()) search_options_button_->hide();
        });
    }
    return QMainWindow::eventFilter(watched, event);
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
    QString text; bool allowed = true;
    file_system->traverse(node, [&](FileNode* n) { if (!can_access_node(n)) { allowed = false; return; } if (text.size() < 16000) text += QString::fromStdString(n->get_name() + " " + n->get_content()).left(16000 - text.size()) + ' '; });
    if (!allowed) { QMessageBox::warning(this, "无法归类", "所选目录包含尚未授权访问的隐私项目。"); return; }
    text = text.toLower(); struct Target { FileNode* node; int score; };
    QList<Target> targets;
    QStringList groups{"学习 课程 作业 考试 笔记 数学 英语 study school homework notes", "工作 项目 会议 报告 合同 work project meeting report", "生活 旅行 旅游 购物 日记 travel life diary", "代码 编程 cpp python qt 软件 code programming"};
    file_system->traverse(file_system->get_root(), [&](FileNode* d) {
        if (!d->is_directory() || d == node->get_parent() || !can_access_node(d) || !file_system->can_move(node, d) || file_system->has_name_conflict(d, node->get_name())) return;
        auto semantic = QString::fromStdString(file_system->get_path(d)).toLower(); int score = 0;
        for (auto group : groups) { auto words = group.split(' '); bool directoryMatch = false; for (auto word : words) if (semantic.contains(word)) directoryMatch = true; if (directoryMatch) for (auto word : words) if (text.contains(word)) ++score; }
        auto leaf = QString::fromStdString(d->get_name()).toLower(); if (leaf.size() >= 2 && text.contains(leaf)) score += 2;
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
