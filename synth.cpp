#include "synth.hpp"
#include <cmath>
#include <algorithm>

#include "flute_iir_300.h"

using namespace std;

AirSynth::AirSynth(const char *device)
{
   //fm_tones.resize(32);
   tones.resize(32);
   for (auto &tone : tones)
      tone.set_filter_bank(&filter_bank);

   sustain.resize(16);
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

void AirSynth::set_note(unsigned channel, unsigned note, unsigned velocity)
{
   if (velocity == 0)
   {
      for (auto &tone : tones)
      {
         if (tone.active->load() && tone.note == note &&
               !tone.released && !tone.sustained && tone.channel == channel)
         {
            lock_guard<mutex> lock{*tone.lock};
            if (sustain[channel])
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
            [](const NoiseIIR &t) { return !t.active->load(); });

      if (itr == end(tones))
      {
         fprintf(stderr, "Couldn't find any notes for note: %u, vel: %u.\n",
               note, velocity);
         return;
      }

      fprintf(stderr, "Trigger note: %u.\n", note);
      itr->reset(channel, note, velocity);
   }
}

void AirSynth::set_sustain(unsigned channel, bool sustain)
{
   this->sustain[channel] = sustain;
   if (sustain)
      return;

   for (auto &tone : tones)
   {
      if (tone.active->load() && tone.sustained && tone.channel == channel)
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
         if (!tone.active->load())
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
         active->store(false);
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

void AirSynth::Synth::reset(unsigned channel, unsigned note, unsigned vel)
{
   velocity = vel / 127.0f;

   released = false;
   sustained = false;
   this->note = note;
   this->channel = channel;

   time = 0.0;
   active->store(vel != 0);
}

void AirSynth::FM::reset(unsigned channel, unsigned note, unsigned vel)
{
   float omega = 2.0 * M_PI * 440.0 * pow(2.0, (note - 69.0) / 12.0) / 44100.0;
   carrier = {};
   modulator = {};
   carrier.omega = omega;
   modulator.omega = 2.0 * omega;

   carrier.env.attack = 0.05;
   carrier.env.delay = 0.1;
   carrier.env.sustain_level = 0.80;
   carrier.env.release = 1.0;
   carrier.env.gain = 1.0;

   modulator.env.attack = 0.2;
   modulator.env.delay = 1.0;
   modulator.env.sustain_level = 0.8;
   modulator.env.release = 0.1;
   modulator.env.gain = 2.5;

   Synth::reset(channel, note, vel);
}

void AirSynth::NoiseIIR::reset(unsigned channel, unsigned note, unsigned vel)
{
   iir_ptr = 0;
   iir_buffer.clear();
   iir_len = sizeof(flute_iir_300) / sizeof(flute_iir_300[0]);
   iir_buffer.resize(2 * iir_len);

   history.clear();
   history_ptr = 0;

   float offset = note - (69.0f + 7.0f);
   decimate_factor = round(interpolate_factor * pow(2.0f, offset / 12.0f));
   phase = 0;

   env.attack = 0.035;
   env.delay = 0.035;
   env.sustain_level = 0.35;
   env.release = 0.8;
   env.gain = exp(0.015 * (69.0 - note));

   Synth::reset(channel, note, vel);
}

void AirSynth::NoiseIIR::render(float *out, unsigned samples)
{
   unsigned s;
   for (s = 0; s < samples; s++, phase += decimate_factor)
   {
      if (released && time >= released_time + env.release)
      {
         sustained = false;
         released = false;
         active->store(false);
         //fprintf(stderr, "Ended note %u.\n", note);
         break;
      }

      while (phase >= interpolate_factor)
      {
         history_ptr = history_ptr ? history_ptr - 1 : history_len - 1;
         history[history_ptr] = history[history_ptr + history_len] = noise_step(); 
         phase -= interpolate_factor;
      }

      const float *filter = bank->buffer.data() + phase * bank->taps;
      const float *src = history.data() + history_ptr;
      float res = 0.0f;
      for (unsigned i = 0; i < history_len; i++)
         res += filter[i] * src[i];

      out[s] = env.envelope(time, released) * res;
      time += time_step;
   }

   fill(out + s, out + samples, 0.0f);
}

float AirSynth::NoiseIIR::noise_step()
{
   float res = dist(engine);
   const float *src = iir_buffer.data() + iir_ptr;
   for (unsigned i = 0; i < iir_len; i++)
      res -= src[i] * flute_iir_300[i];

   iir_ptr = iir_ptr ? iir_ptr - 1 : iir_len - 1;
   iir_buffer[iir_ptr] = iir_buffer[iir_ptr + iir_len] = res;
   return res;
}

void AirSynth::NoiseIIR::set_filter_bank(const PolyphaseBank *bank)
{
   this->bank = bank;
   interpolate_factor = bank->phases;
   history_len = bank->taps;
   history.clear();
   history.resize(2 * history_len);
}

double AirSynth::Envelope::envelope(double time, bool released)
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

double AirSynth::Oscillator::step()
{
   double ret = sin(angle);
   angle += omega;
   return ret;
}

double AirSynth::Oscillator::step(Oscillator &osc, double depth)
{
   double ret = sin(angle);
   angle += omega + depth * osc.omega * osc.step();
   return ret;
}

double AirSynth::PolyphaseBank::sinc(double v) const
{
   if (fabs(v) < 0.00001)
      return 1.0;
   else
      return sin(v) / v;
}

AirSynth::PolyphaseBank::PolyphaseBank(unsigned taps, unsigned phases)
   : taps(taps), phases(phases)
{
   buffer.resize(taps * phases);
   std::vector<float> tmp(taps * phases);
   int elems = taps * phases;
   for (int i = 0; i < elems; i++)
   {
      double phase = M_PI * double(i - (elems >> 1)) / phases;
      double window_phase = 2.0 * phase / taps;
      tmp[i] = 0.85 * sinc(0.85 * phase) * sinc(window_phase);
   }

   for (unsigned t = 0; t < taps; t++)
      for (unsigned p = 0; p < phases; p++)
         buffer[p * taps + t] = tmp[t * phases + p];
}

