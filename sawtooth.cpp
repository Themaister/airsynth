#include "synth.hpp"
#include <algorithm>

using namespace std;

std::vector<blipper_sample_t> Sawtooth::filter_bank;
Sawtooth::Sawtooth()
   : filter({}, {})
{
   if (filter_bank.empty())
      init_filter();
   blip = blipper_new(64, 0.85, 8.0, 64, 16 * 1024, filter_bank.data());
}

Sawtooth::~Sawtooth()
{
   blipper_free(blip);
}

Sawtooth& Sawtooth::operator=(Sawtooth&& saw)
{
   blipper_free(blip);
   blip = saw.blip;
   delta = saw.delta;
   period = saw.period;
   filter = move(saw.filter);
   saw.blip = nullptr;
   return *this;
}

Sawtooth::Sawtooth(Sawtooth&& square)
{
   *this = move(square);
}

void Sawtooth::init_filter()
{
   blipper_sample_t *filt = blipper_create_filter_bank(64, 64, 0.85, 8.0);
   filter_bank.insert(end(filter_bank), filt, filt + 64 * 64);
   free(filt);
}

void Sawtooth::trigger(unsigned note, unsigned velocity, unsigned sample_rate, float detune)
{
   Voice::trigger(note, velocity, sample_rate);

   double freq = (1.0f + detune) * 440.0f * pow(2.0f, (note - 69.0f) / 12.0f);

   period = unsigned(round(sample_rate * 64 / freq)); 
   if (period > 16 * 1024 * 64)
   {
      active(false);
      return;
   }

   delta = -0.2f;
   blipper_reset(blip);
   blipper_push_delta(blip, -0.1f, 0);

   blipper_set_ramp(blip, 0.2f, period);
}

unsigned Sawtooth::render(float **out, const float *amp, unsigned frames, unsigned channels)
{
   blipper_sample_t stage_buffer[256];
   blipper_sample_t env_buffer[256];
   while (blipper_read_avail(blip) < frames)
      blipper_push_delta(blip, delta, period);

   unsigned s;
   for (s = 0; s < frames; )
   {
      if (check_release_complete())
         break;

      unsigned process_frames = min(256u, frames - s);
      blipper_read(blip, stage_buffer, process_frames, 1);

      for (unsigned i = 0; i < process_frames; i++)
      {
         env_buffer[i] = filter.process(stage_buffer[i]) * envelope_amp();
         step();
      }

      for (unsigned c = 0; c < channels; c++)
      {
         float *buf = out[c] + s;
         float amp_tmp = amp[c];
         for (unsigned i = 0; i < process_frames; i++)
            buf[i] += amp_tmp * env_buffer[i];
      }

      s += process_frames;
   }

   return s;
}

