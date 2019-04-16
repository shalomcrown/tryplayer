#include "tryplayer.h"
#include "ui_tryplayer.h"

tryplayer::tryplayer(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::tryplayer)
{
    ui->setupUi(this);
}

tryplayer::~tryplayer()
{
    delete ui;
}
