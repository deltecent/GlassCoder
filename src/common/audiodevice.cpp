// audiodevice.cpp
//
// Abstract base class for audio input sources.
//
//   (C) Copyright 2014-2015 Fred Gleason <fredg@paravelsystems.com>
//
//   This program is free software; you can redistribute it and/or modify
//   it under the terms of the GNU General Public License version 2 as
//   published by the Free Software Foundation.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public
//   License along with this program; if not, write to the Free Software
//   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#include <string.h>
#include <syslog.h>

#include <samplerate.h>

#include "audiodevice.h"

AudioDevice::AudioDevice(unsigned chans,unsigned samprate,
			 std::vector<Ringbuffer *> *rings,QObject *parent)
  : QObject(parent)
{
  audio_rings=rings;
  audio_channels=chans;
  audio_samplerate=samprate;
}


AudioDevice::~AudioDevice()
{
}


unsigned AudioDevice::deviceSamplerate() const
{
  return audio_samplerate;
}


QString AudioDevice::deviceTypeText(AudioDevice::DeviceType type)
{
  QString ret=tr("Unknown Device");

  switch(type) {
  case AudioDevice::Alsa:
    ret=tr("Advanced Linux Sound Architecture (ALSA)");
    break;

  case AudioDevice::AsiHpi:
    ret=tr("AudioScience HPI");
    break;

  case AudioDevice::File:
    ret=tr("File Streaming");
    break;

  case AudioDevice::Jack:
    ret=tr("JACK Audio Connection Kit");
    break;

  case AudioDevice::LastType:
    break;
  }

  return ret;
}


QString AudioDevice::optionKeyword(AudioDevice::DeviceType type)
{
  QString ret;

  switch(type) {
  case AudioDevice::Alsa:
    ret="alsa";
    break;

  case AudioDevice::AsiHpi:
    ret="asihpi";
    break;

  case AudioDevice::File:
    ret="file";
    break;

  case AudioDevice::Jack:
    ret="jack";
    break;

  case AudioDevice::LastType:
    break;
  }

  return ret;
}


QString AudioDevice::formatString(AudioDevice::Format fmt)
{
  QString ret="UNKNOWN";

  switch(fmt) {
  case AudioDevice::FLOAT:
    ret="FLOAT";
    break;

  case AudioDevice::S16_LE:
    ret="S16_LE";
    break;

  case AudioDevice::S32_LE:
    ret="S32_LE";
    break;

  case AudioDevice::LastFormat:
    break;
  }

  return ret;
}


unsigned AudioDevice::ringBufferQuantity() const
{
  return audio_rings->size();
}


Ringbuffer *AudioDevice::ringBuffer(unsigned n)
{
  return audio_rings->at(n);
}


unsigned AudioDevice::channels() const
{
  return audio_channels;
}


unsigned AudioDevice::samplerate() const
{
  return audio_samplerate;
}


void AudioDevice::remixChannels(float *pcm_out,unsigned chans_out,float *pcm_in,
				unsigned chans_in,unsigned nframes)
{
  if(chans_out==chans_in) {
    memcpy(pcm_out,pcm_in,nframes*chans_in*sizeof(float));
    return;
  }
  if((chans_in==1)&&(chans_out==2)) {
    for(unsigned i=0;i<nframes;i++) {
      pcm_out[2*i]=pcm_in[i];
      pcm_out[2*i+1]=pcm_in[i];
    }
    return;
  }
  if((chans_in==2)&&(chans_out==1)) {
    for(unsigned i=0;i<nframes;i++) {
      pcm_out[i]=(pcm_in[2*i]+pcm_in[2*i+1])/2.0;
    }
    return;
  }
  syslog(LOG_ERR,"invalid channel remix: chans_in: %d  chans_out: %d",
	 chans_in,chans_out);
  exit(256);
}


void AudioDevice::convertToFloat(float *pcm_out,const void *pcm_in,
				 Format fmt_in,unsigned nframes,unsigned chans)
{
  switch(fmt_in) {
  case AudioDevice::FLOAT:
    break;

  case AudioDevice::S16_LE:
    src_short_to_float_array((const short *)pcm_in,pcm_out,nframes*chans);
    break;

  case AudioDevice::S32_LE:
    src_int_to_float_array((const int *)pcm_in,pcm_out,nframes*chans);
    break;

  case AudioDevice::LastFormat:
    break;
  }
}