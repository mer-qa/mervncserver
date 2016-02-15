QT += core
QT -= gui

TEMPLATE = app
TARGET = mervncserver
target.path += /usr/bin
INSTALLS = target

CONFIG   += link_pkgconfig
PKGCONFIG += libvncserver

SOURCES += \
    screentovnc.cpp \
    main.cpp

HEADERS += \
    screentovnc.h \
    logging.h \
    pointer_finger.h \
    pointer_finger_touch.h \
    empty_mouse.h

CONFIG(release, debug|release) {
    DEFINES += QT_NO_DEBUG_OUTPUT
}

!without_systemd {
    DEFINES += SD_DAEMON_DISABLE_MQ
    PKGCONFIG += libsystemd-daemon
} else {
    DEFINES += MER_WITHOUT_SYSTEMD
}

without_network_check {
    DEFINES += MER_WITHOUT_NET_CHECK
} else {
    QT += network
}

without_mce_dbus {
    DEFINES += MER_WITHOUT_MCE_DBUS
} else {
    QT += dbus
}
