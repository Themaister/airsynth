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

#include <memory>
#include <cstdint>
#include <vector>
#include <deque>
#include <random>
#include "audio_driver.hpp"

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

      virtual void set_note(unsigned note, unsigned velocity) = 0;
      virtual void set_sustain(bool enable) = 0;

      virtual ~Synthesizer() = default;

   protected:
      unsigned channels = 2;
      unsigned sample_rate = 44100;
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

      // Sub-classes of Voice should call this if overridden.
      virtual void trigger(unsigned note, unsigned velocity, unsigned sample_rate, float detune = 0.0f);

      inline bool active() const { return m_active; }
      inline void active(bool val) { m_active = val; }

      // Voices can override this to use custom envelopes.
      virtual inline void set_envelope(float gain, float attack, float delay,
            float sustain_level, float release)
      {
         env.gain = gain;
         env.attack = attack;
         env.delay = delay;
         env.sustain_level = sustain_level;
         env.release = release;
      }

      virtual inline void set_envelope(const Envelope &env)
      {
         this->env = env;
      }

      inline unsigned get_note() const
      {
         return note;
      }

      // Should always be called when sustain is released.
      inline void release_sustain()
      {
         if (active() && sustained)
         {
            released = true;
            released_time = time;
            sustained = false;
         }
      }

      // If sustained, the release will be deferred until release_sustain() is called.
      inline void release(bool sustained)
      {
         if (active() && !this->sustained && !released)
         {
            if (sustained)
               this->sustained = true;
            else
            {
               released = true;
               released_time = time;
            }
         }
      }

   protected:
      bool check_release_complete();
      inline float velocity() const { return m_velocity; }
      inline void step() { time += time_step; }
      inline float envelope_amp() { return velocity() * env.envelope(time, released); }

   private:
      Envelope env;

      unsigned note = 0;
      float time = 0;
      float time_step = 1.0 / 44100.0;
      float sample_rate = 44100.0;
      float released_time = 0.0;

      bool sustained = false;
      bool released = false;
      float m_velocity = 0.0f;

      bool m_active = false;
};

// Uses voice-stealing algorithm to implement a multiple-voice instrument.
class Instrument
{
   public:
      template<typename T, typename... P>
      inline void init(unsigned num_voices, const P&... p)
      {
         voices.clear();
         for (unsigned i = 0; i < num_voices; i++)
            voices.push_back(std::unique_ptr<Voice>(new T(p...)));
      }

      void render(float **buffer, unsigned frames, unsigned channels);
      void set_note(unsigned note,
            unsigned velocity, unsigned sample_rate);
      void set_sustain(bool sustain);

      void reset();

   private:
      std::vector<std::unique_ptr<Voice>> voices;
      bool sustain = false;
};

class AirSynth : public Synthesizer
{
   public:
      AirSynth();

      AirSynth(AirSynth&&) = delete;
      void operator=(AirSynth&&) = delete;

      void set_note(unsigned note, unsigned velocity) override;
      void set_sustain(bool enable) override;

      void process_audio(float **buffer, unsigned frames) override;

      template<typename T, typename... P>
      void set_voices(unsigned voices, const P&&... p)
      {
         instrument.init<T>(voices, p...);
      }

   private:
      Instrument instrument;
};

struct PolyphaseBank
{
   PolyphaseBank(unsigned taps = 32, unsigned phases = 1 << 13, double cutoff = 0.75, double beta = 7.0);
   std::vector<float> buffer;
   unsigned taps;
   unsigned phases;
};

class NoiseIIR : public Voice 
{
   public:
      NoiseIIR();
      NoiseIIR(const PolyphaseBank *bank);

      unsigned render(float **out, unsigned frames, unsigned channels) override;
      void trigger(unsigned note, unsigned velocity, unsigned sample_rate, float detune) override;

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

      const PolyphaseBank *bank;
      std::default_random_engine engine;
      std::uniform_real_distribution<float> dist{-0.001, 0.001};
      static PolyphaseBank static_bank;
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
      void trigger(unsigned note, unsigned velocity, unsigned sample_rate, float detune) override;

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
      void trigger(unsigned note, unsigned velocity, unsigned sample_rate, float detune) override;

   private:
      blipper_t *blip = nullptr;

      float delta;
      unsigned period;

      static std::vector<blipper_sample_t> filter_bank;
      static void init_filter();

      Filter filter;
};

#endif

