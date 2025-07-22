QT += core gui widgets testlib
CONFIG += c++20 console
CONFIG -= app_bundle

TARGET = spectrogram_test
TEMPLATE = app

# Archivos fuente
SOURCES += \
    tests/spectrogram_test.cpp \
    core/spectrogram_calculator.cpp

HEADERS += \
    core/spectrogram_calculator.h

# FFTW library
LIBS += -lfftw3f

# Directorio de salida
DESTDIR = build
