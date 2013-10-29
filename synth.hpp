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
#include <random>
#include "audio_driver.hpp"
#include <sndfile.h>

class Synthesizer
{
   public:
      virtual void set_note(unsigned, unsigned note, unsigned velocity) = 0;
      virtual void set_sustain(unsigned, bool enable) = 0;
      virtual ~Synthesizer() = default;
};

class AirSynth : public Synthesizer
{
   public:
      AirSynth(const char *device, const char *path);
      ~AirSynth();

      AirSynth(AirSynth&&) = delete;
      void operator=(AirSynth&&) = delete;

      void set_note(unsigned channel, unsigned note, unsigned velocity) override;
      void set_sustain(unsigned channel, bool enable) override;

   private:
      std::vector<float> wav_buffer;
      SNDFILE *sndfile = nullptr;
      std::unique_ptr<AudioDriver> audio;
      std::thread mixer_thread;
      void mixer_loop();

      struct Envelope
      {
         double attack = 0.0;
         double delay = 0.0;
         double sustain_level = 0.0;
         double release = 0.0;
         double amp = 0.0;
         double gain = 1.0;

         double time_step = 1.0 / 44100.0;

         double envelope(double time, bool released);
      };

      struct Synth
      {
         Synth() { active->store(false); }
         unsigned note = 0;
         unsigned channel = 0;
         double time = 0;
         double time_step = 1.0 / 44100.0;
         double released_time = 0.0;

         bool sustained = false;
         bool released = false;
         float velocity = 0.0f;
         std::unique_ptr<std::mutex> lock{new std::mutex};
         std::unique_ptr<std::atomic_bool> active{new std::atomic_bool};

         virtual void render(float *out, unsigned frames) = 0;
         virtual void reset(unsigned channel, unsigned note, unsigned velocity);

         bool check_release_complete(double release);
      };

      struct PolyphaseBank
      {
         PolyphaseBank(unsigned taps, unsigned phases);
         std::vector<double> buffer;
         unsigned taps;
         unsigned phases;

         double sinc(double v) const;
      };
      PolyphaseBank filter_bank{64, 1 << 13};

      struct NoiseIIR : Synth
      {
         NoiseIIR();
         Envelope env;

         struct IIR
         {
            const double *filter = nullptr;
            std::vector<double> buffer;
            unsigned ptr = 0;
            unsigned len = 0;
            double step(double v);
            void set_filter(const double *filter, unsigned len);
            void reset();
         } iir_l, iir_r;

         void render(float *out, unsigned frames) override;
         void reset(unsigned channel, unsigned note, unsigned velocity) override;

         unsigned interpolate_factor = 0;
         unsigned decimate_factor = 0;
         unsigned phase = 0;

         std::vector<double> history_l;
         std::vector<double> history_r;
         unsigned history_ptr = 0;
         unsigned history_len = 0;

         double noise_step(IIR &iir);

         void set_filter_bank(const PolyphaseBank *bank);
         const PolyphaseBank *bank = nullptr;

         std::default_random_engine engine;
         std::uniform_real_distribution<double> dist{-0.01, 0.01};
      };

      std::vector<NoiseIIR> tones;

      std::vector<bool> sustain;
      std::atomic<bool> dead;

      void float_to_s16(int16_t *out, const float *in, unsigned samples);
      void mixer_add(float *out, const float *in, unsigned samples);
};

#endif

