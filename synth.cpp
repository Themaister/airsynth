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

#include "flute_iir.h"

using namespace std;

AirSynth::AirSynth(const char *device, const char *path)
{
   tones_noise.resize(32);
   for (auto& tone : tones_noise)
   {
      tone = unique_ptr<NoiseIIR>(new NoiseIIR);
      static_cast<NoiseIIR*>(tone.get())->set_filter_bank(&filter_bank);
   }

   tones_square.resize(32);
   for (auto& tone : tones_square)
      tone = unique_ptr<Square>(new Square);

   sustain.resize(16);
   audio = unique_ptr<AudioDriver>(new ALSADriver(device, 44100, 2));
   dead.store(false);
   mixer_thread = thread(&AirSynth::mixer_loop, this);

   if (path)
   {
      SF_INFO info = {0};
      info.samplerate = 44100;
      info.channels = 2;
      info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
      sndfile = sf_open(path, SFM_WRITE, &info);
   }

   if (sndfile)
      wav_buffer.reserve(256 * 1024 * 1024); // Keep it simple for now. Should be a threaded dump loop.
}

AirSynth::~AirSynth()
{
   dead.store(true);
   if (mixer_thread.joinable())
      mixer_thread.join();

   if (sndfile)
   {
      sf_writef_float(sndfile, wav_buffer.data(), wav_buffer.size() / 2);
      sf_close(sndfile);
   }
}

void AirSynth::set_note(unsigned channel, unsigned note, unsigned velocity)
{
   //auto& tones = note > 60 ? tones_square : tones_noise;
   auto& tones = tones_square;

   if (velocity == 0)
   {
      for (auto &tone : tones)
      {
         if (tone->active->load() && tone->note == note &&
               !tone->released && !tone->sustained && tone->channel == channel)
         {
            lock_guard<mutex> lock{*tone->lock};
            if (sustain[channel])
               tone->sustained = true;
            else
            {
               tone->released = true;
               tone->released_time = tone->time;
            }
         }
      }
   }
   else
   {
      auto itr = find_if(begin(tones), end(tones),
            [](const unique_ptr<Instrument> &t) { return !t->active->load(); });

      if (itr == end(tones))
      {
         fprintf(stderr, "Couldn't find any notes for note: %u, vel: %u.\n",
               note, velocity);
         return;
      }

      fprintf(stderr, "Trigger note: %u.\n", note);
      (*itr)->reset(channel, note, velocity);
   }
}

void AirSynth::set_sustain(unsigned channel, bool sustain)
{
   this->sustain[channel] = sustain;
   if (sustain)
      return;

   auto release = [channel](vector<unique_ptr<Instrument>>& tones) {
      for (auto &tone : tones)
      {
         if (tone->active->load() && tone->sustained && tone->channel == channel)
         {
            lock_guard<mutex> lock{*tone->lock};
            tone->released = true;
            tone->released_time = tone->time;
            tone->sustained = false;
         }
      }
   };

   release(tones_noise);
   release(tones_square);
}

void AirSynth::float_to_s16(int16_t *out, const float *in, unsigned samples)
{
   for (unsigned s = 0; s < samples; s++)
   {
      int32_t v = int32_t(round(in[s] * 0x7fff));
      if (v > 0x7fff)
      {
         fprintf(stderr, "Clipping (+): %d.\n", v);
         out[s] = 0x7fff;
      }
      else if (v < -0x8000)
      {
         fprintf(stderr, "Clipping (-): %d.\n", v);
         out[s] = -0x8000;
      }
      else
         out[s] = v;
   }
}

void AirSynth::mixer_add(float *out, const float *in, unsigned samples)
{
   for (unsigned s = 0; s < samples; s++)
      out[s] += in[s];
}

void AirSynth::render_synth(const vector<unique_ptr<Instrument>>& synth,
      float *mix_buffer, float *tmp_buffer, unsigned frames)
{
   for (auto &tone : synth)
   {
      if (!tone->active->load())
         continue;

      tone->render(tmp_buffer, frames);
      mixer_add(mix_buffer, tmp_buffer, 2 * frames);
   }
}

void AirSynth::mixer_loop()
{
   float buffer[2 * 64];
   float tmp[2 * 64];
   int16_t out_buffer[2 * 64];

   while (!dead.load())
   {
      fill(begin(buffer), end(buffer), 0.0f);

      render_synth(tones_noise, buffer, tmp, 64);
      render_synth(tones_square, buffer, tmp, 64);

      float_to_s16(out_buffer, buffer, 2 * 64);
      audio->write(out_buffer, 64);

      if (sndfile)
         wav_buffer.insert(end(wav_buffer), buffer, buffer + 2 * 64);
   }
}

void Instrument::reset(unsigned channel, unsigned note, unsigned vel)
{
   velocity = vel / 127.0f;

   released = false;
   sustained = false;
   this->note = note;
   this->channel = channel;

   time = 0.0;
   active->store(vel != 0);
}

bool Instrument::check_release_complete(double release)
{
   if (released && time >= released_time + release)
   {
      sustained = false;
      released = false;
      active->store(false);
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

void Square::reset(unsigned channel, unsigned note, unsigned velocity)
{
   double freq = 440.0 * pow(2.0f, (note - 69.0) / 12.0);

   period = unsigned(round(44100.0 * 64 / (2.0 * freq))); 

   delta = 0.5f;
   blipper_reset(blip);
   blipper_push_delta(blip, -0.25f, 0);

   env.attack = 0.60;
   env.delay = 0.60;
   env.sustain_level = 0.55;
   env.release = 0.8;

   Instrument::reset(channel, note, velocity);
}

void Square::render(float *out, unsigned frames)
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
      //fprintf(stderr, "Val: %d.\n", val);
      out[(s << 1) + 0] = out[(s << 1) + 1] = filter.process(val) * env.envelope(time, released) * velocity;

      time += time_step;
   }

   fill(out + (s << 1), out + (frames << 1), 0.0f);
}

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

double Envelope::envelope(double time, bool released)
{
   if (released)
   {
      double release_factor = 8.0 * time_step / release;
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

double PolyphaseBank::sinc(double v) const
{
   if (fabs(v) < 0.0001)
      return 1.0;
   else
      return sin(v) / v;
}

PolyphaseBank::PolyphaseBank(unsigned taps, unsigned phases)
   : taps(taps), phases(phases)
{
   buffer.resize(taps * phases);
   std::vector<float> tmp(taps * phases);
   int elems = taps * phases;
   for (int i = 0; i < elems; i++)
   {
      double phase = M_PI * double(i - (elems >> 1)) / phases;
      double window_phase = phase / taps;
      tmp[i] = 0.75 * sinc(0.75 * phase) * cos(window_phase);
   }

   for (unsigned t = 0; t < taps; t++)
      for (unsigned p = 0; p < phases; p++)
         buffer[p * taps + t] = tmp[t * phases + p];
}

