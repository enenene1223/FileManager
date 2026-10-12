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

    // 👇 使用循环，允许注销后重新弹出登录界面
    while (true) {
        LoginDialog login;
        if (login.exec() != QDialog::Accepted) {
            return 0; // 用户直接点 X 关闭登录框，程序彻底结束
        }

        // 登录成功，进入主界面
        MainWindow w(login.get_username());
        w.show();

        // 开始事件循环
        a.exec();

        // 检查是否是“注销”操作触发的退出
        // 如果在 MainWindow 里点了“注销”，我们会把 is_logout_requested 设为 true
        if (qApp->property("is_logout_requested").toBool() == false) {
            break; // 不是注销，说明用户点了“退出程序”，结束 while 循环
        }

        // 如果是注销，重置标志位，进入下一次 while 循环，重新弹出 LoginDialog
        qApp->setProperty("is_logout_requested", false);
    }

    return 0;
}
