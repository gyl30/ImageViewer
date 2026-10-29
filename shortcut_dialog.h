#ifndef SHORTCUT_DIALOG_H
#define SHORTCUT_DIALOG_H

#include <QList>
#include <QString>

class QWidget;

struct shortcut_entry
{
    QString scope;
    QString action;
    QString keys;
};

void show_shortcuts_dialog(QWidget* parent, const QList<shortcut_entry>& entries);

#endif
