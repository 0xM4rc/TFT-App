QT       += core gui multimedia multimediawidgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++20

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    audio_engine.cpp \
    main.cpp \
    mainwindow.cpp \
    src/audio_loader.cpp \
    src/waveformview.cpp

HEADERS += \
    include/audio_engine.h \
    include/audio_loader.h \
    include/data_structures/audio_input_config.h \
    include/data_structures/file_audio_data.h \
    include/data_structures/waveform_config.h \
    include/waveformview.h \
    mainwindow.h

FORMS += \
    mainwindow.ui

TRANSLATIONS += \
    TFT-App_en_GB.ts
CONFIG += lrelease
CONFIG += embed_translations

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
