/*  AirSynth - A simple realtime softsynth for ALSA.
 *  Copyright (C) 2013 - Hans-Kristian Arntzen
 * 
 *  AirSynth is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  AirSynth is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with AirSynth.
 *  If not, see <http://www.gnu.org/licenses/>.
 */


#include "synth.hpp"
#include <cmath>
#include <algorithm>

using namespace std;

static PolyphaseBank filter_bank;

AirSynth::AirSynth()
{
   instrument.init<NoiseIIR>(32, &filter_bank);
}

void Synthesizer::process_midi(MidiEvent data)
{
   switch (data.event)
   {
      case Event::NoteOn:
         set_note(data.lo, data.hi);
         break;

      case Event::NoteOff:
         set_note(data.lo, 0);
         break;

      case Event::Control:
         if (data.lo == 64) // Sustain controller on my CP33.
            set_sustain(data.hi);
         break;

      default:
         break;
   }
}

void AirSynth::set_note(unsigned note, unsigned velocity)
{
   instrument.set_note(note, velocity, sample_rate);
}

void AirSynth::set_sustain(bool sustain)
{
   instrument.set_sustain(sustain);
}

void AirSynth::process_audio(float **buffer, unsigned frames)
{
   instrument.render(buffer, frames, channels);
}

void Instrument::set_note(unsigned note,
      unsigned velocity, unsigned sample_rate)
{
   if (velocity == 0)
   {
      for (auto &tone : voices)
         if (note == tone->get_note())
            tone->release(sustain);
   }
   else
   {
      auto itr = find_if(begin(voices), end(voices),
            [](const unique_ptr<Voice> &t) { return !t->active(); });
      if (itr == end(voices))
         return;
      (*itr)->trigger(note, velocity, sample_rate);
   }
}

void Instrument::set_sustain(bool sustain)
{
   this->sustain = sustain;
   if (sustain)
      return;

   for (auto &tone : voices)
      tone->release_sustain();
}

void Instrument::render(float **mix_buffer, unsigned frames, unsigned channels)
{
   for (auto &tone : voices)
      if (tone->active())
         tone->render(mix_buffer, frames, channels);
}

void Instrument::reset()
{
   sustain = false;
   for (auto &tone : voices)
      tone->active(false);
}

void Voice::trigger(unsigned note, unsigned vel, unsigned sample_rate)
{
   m_velocity = vel / 127.0f;

   released = false;
   sustained = false;
   this->note = note;

   this->sample_rate = sample_rate;
   time_step = 1.0 / sample_rate;
   env.time_step = time_step;

   time = 0.0;
   active(vel != 0);
}

bool Voice::check_release_complete()
{
   if (released && time >= released_time + env.release)
   {
      sustained = false;
      released = false;
      active(false);
      return true;
   }
   else
      return false;
}

Filter::Filter(vector<float> b, vector<float> a)
   : b(move(b)), a(move(a))
{
   if (this->a.size() < 1)
      this->a.push_back(1.0f);
   if (this->b.size() < 1)
      this->b.push_back(1.0f);
   reset();
}

Filter::Filter()
   : Filter({}, {})
{}

float Filter::process(float samp)
{
   float iir_sum = samp;
   for (unsigned i = 1; i < a.size(); i++)
      iir_sum -= a[i] * buffer[i - 1];
   iir_sum /= a[0];

   buffer.push_front(iir_sum);

   float fir_sum = 0.0f;
   for (unsigned i = 0; i < b.size(); i++)
      fir_sum += buffer[i] * b[i];

   buffer.pop_back();
   return fir_sum;
}

void Filter::reset()
{
   buffer.clear();
   auto len = max(a.size(), b.size()) - 1;
   buffer.resize(len);
}

float Envelope::envelope(float time, bool released)
{
   if (released)
   {
      float release_factor = 8.0 * time_step / release;
      amp -= amp * release_factor;
   }
   else if (time >= attack + delay)
      amp = sustain_level;
   else if (time >= attack)
   {
      float lerp = (time - attack) / delay;
      amp = (1.0 - lerp) + sustain_level * lerp;
   }
   else
      amp = time / attack;

   return gain * amp;
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double besseli0(double x)
{
   double sum = 0.0;

   double factorial = 1.0;
   double factorial_mult = 0.0;
   double x_pow = 1.0;
   double two_div_pow = 1.0;
   double x_sqr = x * x;

   /* Approximate. This is an infinite sum.
    * Luckily, it converges rather fast. */
   for (unsigned i = 0; i < 18; i++)
   {
      sum += x_pow * two_div_pow / (factorial * factorial);

      factorial_mult += 1.0;
      x_pow *= x_sqr;
      two_div_pow *= 0.25;
      factorial *= factorial_mult;
   }

   return sum;
}

// index range = [-1, 1)
static inline double kaiser_window(double index, double beta)
{
   return besseli0(beta * sqrt(1.0 - index * index));
}

static inline double sinc(double v)
{
   if (fabs(v) < 0.0001)
      return 1.0;
   else
      return sin(v) / v;
}

PolyphaseBank::PolyphaseBank(unsigned taps, unsigned phases, double cutoff, double beta)
   : taps(taps), phases(phases)
{
   buffer.resize(taps * phases);
   std::vector<float> tmp(taps * phases);

   int elems = taps * phases;
   double sidelobes = taps / 2.0;
   double window_mod = 1.0 / kaiser_window(0.0, beta);

   for (int i = 0; i < elems; i++)
   {
      double window_phase = double(i) / elems;
      window_phase = 2.0 * window_phase - 1.0;
      double sinc_phase = window_phase * sidelobes;

      tmp[i] = cutoff * sinc(M_PI * cutoff * sinc_phase) * kaiser_window(window_phase, beta) * window_mod;
   }

   for (unsigned t = 0; t < taps; t++)
      for (unsigned p = 0; p < phases; p++)
         buffer[p * taps + t] = tmp[t * phases + p];
}

