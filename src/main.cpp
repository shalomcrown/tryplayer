#include "tryplayer.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    tryplayer w;
    w.show();

    return app.exec();
}

