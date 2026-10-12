#include "logindialog.h"
#include "ui_logindialog.h"
#include <QCryptographicHash>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>

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
//注册
void LoginDialog::on_btn_register_clicked()
{
    // 1. 获取三个输入框的内容
    QString user = ui->lineEdit_user->text().trimmed();
    QString pass = ui->lineEdit_pass->text().trimmed();
    QString privacyPass = ui->lineEdit_privacy_pass->text().trimmed(); // 👈 新增隐私密码

    if (user.isEmpty() || pass.isEmpty()) {
        QMessageBox::warning(this, "提示", "账号和密码不能为空！");
        return;
    }

    // 如果用户没填隐私密码，默认设为和登录密码相同
    if (privacyPass.isEmpty()) {
        privacyPass = pass;
    }

    // 2. 检查用户名是否重复
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

    // 3. 写入新用户（关键：改成三列格式！）
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        // 格式：账号 | 登录密码哈希 | 隐私密码哈希
        out << user << "|"
            << hashPassword(pass) << "|"
            << hashPassword(privacyPass) << "\n";
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
void LoginDialog::on_btn_modify_privacy_clicked()
{

        bool ok;
        // 1. 输入用户名
        QString username = QInputDialog::getText(this, "修改隐私密码", "请输入您的账号名：", QLineEdit::Normal, "", &ok);
        if (!ok || username.isEmpty()) return;

        // 2. 验证该用户是否存在，并获取旧信息
        QFile file("users.txt");
        QStringList lines;
        bool userFound = false;
        int userIndex = -1;

        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                QString line = in.readLine();
                lines.append(line);
                QStringList parts = line.split("|");
                if (parts.size() >= 3 && parts[0] == username) {
                    userFound = true;
                    userIndex = lines.size() - 1;
                }
            }
            file.close();
        }

        if (!userFound) {
            QMessageBox::warning(this, "错误", "该账号不存在！");
            return;
        }

        // 3. 输入旧隐私密码进行验证（如果你有实现校验函数，直接调用）
        QString oldPrivacyPass = QInputDialog::getText(this, "修改隐私密码", "请输入旧隐私密码：", QLineEdit::Password, "", &ok);
        if (!ok || oldPrivacyPass.isEmpty()) return;

        // 假设原密码以哈希形式存在第3个字段（索引2）
        QString oldHash = lines[userIndex].split("|")[2];
        if (oldHash != hashPassword(oldPrivacyPass)) {
            QMessageBox::warning(this, "错误", "旧隐私密码错误！");
            return;
        }

        // 4. 输入新隐私密码
        QString newPrivacyPass = QInputDialog::getText(this, "修改隐私密码", "请输入新隐私密码：", QLineEdit::Password, "", &ok);
        if (!ok || newPrivacyPass.isEmpty()) return;

        // 5. 更新 users.txt 对应行
        QStringList parts = lines[userIndex].split("|");
        parts[2] = hashPassword(newPrivacyPass); // 替换哈希值
        lines[userIndex] = parts.join("|");

        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            for (const QString& l : lines) {
                out << l << "\n";
            }
            file.close();
            QMessageBox::information(this, "成功", "隐私密码修改成功！");
        }

}

