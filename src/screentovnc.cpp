/****************************************************************************
 *
 *  screentovnc.cpp - Mervncserver, a VNC server for Mer devices
 *  Copyright (C) 2014 Jolla Ltd.
 *  Contact: Reto Zingg <reto.zingg@jolla.com>
 *
 *  This file is part of Mervncserver.
 *
 *  Mervncserver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 *  This file incorporates work covered by the following copyright and
 *  permission notice:
 *
 *****************************************************************************
 *
 *  The following functions:
 *  - "void init_fb(void)" from "static void init_fb(void)":
 *    https://code.google.com/p/android-vnc-server/source/browse/trunk/fbvncserver.c#83
 *
 *  - "void cleanup_fb(void)" from "static void cleanup_fb(void)":
 *    https://code.google.com/p/android-vnc-server/source/browse/trunk/fbvncserver.c#118
 *
 *  - "void grapFrame()" from "static void update_screen(void)":
 *    https://code.google.com/p/android-vnc-server/source/browse/trunk/fbvncserver.c#381
 *
 *  are based on fbvncserver.c code, copyright header:
 *
 *    This program is free software; you can redistribute it and/or modify it
 *    under the terms of the GNU General Public License as published by the
 *    Free Software Foundation; either version 2, or (at your option) any
 *    later version.
 *
 *    This program is distributed in the hope that it will be useful, but
 *    WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    General Public License for more details.
 *
 *    This project is an adaptation of the original fbvncserver for the iPAQ
 *    and Zaurus.
 *
 ****************************************************************************
 *
 *  The following functions:
 *  - "makeRichCursor(rfbScreenInfoPtr rfbScreen)" from
 *    "static void MakeRichCursor(rfbScreenInfoPtr rfbScreen)"
 *
 *  - "makeRichCursorTouch(rfbScreenInfoPtr rfbScreen)" from
 *    "static void MakeRichCursor(rfbScreenInfoPtr rfbScreen)"
 *
 *  - "void mouseHandler(int buttonMask, int x, int y, rfbClientPtr cl)"
 *    from "static void doptr(int buttonMask,int x,int y,rfbClientPtr cl)"
 *
 *  - "void clientgone(rfbClientPtr cl)" from
 *    "static void clientgone(rfbClientPtr cl)"
 *
 *  - "rfbNewClientAction newclient(rfbClientPtr cl)" from
 *    "static enum rfbNewClientAction newclient(rfbClientPtr cl)"
 *
 *  are based on the LibVNCServer example.c code, copyright header:
 *
 *    @example example.c
 *    This is an example of how to use libvncserver.
 *
 *    libvncserver example
 *    Copyright (C) 2001 Johannes E. Schindelin <Johannes.Schindelin@gmx.de>
 *
 *    This is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This software is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this software; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *    USA.
 *
 ****************************************************************************/

#include "screentovnc.h"

// TODO: make that configurable
#define POINTER_DELAY 10

// TODO: use mktemp...
#define SC_FILE "/tmp/screen2vnc.ppm"

ScreenToVnc::ScreenToVnc(QObject *parent) :
    QObject(parent)
{
    IN;
    // TODO: make that configurable?
    exitWhenLastClientGone = false;
    isEmptyMouse = false;
    m_fbfd = -1;
    lastPointerEvent = QDateTime::currentMSecsSinceEpoch();
    lastPointerMove = lastPointerEvent;

    // Unix Signal Handling set up
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, unixHupSignalFd))
        qFatal("Couldn't create HUP socketpair");

    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, unixTermSignalFd))
        qFatal("Couldn't create TERM socketpair");

    hupSignalNotifier = new QSocketNotifier(unixHupSignalFd[1], QSocketNotifier::Read, this);
    connect(hupSignalNotifier,
            SIGNAL(activated(int)),
            this,
            SLOT(qtHubSignalHandler()));

    termSignalNotifier = new QSocketNotifier(unixTermSignalFd[1], QSocketNotifier::Read, this);
    connect(termSignalNotifier,
            SIGNAL(activated(int)),
            this,
            SLOT(qtTermSignalHandler()));

    // init the Framebuffer
    init_fb();

    // setup vnc server
    // must run after init_rb, so m_scrinfo and m_xPadding is set!
    char *argv[0];
    // m_server = rfbGetScreen(0,argv,(m_scrinfo.xres + m_xPadding), m_scrinfo.yres, 8, 3, m_scrinfo.bits_per_pixel / 8);
    m_server = rfbGetScreen(0,argv,m_scrinfo.xres,m_scrinfo.yres,8,3,4);

    if(!m_server){
        LOG() << "failed to create VNC server";
    }

    m_server->desktopName = "Mer VNC";
    // m_server->frameBuffer=(char*)malloc((m_scrinfo.xres + m_xPadding)*m_scrinfo.yres*(m_scrinfo.bits_per_pixel / 8));
    m_server->frameBuffer=(char*)malloc(m_scrinfo.xres*m_scrinfo.yres*4);

    m_server->alwaysShared=(1==1);

    m_server->newClientHook = newclient;
    m_server->ptrAddEvent = mouseHandler;

    // check if launched by systemd with a ready socket (LISTEN_FDS env var)
    int sd_fds = sd_listen_fds(1);
    if (sd_fds){
        for (int i = SD_LISTEN_FDS_START; i <= (SD_LISTEN_FDS_START + sd_fds - 1); i++){
            if (sd_is_socket(i, AF_INET6, SOCK_STREAM, 1)
                || sd_is_socket(i, AF_INET, SOCK_STREAM, 1)){
                LOG() << "using given socket at FD:" << i;
                m_server->autoPort = false;
                m_server->port = 0;
                m_server->ipv6port = 0;
                m_server->udpPort = 0;
                m_server->listenSock = i;
                FD_SET(m_server->listenSock, &(m_server->allFds));
                m_server->maxFd = m_server->listenSock;
                exitWhenLastClientGone = true;
            }
        }
    }

    // init the cursors
    init_fingerPointers();
    makeRichCursor(m_server);

    // Initialize the VNC server
    rfbInitServer(m_server);

    // init compare frame buffer
    m_compareFrameBuffer = (unsigned short int *)calloc((m_scrinfo.xres + m_xPadding) * m_scrinfo.yres, (m_scrinfo.bits_per_pixel / 8));

    m_screenshotTimer = new QTimer(this);
//    connect(m_screenshotTimer,
//            SIGNAL(timeout()),
//            this,
//            SLOT(grapFrame()));

    connect(m_screenshotTimer,
            SIGNAL(timeout()),
            this,
            SLOT(shootNow()));

    m_processTimer = new QTimer(this);
    connect(m_processTimer,
            SIGNAL(timeout()),
            this,
            SLOT(rfbProcessTrigger()));

    // open the event device
    // TODO: not Hardcode?
    eventDev = open("/dev/input/event1", O_RDWR);
    if(eventDev < 0) {
        LOG() << "can't open /dev/input/event1";
        return;
    }

    // start the process trigger timers
    m_processTimer->start();
    m_screenshotTimer->start(400);

    // inform systemd that we started up
    sd_notifyf(0, "READY=1\n"
               "STATUS=Processing requests...\n"
               "MAINPID=%lu",
               (unsigned long) getpid());

    OUT;
}

ScreenToVnc::~ScreenToVnc()
{
    IN;
    close(eventDev);
    cleanup_fb();
    free(m_server->frameBuffer);
    rfbScreenCleanup(m_server);
    free(m_compareFrameBuffer);
    OUT;
}

void ScreenToVnc::rfbProcessTrigger()
{
    long usec;
    usec = m_server->deferUpdateTime*1000;
    rfbProcessEvents(m_server,0);
}

/****************************************************************************
 * Framebuffer Functions
 ****************************************************************************/

/****************************************************************************
 * based on:
 * https://code.google.com/p/android-vnc-server/source/browse/trunk/fbvncserver.c#83
 * GPLv2+
 ****************************************************************************/
void ScreenToVnc::init_fb(void)
{
    IN;
    m_fbmmap = (unsigned int*)MAP_FAILED;

    // TODO not hardcode! /dev/fb0 ?
    if ((m_fbfd = open("/dev/fb0", O_RDONLY)) == -1)
    {
        LOG() << "cannot open fb device: /dev/fb0";
        return;
    }

    if (ioctl(m_fbfd, FBIOGET_FSCREENINFO, &m_fix_scrinfo) != 0){
        LOG() << "ioctl error on FBIOGET_FSCREENINFO";
        return;
    }

    LOG() << "fix_scrinfo.line_length:" << m_fix_scrinfo.line_length;
    LOG() << "fix_scrinfo.id" << m_fix_scrinfo.id;
    LOG() << "fix_scrinfo.smem_len:" << m_fix_scrinfo.smem_len;
    LOG() << "fix_scrinfo.type:" << m_fix_scrinfo.type;
    LOG() << "fix_scrinfo.type_aux:" << m_fix_scrinfo.type_aux;
    LOG() << "fix_scrinfo.xpanstep:" << m_fix_scrinfo.xpanstep;
    LOG() << "fix_scrinfo.ypanstep:" << m_fix_scrinfo.ypanstep;
    LOG() << "fix_scrinfo.ywrapstep:" << m_fix_scrinfo.ywrapstep;
    LOG() << "fix_scrinfo.mmio_len:" << m_fix_scrinfo.mmio_len;
    LOG() << "fix_scrinfo.capabilities:" << m_fix_scrinfo.capabilities;

    if (ioctl(m_fbfd, FBIOGET_VSCREENINFO, &m_scrinfo) != 0){
        LOG() << "ioctl error on FBIOGET_VSCREENINFO";
        return;
    }

    LOG() << "scrinfo.xres"           << m_scrinfo.xres;
    LOG() << "scrinfo.yres"           << m_scrinfo.yres;
    LOG() << "scrinfo.xres_virtual"   << m_scrinfo.xres_virtual;
    LOG() << "scrinfo.yres_virtual"   << m_scrinfo.yres_virtual;
    LOG() << "scrinfo.xoffset"        << m_scrinfo.xoffset;
    LOG() << "scrinfo.yoffset"        << m_scrinfo.yoffset;
    LOG() << "scrinfo.bits_per_pixel" << m_scrinfo.bits_per_pixel;
    LOG() << "m_scrinfo.right_margin" << m_scrinfo.right_margin;
    LOG() << "m_scrinfo.left_margin"  << m_scrinfo.left_margin;
    LOG() << "m_scrinfo.vmode"        << m_scrinfo.vmode;
    LOG() << "m_scrinfo.rotate"       << m_scrinfo.rotate;

    m_xPadding = (m_fix_scrinfo.line_length / (m_scrinfo.bits_per_pixel / 8) ) - m_scrinfo.xres;

    m_fbmmap = (unsigned int*)mmap(NULL, m_fix_scrinfo.smem_len, PROT_READ, MAP_SHARED, m_fbfd, 0);

    if (m_fbmmap == (unsigned int*)MAP_FAILED){
        LOG() << "mmap failed";
        return;
    }
}

/****************************************************************************
 * based on:
 * https://code.google.com/p/android-vnc-server/source/browse/trunk/fbvncserver.c#118
 * GPLv2+
 ****************************************************************************/
void ScreenToVnc::cleanup_fb(void)
{
    IN;
    if(m_fbfd != -1){
        close(m_fbfd);
    }
}


/****************************************************************************
 * based on:
 * https://code.google.com/p/android-vnc-server/source/browse/trunk/fbvncserver.c#381
 * GPLv2+
 ****************************************************************************/
void ScreenToVnc::grapFrame()
{
    IN;
    if (rfbIsActive(m_server) && m_server->clientHead != NULL){
        unsigned int *f, *c, *r;
        int x, y;

        int min_x = 99999;
        int min_y = 99999;
        int max_x = -1;
        int max_y = -1;
        bool bufferChanged = false;

        f = (unsigned int *)m_fbmmap;                 /* -> framebuffer         */
        c = (unsigned int *)m_compareFrameBuffer;     /* -> compare framebuffer */
        r = (unsigned int *)m_server->frameBuffer;    /* -> remote framebuffer  */

        struct fb_var_screeninfo scrinfo;
        ioctl(m_fbfd, FBIOGET_VSCREENINFO, &scrinfo);
        int offset = (scrinfo.xres + m_xPadding) * scrinfo.yoffset;

        for (y = 0; y < m_scrinfo.yres; y++)
        {
            for (x = 0; x < (m_scrinfo.xres + m_xPadding); x++)
            {
                unsigned int pixel = *(f + offset);

                if (pixel != *c)
                {
                    *c = pixel;
                    *r = pixel;
                    bufferChanged = true;

                    if (x < min_x)
                        min_x = x;

                    if (y < min_y)
                        min_y = y;

                    if (x > max_x)
                        max_x = x;

                    if (y > max_y)
                        max_y = y;
                }
                f++, c++;
                r++;
            }
        }

        if (bufferChanged)
        {
//            LOG() << "Dirty page:" << min_x << "x"
//                  << min_y << "x"
//                  << max_x << "x"
//                  << max_y;

            // TODO: somewhere we are off by one?
            rfbMarkRectAsModified(m_server, min_x, min_y, max_x+1, max_y+1);
        }

        // TODO: make the 500ms configurable?!
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (!isEmptyMouse && now - lastPointerMove > 500) {
            makeEmptyMouse(m_server);
        }
    }
}


/****************************************************************************
 * Mouse Pointer Functions
 ****************************************************************************/

void ScreenToVnc::init_fingerPointers()
{
    pointerFingerPtr = rfbMakeXCursor(pointer_finger.width,
                                      pointer_finger.height,
                                      pointer_finger.bitmask,
                                      pointer_finger.bitmask);

    pointerFingerTouchPtr = rfbMakeXCursor(pointer_finger_touch.width,
                                           pointer_finger_touch.height,
                                           pointer_finger_touch.bitmask,
                                           pointer_finger_touch.bitmask);

    emptyMousePtr = rfbMakeXCursor(empty_mouse.width,
                                   empty_mouse.height,
                                   empty_mouse.bitmask,
                                   empty_mouse.bitmask);
}

void ScreenToVnc::mceUnblank()
{
    IN;
    QDBusConnection bus = QDBusConnection::systemBus();
    QDBusInterface dbus_iface("com.nokia.mce",
                              "/com/nokia/mce/request",
                              "com.nokia.mce.request",
                              bus);

    dbus_iface.call("req_display_state_on");
}

/******************************************************************
 * The functions:
 * - makeEmptyMouse(rfbScreenInfoPtr rfbScreen)
 * - makeRichCursor(rfbScreenInfoPtr rfbScreen)
 * - makeRichCursorTouch(rfbScreenInfoPtr rfbScreen)
 *
 * are based on the example.c example from (GPLv2+)
 * LibVNCServer: http://libvncserver.sourceforge.net/
 ******************************************************************/
void ScreenToVnc::makeEmptyMouse(rfbScreenInfoPtr rfbScreen)
{
    rfbScreen->cursor = emptyMousePtr;
    rfbScreen->cursor->richSource = empty_mouse.pixel_data;

    rfbScreen->cursor->xhot = 1;
    rfbScreen->cursor->yhot = 1;

    updateClientCursors(rfbScreen, true);
}

void ScreenToVnc::makeRichCursor(rfbScreenInfoPtr rfbScreen)
{
    rfbScreen->cursor = pointerFingerPtr;
    rfbScreen->cursor->richSource = pointer_finger.pixel_data;

    rfbScreen->cursor->xhot = 32;
    rfbScreen->cursor->yhot = 32;

    updateClientCursors(rfbScreen, false);
}

void ScreenToVnc::makeRichCursorTouch(rfbScreenInfoPtr rfbScreen)
{
    rfbScreen->cursor = pointerFingerTouchPtr;
    rfbScreen->cursor->richSource = pointer_finger_touch.pixel_data;

    rfbScreen->cursor->xhot = 32;
    rfbScreen->cursor->yhot = 32;

    updateClientCursors(rfbScreen, false);
}

void ScreenToVnc::updateClientCursors(rfbScreenInfoPtr rfbScreen, bool emptyMouse)
{
    rfbClientIteratorPtr iter;
    rfbClientPtr cl;

    iter = rfbGetClientIterator(rfbScreen);
    while( (cl = rfbClientIteratorNext(iter)) ) {
        cl->cursorWasChanged = true;
    }
    rfbReleaseClientIterator(iter);

    isEmptyMouse = emptyMouse;
}

/****************************************************************************
 *
 * buttonMask: bits 0-7 are buttons 1-8, 0=up, 1=down
 * button 1: left    : bit 1 -> 1
 * button 2: middle  : bit 2 -> 2
 * button 3: right   : bit 3 -> 4
 *
 * inspired by:
 * doptr(int buttonMask,int x,int y,rfbClientPtr cl)
 * from example.c example from (GPLv2+)
 * LibVNCServer: http://libvncserver.sourceforge.net/
 *
 ****************************************************************************/

void ScreenToVnc::mouseHandler(int buttonMask, int x, int y, rfbClientPtr cl)
{
    ClientData* cd=(ClientData*)cl->clientData;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    lastPointerMove = now;
    int nextClientId = 0;

    // TODO: smarter way to dedect if in dragMode or not
    switch (buttonMask){
    case 0: /*all buttons up */
        if (cd->dragMode){
            struct input_event event_mt_report,event_end,event_mt_tracking_id;
            memset(&event_mt_report, 0, sizeof(event_mt_report));
            memset(&event_end, 0, sizeof(event_end));
            memset(&event_mt_tracking_id, 0, sizeof(event_mt_tracking_id));

            event_mt_report.type = EV_SYN;
            event_mt_report.code = SYN_MT_REPORT;
            event_mt_report.value = 0;

            event_end.type = EV_SYN;
            event_end.code = SYN_REPORT;
            event_end.value = 0;

            event_mt_tracking_id.type = EV_ABS;
            event_mt_tracking_id.code = ABS_MT_TRACKING_ID;
            event_mt_tracking_id.value = 0xffffffff;

//            if(write(eventDev, &event_mt_report, sizeof(event_mt_report)) < sizeof(event_mt_report)) {
//                LOG() << "write event_mt_report failed: " << strerror(errno);
//                return;
//            }


            if(write(eventDev, &event_mt_tracking_id, sizeof(event_mt_tracking_id)) < sizeof(event_mt_tracking_id)) {
                LOG() << "write event_mt_tracking_id failed: " << strerror(errno);
                return;
            }

            if(write(eventDev, &event_end, sizeof(event_end)) < sizeof(event_end)) {
                LOG() << "write event_end failed: " << strerror(errno);
                return;
            }

            rfbDefaultPtrAddEvent(buttonMask,x,y,cl);
            cd->dragMode = false;
        }
        makeRichCursor(cl->screen);
        break;
    case 1: /* left button down */
        if(x>=0 && y>=0 && x< cl->screen->width && y< cl->screen->height && now - lastPointerEvent > POINTER_DELAY) {
            struct input_event event_x, event_y, event_pressure, event_mt_report, event_end, event_mt_tracking_id, event_mt_touch_major;
            memset(&event_x, 0, sizeof(event_x));
            memset(&event_y, 0, sizeof(event_y));
            memset(&event_pressure, 0, sizeof(event_pressure));
            memset(&event_mt_report, 0, sizeof(event_mt_report));
            memset(&event_end, 0, sizeof(event_end));
            memset(&event_mt_tracking_id, 0, sizeof(event_mt_tracking_id));
            memset(&event_mt_touch_major, 0, sizeof(event_mt_touch_major));

            nextClientId = cd->eventId + 1;

            event_x.type = EV_ABS;
            event_x.code = ABS_MT_POSITION_X;
            event_x.value = x;

            event_y.type = EV_ABS;
            event_y.code = ABS_MT_POSITION_Y;
            event_y.value = y;

            event_pressure.type = EV_ABS;
            event_pressure.code = ABS_MT_PRESSURE;
            event_pressure.value = 0x33;

            event_mt_report.type = EV_SYN;
            event_mt_report.code = SYN_MT_REPORT;
            event_mt_report.value = 0;

            event_end.type = EV_SYN;
            event_end.code = SYN_REPORT;
            event_end.value = 0;

            event_mt_tracking_id.type = EV_ABS;
            event_mt_tracking_id.code = ABS_MT_TRACKING_ID;
            event_mt_tracking_id.value = nextClientId;

            event_mt_touch_major.type = EV_ABS;
            event_mt_touch_major.code = ABS_MT_TOUCH_MAJOR;
            event_mt_touch_major.value = 0x6;

            if(!cd->dragMode){
                if(write(eventDev, &event_mt_tracking_id, sizeof(event_mt_tracking_id)) < sizeof(event_mt_tracking_id)) {
                    LOG() << "write event_mt_tracking_id failed: " << strerror(errno);
                    return;
                }
            }

            if(write(eventDev, &event_x, sizeof(event_x)) < sizeof(event_x)) {
                LOG() << "write event_x failed: " << strerror(errno);
                return;
            }

            if(write(eventDev, &event_y, sizeof(event_y)) < sizeof(event_y)) {
                LOG() << "write event_y failed: " << strerror(errno);
                return;
            }

            if(write(eventDev, &event_pressure, sizeof(event_pressure)) < sizeof(event_pressure)) {
                LOG() << "write event_pressure failed: " << strerror(errno);
                return;
            }

//            if(write(eventDev, &event_mt_report, sizeof(event_mt_report)) < sizeof(event_mt_report)) {
//                LOG() << "write event_mt_report failed: " << strerror(errno);
//                return;
//            }

            if(write(eventDev, &event_mt_touch_major, sizeof(event_mt_touch_major)) < sizeof(event_mt_touch_major)) {
                LOG() << "write event_mt_touch_major failed: " << strerror(errno);
                return;
            }

            if(write(eventDev, &event_end, sizeof(event_end)) < sizeof(event_end)) {
                LOG() << "write event_end failed: " << strerror(errno);
                return;
            }

            makeRichCursorTouch(cl->screen);
            rfbDefaultPtrAddEvent(buttonMask,x,y,cl);
            cd->dragMode = true;
            lastPointerEvent = QDateTime::currentMSecsSinceEpoch();
            cd->eventId = nextClientId;
        }
        break;
    case 4: /* right button down */
        if(x>=0 && y>=0 && x< cl->screen->width && y< cl->screen->height && now - lastPointerEvent > POINTER_DELAY) {
            mceUnblank();
        }
        break;
    default:
        makeRichCursor(cl->screen);
        break;
    }

    cd->oldx=x;
    cd->oldy=y;
    cd->oldButton=buttonMask;
}

/****************************************************************************
 * Client Handling Functions
 ****************************************************************************/
/****************************************************************************
 * The functions:
 * - clientgone(rfbClientPtr cl)
 * - newclient(rfbClientPtr cl)
 *
 * are based on the example.c example from
 * LibVNCServer: http://libvncserver.sourceforge.net/
 ****************************************************************************/
void ScreenToVnc::clientgone(rfbClientPtr cl)
{
    IN;

    rfbScreenInfoPtr screen = cl->screen;
    free(cl->clientData);

    if (screen->clientHead == NULL && exitWhenLastClientGone){
        QCoreApplication::exit(0);
    }

}

rfbNewClientAction ScreenToVnc::newclient(rfbClientPtr cl)
{
    IN;

    bool allowConnection = false;

    // TODO: make that configurable, usb device interface is not always rndis0!
    QNetworkInterface usbIf = QNetworkInterface::interfaceFromName("rndis0");

    QHostAddress remoteAddr = QHostAddress(QString::fromLatin1(cl->host));

    if (remoteAddr.protocol() == QAbstractSocket::IPv6Protocol
        && remoteAddr.toString().startsWith("::ffff:")){
        // this is an IPv4-mapped IPv6 address
        // see: http://www.tcpipguide.com/free/t_IPv6IPv4AddressEmbedding-2.htm
        QString remoteAddrIPv4 = remoteAddr.toString().remove("::ffff:");
        LOG() << "remoteAddrIPv4" << remoteAddrIPv4;
        remoteAddr = QHostAddress(remoteAddrIPv4);
    }

    foreach (QNetworkAddressEntry entry, usbIf.addressEntries()){
        if (remoteAddr.protocol() == entry.ip().protocol()
            && remoteAddr.isInSubnet(entry.ip(), entry.prefixLength())){
            allowConnection = true;
        }
    }

    if (allowConnection){
        cl->clientData = (void*)calloc(sizeof(ClientData),1);
        ClientData* cd=(ClientData*)cl->clientData;
        cd->dragMode = false;
        cd->eventId = 0;
        cl->clientGoneHook = clientgone;
        return RFB_CLIENT_ACCEPT;
    } else {
        LOG() << "RFB_CLIENT_REFUSE";
        cl->clientGoneHook = clientgone;
        return RFB_CLIENT_REFUSE;
    }
}

/****************************************************************************
 * Unix Signal Handler Functions
 ****************************************************************************/
int ScreenToVnc::unixHupSignalFd[2];
int ScreenToVnc::unixTermSignalFd[2];

void ScreenToVnc::unixHupSignalHandler(int)
{
    IN;
    LOG() << "HUP Signal received";
    char a = '1';
    ::write(unixHupSignalFd[0], &a, sizeof(a));
}

void ScreenToVnc::unixTermSignalHandler(int)
{
    IN;
    LOG() << "TERM Signal received";
    char a = '2';
    ::write(unixTermSignalFd[0], &a, sizeof(a));
}

void ScreenToVnc::qtTermSignalHandler()
{
    IN;
    termSignalNotifier->setEnabled(false);
    char tmp;
    ::read(unixTermSignalFd[1], &tmp, sizeof(tmp));

    LOG() << "TERM Signal received, about to exit...";
    m_screenshotTimer->stop();
    m_processTimer->stop();

    termSignalNotifier->setEnabled(true);

    QCoreApplication::exit(0);
}

void ScreenToVnc::qtHubSignalHandler()
{
    IN;
    hupSignalNotifier->setEnabled(false);
    char tmp;
    ::read(unixHupSignalFd[1], &tmp, sizeof(tmp));

    LOG() << "HUP Signal received, currently do nothing...";

    hupSignalNotifier->setEnabled(true);
}

/****************************************************************************
 * Screen-Shot Functions
 ****************************************************************************/

void ScreenToVnc::shootNow()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface dbus_iface("org.nemomobile.lipstick", "/org/nemomobile/lipstick/screenshot",
                              "org.nemomobile.lipstick", bus);

    dbus_iface.call("saveScreenshot", SC_FILE);

    if (rfbIsActive(m_server)){
        if (!takePicture((unsigned char *)m_server->frameBuffer)){
            rfbMarkRectAsModified(m_server,0,0,m_scrinfo.xres,m_scrinfo.yres);
        }
    }
}

/******************************************************************
 * This function is based on the pnmshow.c example from
 * LibVNCServer: http://libvncserver.sourceforge.net/
 ******************************************************************/

int ScreenToVnc::takePicture(unsigned char *serverBuffer)
{
    FILE* in=stdin;
    int i,j,k,l,width,height,paddedWidth;
    char buffer[1024];
    enum { BW, GRAY, TRUECOLOUR } picType=TRUECOLOUR;
    int bitsPerPixelInFile;

    in=fopen(SC_FILE,"rb");
    if(!in) {
        LOG() << "Couldn't find file:" << SC_FILE;
        return 1;
    }

    fgets(buffer,1024,in);
    if(!strncmp(buffer,"P6",2)) {
        picType=TRUECOLOUR;
        bitsPerPixelInFile=3*8;
    } else if(!strncmp(buffer,"P5",2)) {
        picType=GRAY;
        bitsPerPixelInFile=1*8;
    } else if(!strncmp(buffer,"P4",2)) {
        picType=BW;
        bitsPerPixelInFile=1;
    } else {
        LOG() << "Not a ppm.";
        return 2;
    }

    /* skip comments */
    do {
        fgets(buffer,1024,in);
    } while(buffer[0]=='#');

    /* get width & height */
    sscanf(buffer,"%d %d",&width,&height);
    // rfbLog("Got width %d and height %d.\n",width,height);
    // LOG() << "Got width" << width << "and height" << height;
    if(picType!=BW)
        fgets(buffer,1024,in);
    else
        width=1+((width-1)|7);

    /* vncviewers have problems with widths which are no multiple of 4. */
    paddedWidth = width;
    if(width&3)
        paddedWidth+=4-(width&3);

    fread(serverBuffer,width*bitsPerPixelInFile/8,height,in);
    fclose(in);

    switch(picType) {
    case TRUECOLOUR:
        /* correct the format to 4 bytes instead of 3 (and pad to paddedWidth) */
        for(j=height-1;j>=0;j--) {
            for(i=width-1;i>=0;i--)
                for(k=2;k>=0;k--)
                    serverBuffer[(j*paddedWidth+i)*4+k]=
                            serverBuffer[(j*width+i)*3+k];
            for(i=width*4;i<paddedWidth*4;i++)
                serverBuffer[j*paddedWidth*4+i]=0;
        }
        break;
    case GRAY:
        break;
    case BW:
        /* correct the format from 1 bit to 8 bits */
        for(j=height-1;j>=0;j--)
            for(i=width-1;i>=0;i-=8) {
                l=(unsigned char)serverBuffer[(j*width+i)/8];
                for(k=7;k>=0;k--)
                    serverBuffer[j*paddedWidth+i+7-k]=(l&(1<<k))?0:255;
            }
        break;
    }

    /* success!   We have a new picture! */
    return 0;
}
