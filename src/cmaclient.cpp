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

#include "cmaclient.h"
#include "capability.h"
#include "avdecoder.h"
#include "cmaevent.h"
#include "utils.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QImage>
#include <QTime>
#include <QSettings>
#include <QUrl>

QMutex CmaClient::mutex;
QMutex CmaClient::runner;
QWaitCondition CmaClient::usbcondition;
QMutex CmaClient::usbwait;
QSemaphore CmaClient::sema;

QString CmaClient::tempOnlineId = QString();

bool CmaClient::is_active = false;
bool CmaClient::in_progress = false;

CmaClient *CmaClient::this_object = NULL;

CmaClient::CmaClient(QObject *parent) :
    QObject(parent)
{
    this_object = this;
}

void CmaClient::connectUsb()
{
    vita_device_t *vita;

    qDebug("Starting usb_thread: 0x%016" PRIxPTR, (uintptr_t)QThread::currentThreadId());

    setActive(true);
    usbwait.lock();

    do {
        if((vita = VitaMTP_Get_First_USB_Vita()) !=NULL) {
            processNewConnection(vita);
        } else {
            //TODO: replace this with an event-driven setup
            usbcondition.wait(&usbwait, 2000);
            mutex.lock();
            if(in_progress) {
                sema.acquire();
            }
            mutex.unlock();
        }
    } while(isActive());

    qDebug("Finishing usb_thread");
    emit finished();
}

void CmaClient::connectWireless()
{
    vita_device_t *vita;
    wireless_host_info_t host = {NULL, NULL, NULL, QCMA_REQUEST_PORT};
    typedef CmaClient CC;

    QTime now = QTime::currentTime();
    qsrand(now.msec());

    qDebug("Starting wireless_thread: 0x%016" PRIxPTR, (uintptr_t)QThread::currentThreadId());

    setActive(true);

    do {
        if((vita = VitaMTP_Get_First_Wireless_Vita(&host, 0, CC::deviceRegistered, CC::generatePin, CC::registrationComplete)) != NULL) {
            processNewConnection(vita);
        } else {
            mutex.lock();
            if(in_progress) {
                sema.acquire();
            }
            mutex.unlock();

            // if is active then something happened while setting the socket, wait a little and try again
            if(isActive()) {
                qDebug("Error getting wireless connection");
                Sleeper::sleep(2000);
            } else {
                qDebug("Wireless connection cancelled by the user");
            }
        }
    } while(isActive());

    qDebug("Finishing wireless_thread");
    emit finished();
}

void CmaClient::processNewConnection(vita_device_t *device)
{
    QMutexLocker locker(&mutex);
    in_progress = true;
    broadcast.setUnavailable();

    qDebug("Vita connected: id %s", VitaMTP_Get_Identification(device));
    DeviceCapability vita_info;

    if(!vita_info.exchangeInfo(device)) {
        qCritical("Error while exchanging info with the vita");
    } else {
        QSettings settings;

        // Conection successful, inform the user
        if(vita_info.getOnlineId() != NULL) {
            settings.setValue("lastOnlineId", vita_info.getOnlineId());
            emit deviceConnected(QString(tr("Connected to %1 (PS Vita)")).arg(vita_info.getOnlineId()));
        } else {
            QString onlineId = settings.value("lastOnlineId", "default").toString();
            emit deviceConnected(QString(tr("Connected to %1 (PS Vita)")).arg(onlineId));
        }

        enterEventLoop(device);
    }

    VitaMTP_SendHostStatus(device, VITA_HOST_STATUS_EndConnection);
    qDebug("Releasing device...");
    VitaMTP_Release_Device(device);

    emit deviceDisconnected();

    broadcast.setAvailable();
    in_progress = false;
    sema.release();
}

void CmaClient::registrationComplete()
{
    QSettings settings;
    qDebug("Registration completed");
    settings.setValue("lastOnlineId", tempOnlineId);
    emit this_object->pinComplete();
}

int CmaClient::deviceRegistered(const char *deviceid)
{
    qDebug("Got connection request from %s", deviceid);
    // TODO: check the device to see if is already registered
    return 1;
}

int CmaClient::generatePin(wireless_vita_info_t *info, int *p_err)
{
    // save the device name in a temporal variable, just in case the pin is rejected
    tempOnlineId = QString(info->name);
    qDebug("Registration request from %s (MAC: %s)", info->name, info->mac_addr);
    int pin = rand() % 10000 * 10000 | rand() % 10000;
    qDebug("Your registration PIN for %s is: %08d", info->name, pin);
    *p_err = 0;
    emit this_object->receivedPin(info->name, pin);
    return pin;
}

void CmaClient::enterEventLoop(vita_device_t *device)
{
    vita_event_t event;

    qDebug("Starting event loop");

    CmaEvent eventLoop (device);
    QThread thread;
    thread.setObjectName("event_thread");

    eventLoop.moveToThread(&thread);
    connect(&thread, SIGNAL(started()), &eventLoop, SLOT(process()));
    connect(&eventLoop, SIGNAL(refreshDatabase()), this, SIGNAL(refreshDatabase()), Qt::DirectConnection);
    connect(&eventLoop, SIGNAL(finishedEventLoop()), &thread, SLOT(quit()), Qt::DirectConnection);
    connect(&eventLoop, SIGNAL(messageSent(QString)), this, SIGNAL(messageSent(QString)), Qt::DirectConnection);
    thread.start();

    while(isActive()) {
        if(VitaMTP_Read_Event(device, &event) < 0) {
            qWarning("Error reading event from Vita.");
            break;
        }

        // do not create a event for this since there aren't more events to read
        if(event.Code == PTP_EC_VITA_RequestTerminate) {
            qDebug("Terminating event thread");
            break;

            // this one shuold be processed inmediately
        } else if(event.Code == PTP_EC_VITA_RequestCancelTask) {
            eventLoop.vitaEventCancelTask(&event, event.Param1);
            qDebug("Ended event, code: 0x%x, id: %d", event.Code, event.Param1);
            continue;
        }

        // the events are processed synchronously except for cancel/terminate
        qDebug("Sending new event");
        eventLoop.setEvent(event);
    }

    eventLoop.stop();
    thread.wait();
    qDebug("Finishing event loop");
}

int CmaClient::stop()
{
    if(!isActive()) {
        return -1;
    }
    CmaClient::setActive(false);
    VitaMTP_Cancel_Get_Wireless_Vita();
    usbcondition.wakeAll();
    return 0;
}

bool CmaClient::isActive()
{
    QMutexLocker locker(&runner);
    return is_active;
}

void CmaClient::setActive(bool state)
{
    QMutexLocker locker(&runner);
    is_active = state;
}
