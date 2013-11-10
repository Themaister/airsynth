#include "synth.hpp"
#include "flute_iir.h"
#include <algorithm>

using namespace std;

PolyphaseBank NoiseIIR::static_bank;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
void NoiseIIR::trigger(unsigned note, unsigned vel, unsigned sample_rate, float detune)
{
   Voice::trigger(note, vel, sample_rate);

   fill(begin(history_l), end(history_l), 0.0f);
   fill(begin(history_r), end(history_r), 0.0f);
   history_ptr = 0;

   float offset = note - (69.0f + 7.0f);
   decimate_factor = unsigned(round(((1.0f + detune) * 44100.0f / sample_rate) *
            interpolate_factor * pow(2.0f, offset / 12.0f)));
   phase = 0;
}

NoiseIIR::NoiseIIR(const PolyphaseBank *bank)
{
   iir_l.set_filter(flute_iir_filt_l, ARRAY_SIZE(flute_iir_filt_l));
   iir_r.set_filter(flute_iir_filt_r, ARRAY_SIZE(flute_iir_filt_r));

   this->bank = bank;
   interpolate_factor = bank->phases;
   history_len = bank->taps;

   history_l.clear();
   history_l.resize(2 * history_len);
   history_r.clear();
   history_r.resize(2 * history_len);
}

NoiseIIR::NoiseIIR()
   : NoiseIIR(&static_bank)
{}

unsigned NoiseIIR::render(float **out, const float *amp, unsigned frames, unsigned channels)
{
   unsigned s;
   for (s = 0; s < frames; s++, phase += decimate_factor)
   {
      if (check_release_complete())
         break;

      while (phase >= interpolate_factor)
      {
         history_ptr = (history_ptr ? history_ptr : history_len) - 1;
         history_l[history_ptr] = history_l[history_ptr + history_len] = noise_step(iir_l); 
         history_r[history_ptr] = history_r[history_ptr + history_len] = noise_step(iir_r); 
         phase -= interpolate_factor;
      }

      const float *filter = bank->buffer.data() + phase * bank->taps;

      const float *src_l = history_l.data() + history_ptr;
      const float *src_r = history_r.data() + history_ptr;

      float res[2] = {0.0, 0.0};
      for (unsigned i = 0; i < history_len; i++)
      {
         res[0] += filter[i] * src_l[i];
         res[1] += filter[i] * src_r[i];
      }

      float env_mod = envelope_amp();
      for (unsigned c = 0; c < channels; c++)
         out[c][s] += amp[c] * env_mod * res[c & 1];

      step();
   }

   return s;
}

float NoiseIIR::IIR::step(float v)
{
   float res = 0.0;
   const float *src = buffer.data() + ptr;
   for (unsigned i = 0; i < len; i++)
      res += src[i] * filter[i];
   res += v;

   ptr = (ptr ? ptr : len) - 1;
   buffer[ptr] = buffer[ptr + len] = res;
   return res;
}

void NoiseIIR::IIR::set_filter(const float *filter, unsigned len)
{
   this->filter = filter;
   this->len = len;
   buffer.clear();
   buffer.resize(2 * len);
   ptr = 0;
}

float NoiseIIR::noise_step(IIR &iir)
{
   return iir.step(dist(engine));
}

