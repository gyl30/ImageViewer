#include <QApplication>
#include <QImageReader>
#include "common_types.h"
#include "main_window.h"

int main(int argc, char *argv[])
{
    QImageReader::setAllocationLimit(kQtImageReaderAllocMB);

    QApplication app(argc, argv);

    main_window w;
    w.show();

    return QApplication::exec();
}
