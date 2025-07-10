TEMPLATE    = app
CONFIG     += console testcase
QT         += core testlib multimedia network

# Ruta a tu código fuente (ajusta si tu estructura es distinta)
INCLUDEPATH += ../

# Ficheros de test
SOURCES += \
    audio_receiver_test.cpp

CONFIG    += link_fftw3
LIBS      += -lfftw3f
PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0 gstreamer-audio-1.0
