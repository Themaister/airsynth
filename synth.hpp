#ifndef AIRSYNTH_HPP__
#define AIRSYNTH_HPP__

#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <cstdint>
#include <vector>
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

      struct FM 
      {
         FM() { reset(0, 0, 0); }

         std::unique_ptr<std::atomic_bool> active{new std::atomic_bool};
         unsigned note;
         unsigned channel;
         double time;

         double time_step = 1.0 / 44100.0;
         float velocity;

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
         Oscillator carrier, modulator;

         double released_time = 0.0;

         bool sustained;
         bool released;
         std::unique_ptr<std::mutex> lock{new std::mutex};

         void render(float *out, unsigned samples);
         void reset(unsigned channel, unsigned note, unsigned velocity);
      };

      std::vector<FM> tones;
      std::vector<bool> sustain;
      std::atomic<bool> dead;

      void float_to_s16(int16_t *out, const float *in, unsigned samples);
      void mixer_add(float *out, const float *in, unsigned samples);
};

#endif

