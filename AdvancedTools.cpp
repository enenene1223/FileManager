#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QLabel>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QDate>
#include <climits>
#include <QSet>
#include <QHash>
#include <algorithm>

void MainWindow::open_advanced_tools()
{
    QDialog dialog(this);
    dialog.setWindowTitle("智能搜索与自动分类");
    dialog.resize(1080, 760);
    auto layout = new QVBoxLayout(&dialog);
    auto form = new QFormLayout;
    auto query = new QLineEdit(ui->lineEdit_search->text());
    auto scope = new QComboBox;
    scope->addItems({"当前目录及子目录", "整个文件系统"});
    auto type = new QComboBox;
    type->addItems({"所有类型", "文件", "文件夹", "TXT"});
    auto suffix = new QLineEdit;
    auto minimum = new QLineEdit;
    auto maximum = new QLineEdit;
    auto after = new QLineEdit;
    auto before = new QLineEdit;
    after->setPlaceholderText("YYYY-MM-DD，留空不限");
    before->setPlaceholderText("YYYY-MM-DD，留空不限");
    auto body = new QCheckBox("包含 TXT 正文（仅当前会话有权访问的文件）");
    auto sort = new QComboBox;
    sort->addItems({"相关度：名称优先", "名称", "大小升序", "修改时间降序"});
    form->addRow("关键词（空格分词）", query);
    form->addRow("范围", scope); form->addRow("类型", type);
    form->addRow("扩展名", suffix); form->addRow("最小字节", minimum);
    form->addRow("最大字节", maximum); form->addRow("修改日期起", after);
    form->addRow("修改日期止", before); form->addRow(body); form->addRow("排序", sort);
    layout->addLayout(form);
    if (!ai_search_conditions_.isEmpty()) {
        auto conditions = ai_search_conditions_; ai_search_conditions_ = {};
        suffix->setText(conditions.value("suffix").toString().left(32));
        for (auto pair : {qMakePair(QString("min_bytes"), minimum), qMakePair(QString("max_bytes"), maximum)}) {
            auto value = conditions.value(pair.first);
            if (value.isDouble() && value.toDouble() >= 0 && value.toDouble() < 9e15)
                pair.second->setText(QString::number(static_cast<qlonglong>(value.toDouble())));
        }
        after->setText(conditions.value("after").toString().left(10));
        before->setText(conditions.value("before").toString().left(10));
    }
    auto search = new QPushButton("搜索 / 对比索引与遍历");
    layout->addWidget(search);
    auto status = new QLabel("双击结果定位；选择文件后可推荐分类目录。索引仅在此窗口存活，不缓存正文。");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto tree = new QTreeWidget;
    tree->setHeaderLabels({"名称", "类型", "大小（字节）", "修改时间", "路径", "匹配"});
    tree->setAlternatingRowColors(true);
    for (int i = 0; i < 6; ++i) tree->setColumnWidth(i, QList<int>{160, 80, 100, 170, 300, 70}[i]);
    layout->addWidget(tree);
    auto classify = new QPushButton("为选中文件推荐目录（确认后移动）");
    layout->addWidget(classify);
    auto history = new QPushButton("查看本次会话分类历史");
    layout->addWidget(history);
    connect(history, &QPushButton::clicked, &dialog, [&] {
        QMessageBox::information(&dialog, "分类历史（重启后清除）", classification_history_.isEmpty() ? "本次会话暂无分类记录。撤销操作请使用主窗口撤销按钮。" : classification_history_.join('\n'));
    });
    QList<FileNode*> index;
    QHash<QString, QSet<FileNode*>> name_index;
    QElapsedTimer timer; timer.start();
    file_system->traverse(file_system->get_root(), [&](FileNode* n) {
        if (can_access_node(n)) {
            index.append(n);
            auto name = QString::fromStdString(n->get_name()).toCaseFolded();
            for (int i = 0; i + 1 < name.size(); ++i) name_index[name.mid(i, 2)].insert(n);
        }
    });
    const auto buildNs = timer.nsecsElapsed();
    struct Hit { FileNode* node; int score; };
    connect(search, &QPushButton::clicked, &dialog, [&] {
        bool okMin = true, okMax = true;
        qlonglong low = minimum->text().isEmpty() ? 0 : minimum->text().toLongLong(&okMin);
        qlonglong high = maximum->text().isEmpty() ? LLONG_MAX : maximum->text().toLongLong(&okMax);
        QRegularExpression date("^\\d{4}-\\d{2}-\\d{2}$");
        if (!okMin || !okMax || low < 0 || high < low ||
            (!after->text().isEmpty() && !date.match(after->text()).hasMatch()) ||
            (!before->text().isEmpty() && !date.match(before->text()).hasMatch()) ||
            (!after->text().isEmpty() && !QDate::fromString(after->text(), "yyyy-MM-dd").isValid()) ||
            (!before->text().isEmpty() && !QDate::fromString(before->text(), "yyyy-MM-dd").isValid()) ||
            (!after->text().isEmpty() && !before->text().isEmpty() && after->text() > before->text())) {
            QMessageBox::warning(&dialog, "条件无效", "请检查大小范围与日期格式、顺序。"); return;
        }
        auto start = scope->currentIndex() ? file_system->get_root() : file_system->get_current_directory();
        auto prefix = QString::fromStdString(file_system->get_path(start));
        auto tokens = query->text().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        auto match = [&](FileNode* n) -> int {
            if (!can_access_node(n) || n == file_system->get_root()) return 0;
            auto path = QString::fromStdString(file_system->get_path(n));
            if (prefix != "/" && path != prefix && !path.startsWith(prefix + "/")) return 0;
            QString name = QString::fromStdString(n->get_name());
            if ((type->currentIndex() == 1 && n->is_directory()) ||
                (type->currentIndex() == 2 && !n->is_directory()) ||
                (type->currentIndex() == 3 && (n->is_directory() || !name.endsWith(".txt", Qt::CaseInsensitive)))) return 0;
            QString ext = suffix->text().trimmed();
            if (!ext.isEmpty() && !name.endsWith(ext.startsWith('.') ? ext : "." + ext, Qt::CaseInsensitive)) return 0;
            auto modified = QString::fromStdString(n->get_modified_time()).left(10);
            if (n->get_size() < low || n->get_size() > high ||
                (!after->text().isEmpty() && modified < after->text()) ||
                (!before->text().isEmpty() && modified > before->text())) return 0;
            int score = 1;
            for (auto token : tokens) {
                if (name.contains(token, Qt::CaseInsensitive)) score += 100;
                else if (body->isChecked() && !n->is_directory() && name.endsWith(".txt", Qt::CaseInsensitive) &&
                         QString::fromStdString(n->get_content()).contains(token, Qt::CaseInsensitive)) score += 1;
                else return 0;
            }
            return score;
        };
        QList<Hit> hits;
        timer.restart();
        QSet<FileNode*> candidates;
        bool restricted = false;
        if (!body->isChecked()) for (auto token : tokens) {
            token = token.toCaseFolded();
            for (int i = 0; i + 1 < token.size(); ++i) {
                auto posting = name_index.value(token.mid(i, 2));
                if (!restricted) { candidates = posting; restricted = true; }
                else candidates.intersect(posting);
            }
        }
        if (restricted) {
            for (auto n : candidates) { int score = match(n); if (score) hits.append({n, score}); }
        } else for (auto n : index) { int score = match(n); if (score) hits.append({n, score}); }
        auto indexNs = timer.nsecsElapsed();
        QSet<FileNode*> dfsResults; timer.restart();
        file_system->traverse(start, [&](FileNode* n) { if (match(n)) dfsResults.insert(n); });
        auto dfsNs = timer.nsecsElapsed();
        QSet<FileNode*> indexedResults;
        for (auto hit : hits) indexedResults.insert(hit.node);
        std::sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) {
            if (sort->currentIndex() == 0 && a.score != b.score) return a.score > b.score;
            if (sort->currentIndex() == 2 && a.node->get_size() != b.node->get_size()) return a.node->get_size() < b.node->get_size();
            if (sort->currentIndex() == 3 && a.node->get_modified_time() != b.node->get_modified_time()) return a.node->get_modified_time() > b.node->get_modified_time();
            return a.node->get_name() < b.node->get_name();
        });
        tree->clear();
        for (auto hit : hits) {
            auto n = hit.node;
            auto item = new QTreeWidgetItem(tree, {QString::fromStdString(n->get_name()), n->is_directory() ? "文件夹" : "文件",
                QString::number(n->get_size()), QString::fromStdString(n->get_modified_time()), QString::fromStdString(file_system->get_path(n)), QString::number(hit.score)});
            item->setData(0, Qt::UserRole, QVariant::fromValue(n));
        }
        status->setText(QString("结果 %1；索引构建 %2 ms，查询 %3 ms，DFS %4 ms；结果数量校验 %5。小数据计时存在波动。")
            .arg(hits.size()).arg(buildNs / 1e6, 0, 'f', 3).arg(indexNs / 1e6, 0, 'f', 3).arg(dfsNs / 1e6, 0, 'f', 3).arg(dfsResults == indexedResults ? "通过" : "失败"));
    });
    connect(tree, &QTreeWidget::itemDoubleClicked, &dialog, [&](QTreeWidgetItem* item, int) {
        auto n = item->data(0, Qt::UserRole).value<FileNode*>();
        if (!file_system->contains_active_node(n) || !can_access_node(n)) return;
        navigate_to(file_system->get_path(n->is_directory() ? n : n->get_parent()));
        for (int i = 0; i < ui->list_files->count(); ++i)
            if (ui->list_files->item(i)->data(Qt::UserRole).value<FileNode*>() == n) ui->list_files->setCurrentRow(i);
        dialog.accept();
    });
    connect(classify, &QPushButton::clicked, &dialog, [&] {
        if (!tree->currentItem()) return;
        auto n = tree->currentItem()->data(0, Qt::UserRole).value<FileNode*>();
        if (!file_system->contains_active_node(n) || !can_access_node(n) || n->is_directory()) return;
        QString text = QString::fromStdString(n->get_name() + " " + n->get_content()).toLower();
        struct Target { FileNode* node; int score; };
        QList<Target> targets;
        const QStringList groups = {"学习 课程 作业 考试 笔记 数学 英语", "工作 项目 会议 报告 合同", "生活 旅行 旅游 购物 日记", "代码 编程 cpp python qt 软件"};
        for (auto d : index) {
            if (!d->is_directory() || d == n->get_parent() || !can_access_node(d) || !file_system->can_move(n, d) || file_system->has_name_conflict(d, n->get_name())) continue;
            auto semantic = QString::fromStdString(file_system->get_path(d)).toLower();
            int score = 0;
            for (auto group : groups) {
                bool directoryMatch = false;
                auto words = group.split(' ');
                for (auto word : words) if (semantic.contains(word)) directoryMatch = true;
                if (directoryMatch) for (auto word : words) if (text.contains(word)) ++score;
            }
            if (score) targets.append({d, score});
        }
        std::sort(targets.begin(), targets.end(), [](const Target& a, const Target& b) { return a.score > b.score; });
        if (targets.isEmpty()) { QMessageBox::information(&dialog, "分类", "没有足够的目录语义证据，暂不推荐移动。"); return; }
        auto target = targets.first().node;
        auto oldPath = file_system->get_path(n);
        auto targetPath = file_system->get_path(target);
        if (QMessageBox::question(&dialog, "确认分类移动", QString("名称及正文与目录语义匹配 %1 项。\n%2\n→ %3\n确认移动？可使用撤销恢复。")
                .arg(targets.first().score).arg(QString::fromStdString(oldPath), QString::fromStdString(targetPath)), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        Operation op; op.type = OperationType::MOVE; op.node = n; op.old_parent = n->get_parent(); op.new_parent = target; op.old_path = oldPath;
        if (!file_system->move_node(n, target)) { QMessageBox::warning(&dialog, "分类", "移动失败，可能存在重名或权限限制。"); return; }
        op.new_path = file_system->get_path(n); undo_stack.push(op);
        classification_history_.append(QString::fromStdString(op.old_path + " → " + op.new_path));
        if (!save_data()) QMessageBox::warning(&dialog, "保存失败", "移动已在内存中完成，但存档保存失败，请撤销或重试保存。");
        refresh_tree(); refresh_file_list();
        search->click();
    });
    dialog.exec();
}
