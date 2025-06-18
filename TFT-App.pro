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
    src/audio_manager.cpp \
    src/audio_processor.cpp \
    src/audio_visualizer.cpp \
    src/audiosource.cpp \
    src/gui/control_panel.cpp \
    src/gui/rt_mainwindow.cpp \
    src/microphone_source.cpp \
    src/networksource.cpp \
    src/source_controller.cpp \
    src/spectrogram_widget.cpp \
    src/waveform_widget.cpp \
    tests/audio_tester.cpp

HEADERS += \
    include/audio_manager.h \
    include/audio_processor.h \
    include/audio_visualizer.h \
    include/data_structures/visualization_data.h \
    include/gui/control_panel.h \
    include/gui/rt_mainwindow.h \
    include/source_controller.h \
    include/spectrogram_widget.h \
    include/waveform_widget.h \
    mainwindow.h \
    include/audiochunk.h \
    include/interfaces/audio_source.h \
    include/microphone_source.h \
    include/network_source.h \
    tests/audio_tester.h

FORMS += \
    mainwindow.ui

TRANSLATIONS += \
    TFT-App_en_GB.ts

CONFIG += lrelease embed_translations

# Deployment
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
