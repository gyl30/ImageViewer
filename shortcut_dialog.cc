#include "shortcut_dialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

void show_shortcuts_dialog(QWidget* parent, const QList<shortcut_entry>& entries)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("快捷键");
    dialog.resize(600, 480);

    auto* layout = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget(entries.size(), 3, &dialog);
    table->setHorizontalHeaderLabels({"范围", "操作", "快捷键"});
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(false);
    for (qsizetype row = 0; row < entries.size(); ++row)
    {
        const auto& entry = entries.at(row);
        table->setItem(static_cast<int>(row), 0, new QTableWidgetItem(entry.scope));
        table->setItem(static_cast<int>(row), 1, new QTableWidgetItem(entry.action));
        table->setItem(static_cast<int>(row), 2, new QTableWidgetItem(entry.keys));
    }
    layout->addWidget(table);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);
    dialog.exec();
}
