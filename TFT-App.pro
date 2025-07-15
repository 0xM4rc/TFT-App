QT       += core gui multimedia multimediawidgets widgets concurrent network charts sql

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++20 link_fftw3
LIBS += -lfftw3f

# Configuración para pkg-config
CONFIG += link_pkgconfig

# Paquetes de GStreamer
PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0 gstreamer-audio-1.0


# Si quieres desactivar APIs obsoletas antes de Qt 6.0:
# DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000

SOURCES += \
    core/audio_db.cpp \
    receivers/audio_receiver.cpp \
    core/dsp_worker.cpp \
    main.cpp \
    main_moc.cpp \
    gui/mainwindow.cpp \
    receivers/network_receiver.cpp \
    views/spectrogram_renderer.cpp \
    views/waveform_render.cpp
    #tests/audio_processor_test.cpp \

HEADERS += \
    core/audio_db.h \
    receivers/audio_receiver.h \
    core/dsp_worker.h \
    core/ireceiver.h \
    models/audio_block_model.h \
    models/peak_model.h \
    models/spectrogram_model.h \
    gui/mainwindow.h \
    receivers/network_receiver.h \
    views/spectrogram_renderer.h \
    views/waveform_render.h

FORMS += \
    mainwindow.ui

TRANSLATIONS += \
    TFT-App_en_GB.ts

CONFIG += lrelease embed_translations

# Deployment
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES +=

SUBDIRS += \
    tests/libcore/tests.pro \
    tests/libcore/tests.pro
