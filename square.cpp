#include "synth.hpp"
#include <algorithm>

using namespace std;

std::vector<blipper_sample_t> Square::filter_bank;
Square::Square()
   : filter({}, {})
{
   if (filter_bank.empty())
      init_filter();
   blip = blipper_new(64, 0.85, 8.0, 64, 4 * 1024, filter_bank.data());
}

Square::~Square()
{
   blipper_free(blip);
}

Square& Square::operator=(Square&& square)
{
   blipper_free(blip);
   blip = square.blip;
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
   blipper_sample_t *filt = blipper_create_filter_bank(64, 64, 0.85, 8.0);
   filter_bank.insert(end(filter_bank), filt, filt + 64 * 64);
   free(filt);
}

void Square::trigger(unsigned note, unsigned velocity, unsigned sample_rate, float detune)
{
   Voice::trigger(note, velocity, sample_rate);

   float freq = (1.0f + detune) * 440.0f * pow(2.0f, (note - 69.0f) / 12.0f);

   period = unsigned(round(sample_rate * 64 / (2.0 * freq))); 

   delta = 0.5f;
   blipper_reset(blip);
   blipper_push_delta(blip, -0.25f, 0);
}

unsigned Square::render(float **out, unsigned frames, unsigned channels)
{
   blipper_sample_t stage_buffer[256];
   blipper_sample_t env_buffer[256];
   while (blipper_read_avail(blip) < frames)
   {
      blipper_push_delta(blip, delta, period);
      delta = -delta;
   }

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
         for (unsigned i = 0; i < process_frames; i++)
            buf[i] += env_buffer[i];
      }

      s += process_frames;
   }

   return s;
}

