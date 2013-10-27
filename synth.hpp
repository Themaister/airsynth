#ifndef AIRSYNTH_HPP__
#define AIRSYNTH_HPP__

#include <thread>
#include <atomic>
#include <memory>
#include <cstdint>
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

      std::atomic<float> omega;
      std::atomic<float> vel;
      std::atomic<bool> sustain;
      std::atomic<bool> dead;
};

#endif

