#include "synth.hpp"

using namespace std;

std::vector<blipper_sample_t> Sawtooth::filter_bank;
Sawtooth::Sawtooth()
   : filter({}, {})
{
   if (filter_bank.empty())
      init_filter();
   blip = blipper_new(64, 0.85, 8.0, 64, 2048, filter_bank.data());
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

void Sawtooth::trigger(unsigned note, unsigned velocity, unsigned sample_rate)
{
   Voice::trigger(note, velocity, sample_rate);

   double freq = 440.0 * pow(2.0f, (note - 69.0) / 12.0);

   period = unsigned(round(sample_rate * 64 / freq)); 

   delta = -0.2f;
   blipper_reset(blip);
   blipper_push_delta(blip, -0.1f, 0);

   blipper_set_ramp(blip, 0.2f, period);
}

unsigned Sawtooth::render(float **out, unsigned frames, unsigned channels)
{
   while (blipper_read_avail(blip) < frames)
      blipper_push_delta(blip, delta, period);

   unsigned s;
   for (s = 0; s < frames; s++)
   {
      if (check_release_complete())
         break;

      blipper_sample_t val = 0;
      blipper_read(blip, &val, 1, 1);
      float res = filter.process(val) * envelope_amp();
      for (unsigned c = 0; c < channels; c++)
         out[c][s] += res;

      step();
   }

   return s;
}

