/*
 *  QCMA: Cross-platform content manager assistant for the PS Vita
 *
 *  Copyright (C) 2013  Codestation
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "backupitem.h"
#include "ui_backupitem.h"
#include "utils.h"
#include "dds.h"

#include <QDesktopServices>
#include <QUrl>

const QString BackupItem::nameTemplate = "<html><head/><body>"
        "<p><span style=\" font-size:12pt; font-weight:600;\">%1</span></p>"
        "<p><span style=\" font-size:10pt;\">%2</span></p>"
        "</body></html>";

BackupItem::BackupItem(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::BackupItem)
{
    ui->setupUi(this);
    // connect the buttons
    connect(ui->openButton, SIGNAL(clicked()), this, SLOT(openDirectory()));
    connect(ui->deleteButton, SIGNAL(clicked()), this, SLOT(removeEntry()));
}

BackupItem::~BackupItem()
{
    delete ui;
}

void BackupItem::openDirectory()
{
    QDesktopServices::openUrl(QUrl("file:///" + path));
}

void BackupItem::removeEntry()
{
    emit deleteEntry(this);
}

const QPixmap *BackupItem::getIconPixmap()
{
    return ui->itemPicture->pixmap();
}

void BackupItem::setDirectory(const QString path)
{
    this->path = path;
}

void BackupItem::setItemInfo(const QString name, const QString size)
{
    ui->itemName->setText(nameTemplate.arg(name, size));
}

int BackupItem::getIconWidth()
{
    return ui->itemPicture->width();
}

void BackupItem::setItemIcon(const QString path, int width, bool try_dds)
{
    ui->itemPicture->setMinimumWidth(width);
    QPixmap pixmap(path);
    if((pixmap.width() <= 0 || pixmap.height() <= 0) && try_dds) {
        QImage image;
        if(loadDDS(path, &image)) {
            pixmap = QPixmap::fromImage(image);
        }
    }
    ui->itemPicture->setPixmap(pixmap);
}

bool BackupItem::lessThan(const BackupItem *s1, const BackupItem *s2)
{
    return s1->title.compare(s2->title) < 0;
}

