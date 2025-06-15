QT       += core gui multimedia multimediawidgets widgets concurrent network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++20

# Configuración para pkg-config
CONFIG += link_pkgconfig

# Paquetes de GStreamer
PKGCONFIG += gstreamer-1.0
PKGCONFIG += gstreamer-app-1.0
PKGCONFIG += gstreamer-audio-1.0

# Si quieres desactivar APIs obsoletas antes de Qt 6.0:
# DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    src/audiobuffer.cpp \
    src/audioprocessor.cpp \
    src/audiosource.cpp \
    src/microphone_source.cpp \
    src/networksource.cpp

HEADERS += \
    include/mock_waveformview.h \
    mainwindow.h \
    include/audiobuffer.h \
    include/audiochunk.h \
    include/audioprocessor.h \
    include/interfaces/audio_source.h \
    include/microphone_source.h \
    include/network_source.h

FORMS += \
    mainwindow.ui

TRANSLATIONS += \
    TFT-App_en_GB.ts

CONFIG += lrelease embed_translations

# Deployment
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
