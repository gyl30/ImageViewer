#include <QApplication>
#include <QImageReader>
#include "main_window.h"

int main(int argc, char *argv[])
{
    QImageReader::setAllocationLimit(0);

    QApplication app(argc, argv);

    main_window w;
    w.show();

    return QApplication::exec();
}
