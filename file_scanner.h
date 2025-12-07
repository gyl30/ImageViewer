#ifndef FILE_SCANNER_H
#define FILE_SCANNER_H

#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QList>
#include <atomic>
#include "common_types.h"

Q_DECLARE_METATYPE(QList<image_meta>)

class file_scanner : public QObject
{
    Q_OBJECT
   public:
    explicit file_scanner(QObject* parent = nullptr);
    ~file_scanner() override;

   public slots:
    void start_scan(const QString& dir_path, int session_id);
    void stop_scan();

   signals:
    void images_scanned_batch(QList<image_meta> batch, int session_id);
    void scan_finished(int total_count, qint64 duration_ms, int session_id);

   private:
    std::atomic<bool> stop_flag_;
};

#endif
