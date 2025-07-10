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
    audio_db.cpp \
    audio_receiver.cpp \
    dsp_worker.cpp \
    main.cpp \
    main_moc.cpp \
    mainwindow.cpp \
    network_receiver.cpp \
    realtime_spectrogram.cpp \
    spectrogram_calculator.cpp \
    spectrogram_renderer.cpp \
    waveform_render.cpp
    #tests/audio_processor_test.cpp \

HEADERS += \
    audio_db.h \
    audio_receiver.h \
    dsp_worker.h \
    ireceiver.h \
    libmodels/audio_block_model.h \
    libmodels/peak_model.h \
    libmodels/spectrogram_model.h \
    mainwindow.h \
    network_receiver.h \
    realtime_spectrogram.h \
    spectrogram_calculator.h \
    spectrogram_renderer.h \
    waveform_render.h

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
