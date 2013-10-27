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
      virtual void set_note(unsigned note, unsigned velocity) = 0;
      virtual void set_sustain(bool enable) = 0;
      virtual ~Synthesizer() = default;
};

class AirSynth : public Synthesizer
{
   public:
      AirSynth(const char *device);
      ~AirSynth();

      AirSynth(AirSynth&&) = delete;
      void operator=(AirSynth&&) = delete;

      void set_note(unsigned note, unsigned velocity) override;
      void set_sustain(bool enable) override;

   private:
      std::unique_ptr<AudioDriver> audio;
      std::thread mixer_thread;
      void mixer_loop();

      struct Sine
      {
         Sine() { reset(0, 0); }

         std::unique_ptr<std::atomic_bool> active{new std::atomic_bool};
         unsigned note;
         double angle;
         double omega;
         double time;
         double time_step = 1.0 / 44100.0;
         double amp;
         float velocity;
         double attack;
         double delay;
         double release;
         double sustain_level;
         double released_time = 0.0;

         bool sustained;
         bool released;
         std::unique_ptr<std::mutex> lock{new std::mutex};

         void render(float *out, unsigned samples);
         void reset(unsigned note, unsigned velocity);
      };

      std::vector<Sine> tones;

      bool sustain = false;
      std::atomic<bool> dead;

      void float_to_s16(int16_t *out, const float *in, unsigned samples);
      void mixer_add(float *out, const float *in, unsigned samples);
};

#endif

