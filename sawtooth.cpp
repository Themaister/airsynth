#include "synth.hpp"

using namespace std;

std::vector<blipper_sample_t> Sawtooth::filter_bank;
Sawtooth::Sawtooth()
   //: filter({0.0168f, 2.0f * 0.0168f, 0.0168f}, {1.0f, -1.601092, 0.66836f})
   : filter({}, {})
{
   if (filter_bank.empty())
      init_filter();
   blip = blipper_new(256, 0.85, 9.0, 64, 2048, filter_bank.data());
}

Sawtooth::~Sawtooth()
{
   blipper_free(blip);
}

Sawtooth& Sawtooth::operator=(Sawtooth&& saw)
{
   blipper_free(blip);
   blip = saw.blip;
   env = saw.env;
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
   blipper_sample_t *filt = blipper_create_filter_bank(64, 256, 0.85, 9.0);
   filter_bank.insert(end(filter_bank), filt, filt + 256 * 64);
   free(filt);
}

void Sawtooth::reset(unsigned channel, unsigned note, unsigned velocity, unsigned sample_rate)
{
   double freq = 440.0 * pow(2.0f, (note - 69.0) / 12.0);

   period = unsigned(round(sample_rate * 64 / freq)); 

   delta = -0.2f;
   blipper_reset(blip);
   blipper_push_delta(blip, -0.1f, 0);

   blipper_set_ramp(blip, 0.2f, period);

   env.attack = 0.05;
   env.delay = 0.05;
   env.sustain_level = 0.15;
   env.release = 0.05;

   Instrument::reset(channel, note, velocity, sample_rate);
}

unsigned Sawtooth::render(float **out, unsigned frames, unsigned channels)
{
   while (blipper_read_avail(blip) < frames)
      blipper_push_delta(blip, delta, period);

   unsigned s;
   for (s = 0; s < frames; s++)
   {
      if (check_release_complete(env.release))
         break;

      blipper_sample_t val = 0;
      blipper_read(blip, &val, 1, 1);
      float res = filter.process(val) * env.envelope(time, released) * velocity;
      for (unsigned c = 0; c < channels; c++)
         out[c][s] += res;

      time += time_step;
   }

   return s;
}

