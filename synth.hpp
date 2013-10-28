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
      AirSynth(const char *device);
      ~AirSynth();

      AirSynth(AirSynth&&) = delete;
      void operator=(AirSynth&&) = delete;

      void set_note(unsigned channel, unsigned note, unsigned velocity) override;
      void set_sustain(unsigned channel, bool enable) override;

   private:
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

      struct Oscillator
      {
         double angle = 0.0;
         double omega = 0.0;
         Envelope env;

         double step();
         double step(Oscillator &osc, double depth);
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

         virtual void render(float *out, unsigned samples) = 0;
         virtual void reset(unsigned channel, unsigned note, unsigned velocity);
      };

      struct PolyphaseBank
      {
         PolyphaseBank(unsigned taps, unsigned phases);
         std::vector<float> buffer;
         unsigned taps;
         unsigned phases;

         double sinc(double v) const;
      };
      PolyphaseBank filter_bank{8, 1 << 13};

      struct NoiseIIR : Synth
      {
         Envelope env;
         std::vector<float> iir_buffer;
         unsigned iir_ptr = 0;
         unsigned iir_len = 0;

         void render(float *out, unsigned samples) override;
         void reset(unsigned channel, unsigned note, unsigned velocity) override;

         unsigned interpolate_factor = 0;
         unsigned decimate_factor = 0;
         unsigned phase = 0;
         std::vector<float> history;
         unsigned history_ptr = 0;
         unsigned history_len = 0;

         float noise_step();

         void set_filter_bank(const PolyphaseBank *bank);
         const PolyphaseBank *bank = nullptr;

         std::default_random_engine engine;
         std::uniform_real_distribution<float> dist{-0.01f, 0.01f};
      };

      struct FM : Synth
      {
         Oscillator carrier, modulator;

         void render(float *out, unsigned samples) override;
         void reset(unsigned channel, unsigned note, unsigned velocity) override;
      };

      //std::vector<FM> fm_tones;
      std::vector<NoiseIIR> tones;

      std::vector<bool> sustain;
      std::atomic<bool> dead;

      void float_to_s16(int16_t *out, const float *in, unsigned samples);
      void mixer_add(float *out, const float *in, unsigned samples);
};

#endif

