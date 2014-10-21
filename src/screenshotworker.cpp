
#include "screenshotworker.h"

ScreenShotWorker::ScreenShotWorker(QObject *parent) :
    QObject(parent)
{
}

void ScreenShotWorker::screenShot()
{
    QFile::remove(SC_FILE_TEMP);

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface dbus_iface("org.nemomobile.lipstick", "/org/nemomobile/lipstick/screenshot",
                              "org.nemomobile.lipstick", bus);

    // takes about 200ms
    dbus_iface.call("saveScreenshot", SC_FILE_TEMP);

    QFile *scFileTemp = new QFile(SC_FILE_TEMP);

//    while (! scFileTemp->exists() || !(scFileTemp->size() == 6220817)){
//        LOG() << "wait for:" << SC_FILE_TEMP;
//    }

    QFile::remove(SC_FILE);
    if (!scFileTemp->rename(SC_FILE)){
        LOG() << "renamed" << SC_FILE_TEMP << "to" << SC_FILE  << "failed";
    } else {
        emit newScreenShot();
    }

}
