#pragma once
#include <QString>
inline QString app_theme() {
    return QStringLiteral(R"(
QMainWindow, QDialog { background: #f5f7fb; }
QLabel { color: #24344b; }
QLineEdit, QPlainTextEdit, QComboBox { background: white; color: #24344b; border: 1px solid #cbd5e1; border-radius: 5px; padding: 5px; }
QLineEdit:focus, QPlainTextEdit:focus { border: 1px solid #3b82f6; }
QPushButton { background: #eef3fa; color: #24466b; border: 1px solid #cbd5e1; border-radius: 5px; padding: 5px 10px; }
QPushButton:hover { background: #dbeafe; border-color: #60a5fa; }
QPushButton:disabled { color: #94a3b8; }
QListWidget, QTreeWidget { background: white; alternate-background-color: #f8fafc; border: 1px solid #dbe3ef; border-radius: 6px; selection-background-color: #dbeafe; selection-color: #173e70; }
QListWidget::item { min-height: 34px; padding: 3px; }
QHeaderView::section { background: #eaf0f8; color: #334155; border: none; padding: 6px; }
QMenuBar { background: #ffffff; color: #24466b; }
QMenuBar::item:selected { background: #dbeafe; }
)");
}
