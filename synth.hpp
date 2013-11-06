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


#ifndef AIRSYNTH_HPP__
#define AIRSYNTH_HPP__

#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <cstdint>
#include <vector>
#include <deque>
#include <random>
#include "audio_driver.hpp"
#include <sndfile.h>

#include "blipper.h"

class Synthesizer : public AudioCallback
{
   public:
      void configure_audio(unsigned sample_rate, unsigned channels) override
      {
         this->channels = channels;
         this->sample_rate = sample_rate;
      }

      void process_midi(MidiEvent event) override;

      virtual void set_note(unsigned channel, unsigned note, unsigned velocity) = 0;
      virtual void set_sustain(unsigned channel, bool enable) = 0;

      virtual ~Synthesizer() = default;

   protected:
      unsigned channels = 2;
      unsigned sample_rate = 44100;

      static void mixer_add(float *out, const float *in, unsigned samples);
};

struct Envelope
{
   float attack = 0.1;
   float delay = 0.1;
   float sustain_level = 0.25;
   float release = 0.5;
   float amp = 0.0;
   float gain = 1.0;

   float time_step = 1.0 / 44100.0;

   float envelope(float time, bool released);
};

struct Voice
{
   public:
      virtual unsigned render(float **out, unsigned frames, unsigned channels) = 0;
      virtual void reset(unsigned channel, unsigned note, unsigned velocity, unsigned sample_rate);

      inline bool active() const { return m_active->load(); }
      inline void active(bool val) { m_active->store(val); }

      bool check_release_complete();

      inline void lock() { m_lock->lock(); }
      inline void unlock() { m_lock->unlock(); }

      inline float velocity() const { return m_velocity; }
      inline void step() { time += time_step; }

      inline void set_envelope(float gain, float attack, float delay, float sustain_level,
            float release)
      {
         env.gain = gain;
         env.attack = attack;
         env.delay = delay;
         env.sustain_level = sustain_level;
         env.release = release;
      }

      inline float envelope_amp() { return velocity() * env.envelope(time, released); }

      inline void release_sustain(unsigned channel)
      {
         if (active() && sustained && this->channel == channel)
         {
            std::lock_guard<std::mutex> lock{*m_lock};
            released = true;
            released_time = time;
            sustained = false;
         }
      }

      inline void release_note(unsigned channel, unsigned note, bool sustained)
      {
         if (active() && this->note == note && this->channel == channel &&
            !this->sustained && !released)
         {
            std::lock_guard<std::mutex> holder{*m_lock};
            if (sustained)
               this->sustained = true;
            else
            {
               released = true;
               released_time = time;
            }
         }
      }

   private:
      Envelope env;

      unsigned note = 0;
      unsigned channel = 0;
      float time = 0;
      float time_step = 1.0 / 44100.0;
      float sample_rate = 44100.0;
      float released_time = 0.0;

      bool sustained = false;
      bool released = false;
      float m_velocity = 0.0f;

      std::unique_ptr<std::mutex> m_lock{new std::mutex};
      std::unique_ptr<std::atomic_bool> m_active{new std::atomic_bool(false)};
};

class Instrument
{
   public:
      Instrument() { sustain.resize(16); }
      template<typename T, typename... P>
      inline void init(unsigned num_voices, const P&... p)
      {
         voices.clear();
         for (unsigned i = 0; i < num_voices; i++)
            voices.push_back(std::unique_ptr<Voice>(new T(p...)));
      }

      void render(float **buffer, unsigned frames, unsigned channels);
      void set_note(unsigned channel, unsigned note,
            unsigned velocity, unsigned sample_rate);
      void set_sustain(unsigned channel, bool sustain);

      void reset();

   private:
      std::vector<std::unique_ptr<Voice>> voices;
      std::vector<bool> sustain;
};

struct PolyphaseBank
{
   PolyphaseBank(unsigned taps, unsigned phases);
   std::vector<float> buffer;
   unsigned taps;
   unsigned phases;
};

class NoiseIIR : public Voice 
{
   public:
      NoiseIIR(const PolyphaseBank *bank);

      unsigned render(float **out, unsigned frames, unsigned channels) override;
      void reset(unsigned channel, unsigned note, unsigned velocity, unsigned sample_rate) override;

   private:
      struct IIR
      {
         const float *filter = nullptr;
         std::vector<float> buffer;
         unsigned ptr = 0;
         unsigned len = 0;
         float step(float v);
         void set_filter(const float *filter, unsigned len);
         void reset();
      } iir_l, iir_r;

      unsigned interpolate_factor = 0;
      unsigned decimate_factor = 0;
      unsigned phase = 0;

      std::vector<float> history_l;
      std::vector<float> history_r;
      unsigned history_ptr = 0;
      unsigned history_len = 0;

      float noise_step(IIR &iir);

      const PolyphaseBank *bank = nullptr;

      std::default_random_engine engine;
      std::uniform_real_distribution<float> dist{-0.01, 0.01};
};

class Filter 
{
   public:
      Filter();
      Filter(std::vector<float> b, std::vector<float> a);
      float process(float samp);
      void reset();

   private:
      std::vector<float> b, a;
      std::deque<float> buffer;
};

class Square : public Voice 
{
   public:
      Square();
      ~Square();

      Square(Square&&);
      Square& operator=(Square&&);

      unsigned render(float **out, unsigned frames, unsigned channels) override;
      void reset(unsigned channel, unsigned note, unsigned velocity, unsigned sample_rate) override;

   private:
      blipper_t *blip = nullptr;

      float delta;
      unsigned period;

      static std::vector<blipper_sample_t> filter_bank;
      static void init_filter();

      Filter filter;
};

class Sawtooth : public Voice 
{
   public:
      Sawtooth();
      ~Sawtooth();

      Sawtooth(Sawtooth&&);
      Sawtooth& operator=(Sawtooth&&);

      unsigned render(float **out, unsigned frames, unsigned channels) override;
      void reset(unsigned channel, unsigned note, unsigned velocity, unsigned sample_rate) override;

   private:
      blipper_t *blip = nullptr;

      float delta;
      unsigned period;

      static std::vector<blipper_sample_t> filter_bank;
      static void init_filter();

      Filter filter;
};

class AirSynth : public Synthesizer
{
   public:
      AirSynth();

      AirSynth(AirSynth&&) = delete;
      void operator=(AirSynth&&) = delete;

      void set_note(unsigned channel, unsigned note, unsigned velocity) override;
      void set_sustain(unsigned channel, bool enable) override;

      void process_audio(float **buffer, unsigned frames) override;

   private:
      PolyphaseBank filter_bank{32, 1 << 13};
      std::shared_ptr<Instrument> instrument;
};

#endif

