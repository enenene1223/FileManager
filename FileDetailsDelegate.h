#pragma once
#include <QStyledItemDelegate>
#include <QPainter>
#include <QFileInfo>
#include <QApplication>
#include "FileNode.h"

class FileDetailsDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto node = index.data(Qt::UserRole).value<FileNode*>();
        if (!node || option.rect.width() < 520) {
            QStyledItemDelegate::paint(painter, option, index); return;
        }
        QStyleOptionViewItem base(option);
        initStyleOption(&base, index);
        base.text = option.fontMetrics.elidedText(base.text, Qt::ElideMiddle, option.rect.width() - 320);
        auto style = base.widget ? base.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &base, painter, base.widget);
        painter->save();
        painter->setPen(option.palette.color(option.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));
        QString ext = QFileInfo(QString::fromStdString(node->get_name())).suffix().toUpper();
        QString type = node->is_directory() ? "文件夹" : (ext.isEmpty() ? "文件" : ext + " 文件");
        QRect typeRect = option.rect.adjusted(option.rect.width() - 270, 0, -180, 0);
        QRect timeRect = option.rect.adjusted(option.rect.width() - 174, 0, -8, 0);
        painter->drawText(typeRect, Qt::AlignVCenter | Qt::AlignLeft, option.fontMetrics.elidedText(type, Qt::ElideRight, typeRect.width()));
        painter->drawText(timeRect, Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(node->get_modified_time()));
        painter->restore();
    }
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const override {
        auto rect = option.rect;
        if (rect.width() >= 520) rect.setRight(rect.right() - 280);
        editor->setGeometry(rect);
    }
};
