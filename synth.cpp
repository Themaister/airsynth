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
   if (velocity == 0)
   {
      for (auto &tone : tones)
      {
         if (tone.active->load(memory_order_acquire) && tone.note == note &&
               !tone.released && !tone.sustained)
         {
            lock_guard<mutex> lock{*tone.lock};
            if (sustain)
               tone.sustained = true;
            else
            {
               tone.released = true;
               tone.released_time = tone.time;
            }
         }
      }
   }
   else
   {
      auto itr = find_if(begin(tones), end(tones),
            [](const Sine &t) { return !t.active->load(memory_order_acquire); });

      if (itr == end(tones))
      {
         fprintf(stderr, "Couldn't find any notes for note: %u, vel: %u.\n",
               note, velocity);
         return;
      }

      fprintf(stderr, "Trigger note: %u.\n", note);
      itr->reset(note, velocity);
   }
}

void AirSynth::set_sustain(bool sustain)
{
   this->sustain = sustain;
   if (sustain)
      return;

   for (auto &tone : tones)
   {
      if (tone.active->load(memory_order_acquire) && tone.sustained)
      {
         lock_guard<mutex> lock{*tone.lock};
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
      int32_t v = int32_t(round(in[s] * 0.1f * 0x7fff));
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
         if (!tone.active->load(memory_order_acquire))
            continue;

         float buf[64];
         tone.render(buf, 64);
         mixer_add(buffer, buf, 64);
      }

      float_to_s16(out_buffer, buffer, 64);
      audio->write(out_buffer, 64);
   }
}

void AirSynth::Sine::render(float *out, unsigned samples)
{
   lock_guard<mutex> hold{*lock};

   double attack_deriv = time_step / attack;
   double delay_deriv = time_step * (sustain_level - 1.0f) / delay;
   double release_factor = 6.0 * time_step / release;
   double sustained_time = attack + delay;

   unsigned i;
   for (i = 0; i < samples; i++)
   {
      if (released)
      {
         if (time >= released_time + release)
         {
            sustained = false;
            released = false;
            active->store(false, memory_order_release);
            fprintf(stderr, "Ended note %u.\n", note);
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

      float sine_res = 0.0f;
      static const float harmonics[] = {1.0f, 0.29f, 0.35f, 0.11f};
      double tmp_angle = angle;
      for (auto harm : harmonics)
      {
         sine_res += harm * sin(tmp_angle);
         tmp_angle += angle;
      }

      out[i] = amp * velocity * sine_res;
      angle += omega;
      time += time_step;
   }

   fill(out + i, out + samples, 0.0f);
}

void AirSynth::Sine::reset(unsigned note, unsigned vel)
{
   omega = 2.0 * M_PI * 440.0 * pow(2.0, (note - 69.0) / 12.0) / 44100.0;
   velocity = vel / 128.0f;

   released = false;
   sustained = false;
   angle = 0.0;
   amp = 0.0;
   time = 0.0;
   this->note = note;

   attack = 0.255;
   delay = 0.155;
   release = 0.8;
   sustain_level = 0.55;

   active->store(vel != 0, memory_order_release);
}

