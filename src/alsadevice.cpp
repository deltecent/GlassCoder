// alsadevice.cpp
//
// Audio source for the Advance Linux Sound Architecture
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

#include <syslog.h>

#include "alsadevice.h"

void *AlsaCallback(void *ptr)
{
  static AlsaDevice *dev=(AlsaDevice *)ptr;
  static int n;
  static float pcm1[32768];
  static float pcm2[32768];

  while(1==1) {
    n=snd_pcm_readi(dev->alsa_pcm,dev->alsa_pcm_buffer,
		    dev->alsa_buffer_size/(dev->alsa_period_quantity*2));
    if((snd_pcm_state(dev->alsa_pcm)!=SND_PCM_STATE_RUNNING)&&(n<0)) {
      snd_pcm_drop(dev->alsa_pcm);
      snd_pcm_prepare(dev->alsa_pcm);
      if(n==-EPIPE) {
	syslog(LOG_INFO,"****** ALSA Capture Xrun ******");
      }
      else {
	syslog(LOG_INFO,"ALSA Error [%s]",snd_strerror(n));
      }
    }
    else {
      if(dev->alsa_format!=AudioDevice::FLOAT) {
	dev->convertToFloat(pcm1,dev->alsa_pcm_buffer,dev->alsa_format,n,
			    dev->alsa_channels);
      }
      if(dev->alsa_channels==dev->channels()) {
	for(unsigned i=0;i<dev->ringBufferQuantity();i++) {
	  dev->ringBuffer(i)->write(pcm1,n);
	}
      }
      else {
	dev->remixChannels(pcm2,dev->channels(),pcm1,dev->alsa_channels,n);
	for(unsigned i=0;i<dev->ringBufferQuantity();i++) {
	  dev->ringBuffer(i)->write(pcm2,n);
	}
      }
    }
  }
  return NULL;
}


AlsaDevice::AlsaDevice(unsigned chans,unsigned samprate,
		       std::vector<Ringbuffer *> *rings,QObject *parent)
  : AudioDevice(chans,samprate,rings,parent)
{
#ifdef ALSA
  alsa_device=ALSA_DEFAULT_DEVICE;
  alsa_pcm_buffer=NULL;
#endif  // ALSA
}


AlsaDevice::~AlsaDevice()
{
#ifdef ALSA
  if(alsa_pcm_buffer!=NULL) {
    delete alsa_pcm_buffer;
  }
#endif  // ALSA
}


bool AlsaDevice::processOptions(QString *err,const QStringList &keys,
				const QStringList &values)
{
#ifdef ALSA
  for(int i=0;i<keys.size();i++) {
    bool processed=false;
    if(keys[i]=="--alsa-device") {
      alsa_device=values[i];
      processed=true;
    }
    if(!processed) {
      *err=tr("unrecognized option")+" "+keys[i]+"\"";
      return false;
    }
  }
  return true;
#else
  return false;
#endif  // ALSA
}


bool AlsaDevice::start(QString *err)
{
#ifdef ALSA
  snd_pcm_hw_params_t *hwparams;
  snd_pcm_sw_params_t *swparams;
  int dir;
  int aerr;
  pthread_attr_t pthread_attr;

  if(snd_pcm_open(&alsa_pcm,alsa_device.toUtf8(),SND_PCM_STREAM_CAPTURE,0)!=0) {
    *err=tr("unable to open ALSA device")+" \""+alsa_device+"\"";
    return false;
  }
  snd_pcm_hw_params_alloca(&hwparams);
  snd_pcm_hw_params_any(alsa_pcm,hwparams);

  //
  // Access Type
  //
  if(snd_pcm_hw_params_test_access(alsa_pcm,hwparams,
				   SND_PCM_ACCESS_RW_INTERLEAVED)<0) {
    *err=tr("interleaved access not supported");
    return false;
  }
  snd_pcm_hw_params_set_access(alsa_pcm,hwparams,SND_PCM_ACCESS_RW_INTERLEAVED);

  //
  // Sample Format
  //
  if(snd_pcm_hw_params_test_format(alsa_pcm,hwparams,SND_PCM_FORMAT_FLOAT)==0) {
    alsa_format=AudioDevice::FLOAT;
  }
  else {
    if(snd_pcm_hw_params_test_format(alsa_pcm,hwparams,
				     SND_PCM_FORMAT_S16_LE)==0) {
      alsa_format=AudioDevice::S16_LE;
    }
    else {
      *err=tr("incompatible sample format");
      return false;
    }
  }
  //  printf("format: %s\n",(const char *)AudioDevice::formatString(alsa_format).toUtf8());

  //
  // Sample Rate
  //
  alsa_samplerate=samplerate();
  snd_pcm_hw_params_set_rate_near(alsa_pcm,hwparams,&alsa_samplerate,&dir);
  if(alsa_samplerate!=samplerate()) {
    syslog(LOG_DEBUG,"using ALSA sample rate of %u",alsa_samplerate);
  }

  //
  // Channels
  //
  alsa_channels=channels();
  snd_pcm_hw_params_set_channels_near(alsa_pcm,hwparams,&alsa_channels);
  if(alsa_channels!=channels()) {
    syslog(LOG_DEBUG,"using ALSA channel count of %u",alsa_channels);
  }

  //
  // Buffer Parameters
  //
  alsa_period_quantity=ALSA_PERIOD_QUANTITY;
  snd_pcm_hw_params_set_periods_near(alsa_pcm,hwparams,&alsa_period_quantity,
				     &dir);
  if(alsa_period_quantity!=ALSA_PERIOD_QUANTITY) {
    syslog(LOG_DEBUG,"using ALSA period quantity of %u",alsa_period_quantity);
  }
  //  alsa_buffer_size=ALSA_PERIOD_SIZE*alsa_period_quantity;
  alsa_buffer_size=alsa_samplerate/2;
  snd_pcm_hw_params_set_buffer_size_near(alsa_pcm,hwparams,&alsa_buffer_size);
  if(alsa_buffer_size!=(alsa_samplerate/2)) {
    syslog(LOG_DEBUG,"using ALSA buffer size of %lu",alsa_buffer_size);
  }

  //
  // Fire It Up
  //
  if((aerr=snd_pcm_hw_params(alsa_pcm,hwparams))<0) {
    *err=tr("ALSA device error 1")+": "+snd_strerror(aerr);
    printf("err: %d\n",aerr);
    return false;
  }
  alsa_pcm_buffer=new float[alsa_buffer_size*alsa_channels];

  //
  // Set Wake-up Timing
  //
  snd_pcm_sw_params_alloca(&swparams);
  snd_pcm_sw_params_current(alsa_pcm,swparams);
  snd_pcm_sw_params_set_avail_min(alsa_pcm,swparams,alsa_buffer_size/2);
  if((aerr=snd_pcm_sw_params(alsa_pcm,swparams))<0) {
    *err=tr("ALSA device error 2")+": "+snd_strerror(aerr);
    return false;
  }

  //
  // Start the Callback
  //
  pthread_attr_init(&pthread_attr);

//  if(use_realtime) {
//    pthread_attr_setschedpolicy(&pthread_attr,SCHED_FIFO);

  pthread_create(&alsa_pthread,&pthread_attr,AlsaCallback,this);

  return true;
#else
  return false;
#endif  // ALSA
}


unsigned AlsaDevice::deviceSamplerate() const
{
#ifdef ALSA
  return alsa_samplerate;
#else
  return DEFAULT_SAMPLERATE;
#endif  // ALSA
}