#pragma once
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QHeaderView>
#include <QLineEdit>
#include <QTimer>
#include <QPointer>
#include <QStyleOptionHeader>
#include "FileNode.h"

class FileHeaderView : public QHeaderView {
public:
    using QHeaderView::QHeaderView;
protected:
    void paintSection(QPainter* painter, const QRect& rect, int section) const override {
        QStyleOptionHeader option; initStyleOption(&option); option.rect = rect; option.section = section;
        option.text = model()->headerData(section, Qt::Horizontal).toString();
        option.textAlignment = Qt::AlignLeft | Qt::AlignVCenter; option.sortIndicator = QStyleOptionHeader::None;
        style()->drawControl(QStyle::CE_Header, &option, painter, this);
        if (isSortIndicatorShown() && sortIndicatorSection() == section) {
            const int x = qMin(rect.right() - 14, rect.left() + fontMetrics().horizontalAdvance(option.text) + 17);
            const int y = rect.center().y();
            QPolygon triangle;
            if (sortIndicatorOrder() == Qt::AscendingOrder) triangle << QPoint(x - 4, y + 2) << QPoint(x + 4, y + 2) << QPoint(x, y - 3);
            else triangle << QPoint(x - 4, y - 2) << QPoint(x + 4, y - 2) << QPoint(x, y + 3);
            painter->save(); painter->setPen(Qt::NoPen); painter->setBrush(QColor("#315b87")); painter->drawPolygon(triangle); painter->restore();
        }
    }
};

class FileDetailsDelegate : public QStyledItemDelegate {
    QHeaderView* header_;
public:
    FileDetailsDelegate(QHeaderView* header, QObject* parent) : QStyledItemDelegate(parent), header_(header) {}
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto node = index.data(Qt::UserRole).value<FileNode*>();
        if (!node) { QStyledItemDelegate::paint(painter, option, index); return; }
        QStyleOptionViewItem base(option); initStyleOption(&base, index);
        const int width = header_->sectionSize(0);
        base.text = option.fontMetrics.elidedText(base.text, Qt::ElideMiddle, qMax(0, width - 42));
        auto style = base.widget ? base.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &base, painter, base.widget);
        painter->save(); painter->setPen(option.palette.color(option.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));
        for (int column = 1; column < 3; ++column) {
            QRect rect(option.rect.x() + header_->sectionPosition(column) + 6, option.rect.y(), header_->sectionSize(column) - 12, option.rect.height());
            QString text = column == 1 ? QString::fromStdString(node->get_modified_time()) : node->is_directory() ? "文件夹" : "TXT 文件";
            painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, option.fontMetrics.elidedText(text, Qt::ElideRight, rect.width()));
        }
        painter->restore();
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        QStyledItemDelegate::setEditorData(editor, index);
        auto line = qobject_cast<QLineEdit*>(editor); if (!line) return;
        auto node = index.data(Qt::UserRole).value<FileNode*>();
        int length = line->text().size();
        if (node && !node->is_directory()) { int dot = line->text().lastIndexOf('.'); if (dot > 0) length = dot; }
        line->setSelection(0, length);
        QTimer::singleShot(0, line, [line = QPointer<QLineEdit>(line), length] { if (line) line->setSelection(0, length); });
    }
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const override {
        // Leave the decoration visible to the left of the text editor.
        QRect rect = option.rect; rect.setLeft(rect.left() + 36); rect.setWidth(qMax(40, header_->sectionSize(0) - 40));
        editor->setGeometry(rect);
    }
};

class DirectorySectionDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (index.data(Qt::UserRole + 1).toInt() != 99) { QStyledItemDelegate::paint(painter, option, index); return; }
        painter->save(); painter->fillRect(option.rect, option.palette.brush(QPalette::Base));
        painter->setPen(QColor("#dbe3ec"));
        painter->drawLine(option.rect.left() + 8, option.rect.bottom() - 5, option.rect.right() - 8, option.rect.bottom() - 5); painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        return index.data(Qt::UserRole + 1).toInt() == 99 ? QSize(0, 22) : QStyledItemDelegate::sizeHint(option, index);
    }
};
