/*  AirSynth - A simple realtime softsynth for ALSA.
 *  Copyright (C) 2013 - Hans-Kristian Arntzen
 * 
 *  AirSynth is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  AirSynth is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with AirSynth.
 *  If not, see <http://www.gnu.org/licenses/>.
 */


#include "audio_driver.hpp"
#include <stdexcept>

using namespace std;

ALSADriver::ALSADriver(const char *device, unsigned rate, unsigned channels)
{
   if (!init(device, rate, channels))
   {
      if (pcm)
         snd_pcm_close(pcm);
      throw runtime_error("Failed to open ALSA device.\n");
   }
}

ALSADriver::~ALSADriver()
{
   snd_pcm_drain(pcm);
   snd_pcm_close(pcm);
}

bool ALSADriver::init(const char *device, unsigned rate, unsigned channels)
{
   if (snd_pcm_open(&pcm, device ? device : "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
      return false;

   snd_pcm_hw_params_t *params = NULL;
   snd_pcm_hw_params_alloca(&params);

   unsigned latency_usec = 12000;
   unsigned periods = 4;

   if (snd_pcm_hw_params_any(pcm, params) < 0)
      return false;
   if (snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
      return false;
   if (snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16) < 0)
      return false;
   if (snd_pcm_hw_params_set_channels(pcm, params, channels) < 0)
      return false;
   if (snd_pcm_hw_params_set_rate(pcm, params, rate, 0) < 0)
      return false;
   if (snd_pcm_hw_params_set_buffer_time_near(pcm, params, &latency_usec, nullptr) < 0)
      return false;
   if (snd_pcm_hw_params_set_periods_near(pcm, params, &periods, nullptr) < 0)
      return false;

   if (snd_pcm_hw_params(pcm, params) < 0)
      return false;

   return true;
}

bool ALSADriver::write(const int16_t *data, unsigned frames)
{
   const uint8_t *buffer = reinterpret_cast<const uint8_t*>(data);

   while (frames)
   {
      auto ret = snd_pcm_writei(pcm, buffer, frames);
      if (ret == -EPIPE || ret == -EINTR || ret == -ESTRPIPE)
      {
         if (ret == -EPIPE)
            fprintf(stderr, "[ALSA]: Underrun.\n");
         if (snd_pcm_recover(pcm, ret, 1) < 0)
            return false;
      }
      else if (ret < 0)
         return false;
      else
      {
         frames -= ret;
         buffer += snd_pcm_frames_to_bytes(pcm, ret);
      }
   }

   return true;
}

