#include <QApplication>
#include <QImageReader>
#include <QCommandLineParser>
#include <QIcon>
#include "common_types.h"
#include "main_window.h"

int main(int argc, char *argv[])
{
    QImageReader::setAllocationLimit(kQtImageReaderAllocMB);

    QApplication app(argc, argv);
    QApplication::setApplicationName("ImageViewer");
    QApplication::setApplicationVersion(IMAGEVIEWER_VERSION);
    QApplication::setDesktopFileName("io.github.gyl30.ImageViewer");
    QApplication::setWindowIcon(QIcon(":/icons/image.svg"));

    QCommandLineParser parser;
    parser.setApplicationDescription("轻量瀑布流图片浏览器");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    main_window w;
    w.show();

    return QApplication::exec();
}
