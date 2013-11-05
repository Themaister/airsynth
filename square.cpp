#include "synth.hpp"

using namespace std;

std::vector<blipper_sample_t> Square::filter_bank;
Square::Square()
   : filter({0.0168f, 2.0f * 0.0168f, 0.0168f}, {1.0f, -1.601092, 0.66836f})
{
   if (filter_bank.empty())
      init_filter();
   blip = blipper_new(256, 0.85, 9.0, 64, 2048, filter_bank.data());
}

Square::~Square()
{
   blipper_free(blip);
}

Square& Square::operator=(Square&& square)
{
   blipper_free(blip);
   blip = square.blip;
   env = square.env;
   delta = square.delta;
   period = square.period;
   filter = move(square.filter);
   square.blip = nullptr;
   return *this;
}

Square::Square(Square&& square)
{
   *this = move(square);
}

void Square::init_filter()
{
   blipper_sample_t *filt = blipper_create_filter_bank(64, 256, 0.85, 9.0);
   filter_bank.insert(end(filter_bank), filt, filt + 256 * 64);
   free(filt);
}

void Square::reset(unsigned channel, unsigned note, unsigned velocity, unsigned sample_rate)
{
   double freq = 440.0 * pow(2.0f, (note - 69.0) / 12.0);

   period = unsigned(round(sample_rate * 64 / (2.0 * freq))); 

   delta = 0.5f;
   blipper_reset(blip);
   blipper_push_delta(blip, -0.25f, 0);

   env.attack = 0.10;
   env.delay = 0.10;
   env.sustain_level = 0.05;
   env.release = 0.2;

   Instrument::reset(channel, note, velocity, sample_rate);
}

unsigned Square::render(float **out, unsigned frames, unsigned channels)
{
   while (blipper_read_avail(blip) < frames)
   {
      blipper_push_delta(blip, delta, period);
      delta = -delta;
   }

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


