/***************************************************************************
 *                                                                         *
 *   LinuxSampler - modular, streaming capable sampler                     *
 *                                                                         *
 *   Copyright (C) 2003 by Benno Senoner and Christian Schoenebeck         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the Free Software           *
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston,                 *
 *   MA  02111-1307  USA                                                   *
 ***************************************************************************/

#ifndef __AUDIO_H__
#define __AUDIO_H__

// We only support Alsa at the moment

#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <alsa/asoundlib.h>
#include <sched.h>
#include <sys/mman.h>

#include "global.h"

class AudioIO {
    public:
        bool     Initialized;
        int16_t* pOutputBuffer;  ///< This is the buffer where the final mix will be copied to and send to the sound card
        uint     Channels;
        uint     Samplerate;
        uint     Fragments;
        uint     FragmentSize;   ///< in sample points

        AudioIO();
       ~AudioIO();
        int  Initialize(uint channels, uint samplerate, uint numfragments, uint fragmentsize);
        int  Output();
        void Close();
    private:
        typedef std::string  String;

        String               pcm_name;    ///< Name of the PCM device, like plughw:0,0 the first number is the number of the soundcard, the second number is the number of the device.
        snd_pcm_t*           pcm_handle;  ///< Handle for the PCM device
        snd_pcm_stream_t     stream;
        snd_pcm_hw_params_t* hwparams;    ///< This structure contains information about the hardware and can be used to specify the configuration to be used for the PCM stream.
        snd_pcm_sw_params_t* swparams;

        bool HardwareParametersSupported(uint channels, int samplerate, uint numfragments, uint fragmentsize);
};

#endif // __AUDIO_H__
