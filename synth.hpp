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
         bool active = false;
         unsigned note = 0;

         float angle = 0.0f;
         float velocity = 0.0f;
         float omega = 0.0f;

         float time = 0.0f;
         float time_step = 1.0f / 44100.0f;

         float amp = 0.0f;

         float attack = 0.015f;
         float delay = 0.015f;
         float release = 0.8f;
         float sustain_level = 0.35f;
         float released_time = 0.0f;

         bool sustained = false;
         bool released = false;
         std::unique_ptr<std::mutex> lock{new std::mutex};

         void render(float *out, unsigned samples);
      };

      std::vector<Sine> tones;

      bool sustain = false;
      std::atomic<bool> dead;

      void float_to_s16(int16_t *out, const float *in, unsigned samples);
      void mixer_add(float *out, const float *in, unsigned samples);
};

#endif

