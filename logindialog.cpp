#include "logindialog.h"
#include "ui_logindialog.h"
#include <QCryptographicHash>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LoginDialog)
{
    ui->setupUi(this);
}

LoginDialog::~LoginDialog()
{
    delete ui;
}



// 加盐哈希，防止彩虹表破解
QString hashPassword(const QString& password) {
    QString salt = "FileManager_Salt_2024"; // 固定盐值，也可以每个用户随机生成存到用户文件里
    return QString(QCryptographicHash::hash((password + salt).toUtf8(), QCryptographicHash::Sha256).toHex());
}

// 注册按钮
void LoginDialog::on_btn_register_clicked() {
    QString user = ui->lineEdit_user->text().trimmed();
    QString pass = ui->lineEdit_pass->text().trimmed();

    if (user.isEmpty() || pass.isEmpty()) {
        QMessageBox::warning(this, "提示", "账号和密码不能为空！");
        return;
    }

    // 检查用户名是否重复
    QFile file("users.txt");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine();
            QStringList parts = line.split("|");
            if (parts.size() >= 1 && parts[0] == user) {
                QMessageBox::warning(this, "注册失败", "用户名已存在！");
                file.close();
                return;
            }
        }
        file.close();
    }

    // 写入新用户
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << user << "|" << hashPassword(pass) << "\n";
        file.close();
        QMessageBox::information(this, "注册成功", "账号已注册，请登录！");
    }
}

// 登录按钮
void LoginDialog::on_btn_login_clicked() {
    QString user = ui->lineEdit_user->text().trimmed();
    QString pass = ui->lineEdit_pass->text().trimmed();

    QFile file("users.txt");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine();
            QStringList parts = line.split("|");
            if (parts.size() >= 2 && parts[0] == user) {
                if (parts[1] == hashPassword(pass)) {
                    // 登录成功
                    current_user = user;
                    this->accept(); // 关闭登录窗口，返回 QDialog::Accepted
                    return;
                } else {
                    QMessageBox::warning(this, "登录失败", "密码错误！");
                    return;
                }
            }
        }
        file.close();
    }
    QMessageBox::warning(this, "登录失败", "账号不存在！");
}

QString LoginDialog::get_username() const {
    return current_user;
}