#include "mainwindow.h"
#include "logindialog.h"
#include <QApplication>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "FileManager_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    // 👇 【加2】在这里启动登录窗口
    LoginDialog login;
    if (login.exec() == QDialog::Accepted) { // 如果登录成功
        // 获取刚才登录的账号名
        QString currentUser = login.get_username();

        // 把用户名传给主界面，实现数据隔离！
        MainWindow w(currentUser);
        w.show();

        return a.exec(); // 正常进入程序循环
    }

    // 如果直接关掉了登录框，程序直接退出
    return 0;
}
