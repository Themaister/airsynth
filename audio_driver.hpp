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

#include <jack/jack.h>
#include <jack/types.h>

#include <memory>
#include <cstdint>
#include <utility>
#include <vector>
#include <array>
#include <mutex>
#include <condition_variable>

class AudioCallback
{
   public:
      enum class Event : unsigned
      {
         NoteOff = 0,
         NoteOn,
         Aftertouch,
         Control,
         Program,
         PitchWheel,

         TimeCodeQuarter,
         SongPosition,
         SongSelect,
         TuneRequest,

         TimingClock,
         Start,
         Continue,
         Stop,
         ActiveSensing,
         Reset,

         None
      };

      typedef std::array<uint8_t, 3> MidiRawData;

      struct MidiEvent
      {
         inline MidiEvent(Event event, unsigned channel, unsigned lo, unsigned hi)
            : event(event), channel(channel), lo(lo), hi(hi) {}
         Event event;
         unsigned channel;
         unsigned lo, hi;
      };

      virtual void process_audio(float **audio, unsigned frames) = 0;
      virtual void configure_audio(unsigned sample_rate, unsigned max_frames, unsigned channels) = 0;

      virtual void process_midi(MidiEvent data) = 0;
      void process_midi(MidiRawData midi_raw);

   private:
      MidiEvent get_event(MidiRawData raw);
};

class AudioDriver
{
   public:
      inline AudioDriver(std::shared_ptr<AudioCallback> cb) : audio_cb(move(cb)) {}
      virtual ~AudioDriver() = default;

      void run();
      void kill();

   protected:
      std::shared_ptr<AudioCallback> audio_cb;

   private:
      bool dead = false;
      std::mutex lock;
      std::condition_variable cond;
};

class JACKDriver : public AudioDriver
{
   public:
      JACKDriver(std::shared_ptr<AudioCallback> cb, unsigned channels);
      ~JACKDriver();

      JACKDriver(JACKDriver&&) = delete;
      void operator=(JACKDriver&&) = delete;

      // JACK Callback
      int process(jack_nframes_t frames);

   private:
      bool init(unsigned channels);
      void term();

      jack_client_t *client = nullptr;
      std::vector<jack_port_t*> audio_ports;
      jack_port_t *midi_port = nullptr;
      std::vector<float*> target_ptrs;
      jack_nframes_t max_frames = 0;
};


#endif

