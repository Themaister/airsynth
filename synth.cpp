#include "synth.hpp"
#include <cmath>
#include <algorithm>

using namespace std;

AirSynth::AirSynth(const char *device)
{
   tones.resize(32);
   audio = unique_ptr<AudioDriver>(new ALSADriver(device, 44100, 1));
   dead.store(false);
   mixer_thread = thread(&AirSynth::mixer_loop, this);
}

AirSynth::~AirSynth()
{
   dead.store(true);
   if (mixer_thread.joinable())
      mixer_thread.join();
}

void AirSynth::set_note(unsigned note, unsigned velocity)
{
   float omega = 2.0f * M_PI * 440.0f * pow(2.0f, (note - 69.0f) / 12.0f) / 44100.0f;
   float vel = velocity / 128.0f;

   auto itr = find_if(begin(tones), end(tones), [](const Sine& t) { return !t.active; });
   if (itr == end(tones))
      return;

   lock_guard<mutex> lock{*itr->lock};
   if (itr->active && velocity == 0)
   {
      if (sustain)
         itr->sustained = true;
      else
      {
         itr->released = true;
         itr->released_time = itr->time;
      }
   }
   else
   {
      itr->velocity = vel;
      itr->active = true;
      itr->angle = 0.0f;
      itr->omega = omega;
   }
}

void AirSynth::set_sustain(bool sustain)
{
   this->sustain = sustain;
   if (sustain)
      return;

   for (auto &tone : tones)
   {
      lock_guard<mutex> lock{*tone.lock};
      if (tone.active && tone.sustained)
      {
         tone.released = true;
         tone.released_time = tone.time;
         tone.sustained = false;
      }
   }
}

void AirSynth::float_to_s16(int16_t *out, const float *in, unsigned samples)
{
   for (unsigned s = 0; s < samples; s++)
   {
      int32_t v = int32_t(round(in[s] * 0x7fff));
      if (v > 0x7fff)
         out[s] = 0x7fff;
      else if (v < -0x8000)
         out[s] = -0x8000;
      else
         out[s] = v;
   }
}

void AirSynth::mixer_add(float *out, const float *in, unsigned samples)
{
   for (unsigned s = 0; s < samples; s++)
      out[s] += in[s];
}

void AirSynth::mixer_loop()
{
   float buffer[64];
   int16_t out_buffer[64];

   while (!dead.load())
   {
      fill(begin(buffer), end(buffer), 0.0f);

      for (auto &tone : tones)
      {
         float buf[64];
         {
            lock_guard<mutex> hold{*tone.lock};
            if (!tone.active)
               continue;
            tone.render(buf, 64);
         }
         mixer_add(buffer, buf, 64);
      }

      float_to_s16(out_buffer, buffer, 64);
      audio->write(out_buffer, 64);
   }
}

void AirSynth::Sine::render(float *out, unsigned samples)
{
   float attack_deriv = time_step / attack;
   float delay_deriv = time_step * (-sustain_level + 1.0f) / delay;
   float release_factor = time_step * release * 6.0f;
   float sustained_time = attack + delay;

   unsigned i;
   for (i = 0; i < samples; i++)
   {
      if (released && time >= released_time + release)
      {
         if (time >= released_time + release)
         {
            active = false;
            sustained = false;
            break;
         }
         else
            amp -= amp * release_factor;
      }
      else if (time >= sustained_time)
         amp = sustain_level;
      else if (time >= delay)
         amp += delay_deriv;
      else
         amp += attack_deriv;

      out[i] = amp * sin(angle);
      angle += omega;
      time += time_step;
   }

   fill(out + i, out + samples, 0.0f);
}

