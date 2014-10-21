#ifndef SCREENSHOTWORKER_H
#define SCREENSHOTWORKER_H

#include <QFile>
#include <QObject>
#include <QtDBus/QtDBus>

#include "definitions.h"
#include "logging.h"

class ScreenShotWorker : public QObject
{
    Q_OBJECT
public:
    explicit ScreenShotWorker(QObject *parent = 0);

signals:
    void newScreenShot();

public slots:
    void screenShot();

};

#endif // SCREENSHOTWORKER_H
