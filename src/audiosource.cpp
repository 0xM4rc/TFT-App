# include "include/interfaces/audio_source.h"

AudioSource::AudioSource(QObject *parent)
    : QObject(parent), m_active(false)
{
}
