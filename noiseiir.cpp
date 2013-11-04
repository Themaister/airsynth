#include "synth.hpp"
#include "flute_iir.h"
#include <algorithm>

using namespace std;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
void NoiseIIR::reset(unsigned channel, unsigned note, unsigned vel)
{
   history_l.clear();
   history_r.clear();
   history_l.resize(2 * history_len);
   history_r.resize(2 * history_len);
   history_ptr = 0;

   float offset = note - (69.0f + 7.0f);
   decimate_factor = unsigned(round(interpolate_factor * pow(2.0f, offset / 12.0f)));
   phase = 0;

   env.attack = 0.635 - vel / 220.0;
   env.delay = 0.865 - vel / 220.0;
   env.sustain_level = 0.45;
   env.release = 1.2;
   env.gain = 0.05 * exp(0.025 * (69.0 - note));

   Instrument::reset(channel, note, vel);
}

NoiseIIR::NoiseIIR()
{
   iir_l.set_filter(flute_iir_filt_l, ARRAY_SIZE(flute_iir_filt_l));
   iir_r.set_filter(flute_iir_filt_r, ARRAY_SIZE(flute_iir_filt_r));
}

void NoiseIIR::render(float *out, unsigned frames)
{
   unsigned s;
   for (s = 0; s < frames; s++, phase += decimate_factor)
   {
      if (check_release_complete(env.release))
         break;

      while (phase >= interpolate_factor)
      {
         history_ptr = (history_ptr ? history_ptr : history_len) - 1;
         history_l[history_ptr] = history_l[history_ptr + history_len] = noise_step(iir_l); 
         history_r[history_ptr] = history_r[history_ptr + history_len] = noise_step(iir_r); 
         phase -= interpolate_factor;
      }

      const double *filter = bank->buffer.data() + phase * bank->taps;

      const double *src_l = history_l.data() + history_ptr;
      const double *src_r = history_r.data() + history_ptr;

      double res_l = 0.0;
      double res_r = 0.0;
      for (unsigned i = 0; i < history_len; i++)
      {
         res_l += filter[i] * src_l[i];
         res_r += filter[i] * src_r[i];
      }

      double env_mod = env.envelope(time, released) * velocity;
      out[(s << 1) + 0] = env_mod * res_l;
      out[(s << 1) + 1] = env_mod * res_r;
      time += time_step;
   }

   fill(out + (s << 1), out + (frames << 1), 0.0f);
}

double NoiseIIR::IIR::step(double v)
{
   double res = 0.0;
   const double *src = buffer.data() + ptr;
   for (unsigned i = 0; i < len; i++)
      res += src[i] * filter[i];
   res += v;

   ptr = (ptr ? ptr : len) - 1;
   buffer[ptr] = buffer[ptr + len] = res;
   return res;
}

void NoiseIIR::IIR::set_filter(const double *filter, unsigned len)
{
   this->filter = filter;
   this->len = len;
   buffer.clear();
   buffer.resize(2 * len);
   ptr = 0;
}

double NoiseIIR::noise_step(IIR &iir)
{
   return iir.step(dist(engine));
}

void NoiseIIR::set_filter_bank(const PolyphaseBank *bank)
{
   this->bank = bank;
   interpolate_factor = bank->phases;
   history_len = bank->taps;

   history_l.clear();
   history_l.resize(2 * history_len);
   history_r.clear();
   history_r.resize(2 * history_len);
}

