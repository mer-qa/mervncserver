TEMPLATE = subdirs

src_fb.subdir = src
src_fb.target = sub-src


SUBDIRS = src_fb

OTHER_FILES += \
    rpm/mervncserver.spec \
    data/vnc.socket \
    data/vnc.service

!without_systemd {
    systemd_vnc.files = \
        data/vnc.socket \
        data/vnc.service

    systemd_vnc.path = /lib/systemd/system/

    INSTALLS = systemd_vnc
} else {
    message("systemd support will be deactivated!")
    DEFINES += MER_WITHOUT_SYSTEMD
}

without_network_check {
    message("network interface check support will be deactivated!")
}

without_mce_dbus {
    message("MCE DBus calls will be deactivated!")
}

with_ctrl_b {
    message("Support for CTRL-B will be enabled!")
}
