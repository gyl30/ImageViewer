#include <QApplication>
#include <QImageReader>
#include "main_window.h"

const int kMaxAllocMB = 256;
int main(int argc, char *argv[])
{
    QImageReader::setAllocationLimit(kMaxAllocMB);

    QApplication app(argc, argv);

    main_window w;
    w.show();

    return QApplication::exec();
}
