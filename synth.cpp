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
            [](const FM &t) { return !t.active->load(memory_order_acquire); });

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

void AirSynth::FM::render(float *out, unsigned samples)
{
   lock_guard<mutex> hold{*lock};

   unsigned i;
   for (i = 0; i < samples; i++)
   {
      if (released && time >= released_time + carrier.env.release)
      {
         sustained = false;
         released = false;
         active->store(false, memory_order_release);
         //fprintf(stderr, "Ended note %u.\n", note);
         break;
      }

      double env = carrier.env.envelope(time, released);
      double mod_env = modulator.env.envelope(time, released);

      out[i] = env * velocity * carrier.step(modulator, mod_env);
      time += time_step;
   }

   fill(out + i, out + samples, 0.0f);
}

void AirSynth::FM::reset(unsigned note, unsigned vel)
{
   float omega = 2.0 * M_PI * 440.0 * pow(2.0, (note - 69.0) / 12.0) / 44100.0;
   velocity = vel / 127.0f;

   released = false;
   sustained = false;
   this->note = note;

   time = 0.0;

   carrier = {};
   modulator = {};
   carrier.omega = omega;
   modulator.omega = 2.0 * omega;

   carrier.env.attack = 0.2;
   carrier.env.delay = 0.5;
   carrier.env.sustain_level = 0.80;
   carrier.env.release = 0.5;
   carrier.env.gain = 1.0;

   modulator.env.attack = 0.2;
   modulator.env.delay = 1.0;
   modulator.env.sustain_level = 0.8;
   modulator.env.release = 0.1;
   modulator.env.gain = 2.5;

   active->store(vel != 0, memory_order_release);
}

double AirSynth::FM::Envelope::envelope(double time, bool released)
{
   if (released)
   {
      double release_factor = 6.0 * time_step / release;
      amp -= amp * release_factor;
   }
   else if (time >= attack + delay)
      amp = sustain_level;
   else if (time >= attack)
   {
      double lerp = (time - attack) / delay;
      amp = (1.0 - lerp) + sustain_level * lerp;
   }
   else
      amp = time / attack;

   return gain * amp;
}

double AirSynth::FM::Oscillator::step()
{
   double ret = sin(angle);
   angle += omega;
   return ret;
}

double AirSynth::FM::Oscillator::step(Oscillator &osc, double depth)
{
   double ret = sin(angle);
   angle += omega + depth * osc.omega * osc.step();
   return ret;
}

