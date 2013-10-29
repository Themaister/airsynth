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


#ifndef AUDIO_DRIVER_HPP__
#define AUDIO_DRIVER_HPP__

#include <asoundlib.h>
#include <cstdint>

class AudioDriver
{
   public:
      virtual ~AudioDriver() = default;
      virtual bool write(const std::int16_t *data, unsigned frames) = 0;
};

class ALSADriver : public AudioDriver
{
   public:
      ALSADriver(const char *device, unsigned rate, unsigned channels);
      ~ALSADriver();

      ALSADriver(ALSADriver&&) = delete;
      void operator=(ALSADriver&&) = delete;

      bool write(const std::int16_t *data, unsigned frames) override;

   private:
      snd_pcm_t *pcm = nullptr;
      bool init(const char *device, unsigned rate, unsigned channels);
};


#endif

