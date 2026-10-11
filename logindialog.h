#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog();
    QString get_username() const; // 供主界面读取当前登录的是谁

private:
    Ui::LoginDialog *ui;
      QString current_user;

private slots:
    void on_btn_register_clicked(); // 👈 注册按钮的槽函数声明
    void on_btn_login_clicked();    // 👈 登录按钮的槽函数声明
};

#endif // LOGINDIALOG_H
