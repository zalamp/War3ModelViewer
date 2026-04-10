#include <QApplication>
#include <QStyleFactory>
#include <QFont>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 设置全局样式和字体
    a.setStyle(QStyleFactory::create("Fusion"));
    QFont font("Microsoft YaHei", 9);
    a.setFont(font);

    MainWindow w;
    w.show();

    return a.exec();
}
