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

AirSynth::AirSynth(const char *path)
{
   tones_noise.resize(32);
   for (auto& tone : tones_noise)
   {
      tone = unique_ptr<NoiseIIR>(new NoiseIIR);
      static_cast<NoiseIIR*>(tone.get())->set_filter_bank(&filter_bank);
   }

   tones_square.resize(256);
   for (auto& tone : tones_square)
      tone = unique_ptr<Square>(new Square);

   tones_saw.resize(256);
   for (auto& tone : tones_saw)
      tone = unique_ptr<Sawtooth>(new Sawtooth);

   sustain.resize(16);

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
   if (sndfile)
   {
      sf_writef_float(sndfile, wav_buffer.data(), wav_buffer.size() / 2);
      sf_close(sndfile);
   }
}

void AirSynth::set_format(unsigned sample_rate, unsigned max_frames, unsigned channels)
{
   Synthesizer::set_format(sample_rate, max_frames, channels);
   for (auto& tone : tones_noise)
      tone->time_step = 1.0 / sample_rate;
   for (auto& tone : tones_square)
      tone->time_step = 1.0 / sample_rate;
   for (auto& tone : tones_saw)
      tone->time_step = 1.0 / sample_rate;
}

void AirSynth::set_note(unsigned channel, unsigned note, unsigned velocity)
{
   //auto& tones = note > 60 ? tones_square : tones_noise;
   auto& tones = tones_noise;

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
   release(tones_saw);
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

void AirSynth::render(float *buffer, unsigned frames)
{
   render_synth(tones_noise, buffer, mix_buffer.data(), frames);
   render_synth(tones_square, buffer, mix_buffer.data(), frames);
   render_synth(tones_saw, buffer, mix_buffer.data(), frames);

   if (sndfile)
      wav_buffer.insert(end(wav_buffer), buffer, buffer + channels * frames);
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

AudioThreadLoop::AudioThreadLoop(std::shared_ptr<Synthesizer> synth,
            std::shared_ptr<AudioDriver> driver)
   : synth(move(synth)), driver(move(driver))
{
   dead.store(false);
   mixer_thread = thread(&AudioThreadLoop::mixer_loop, this);
}

AudioThreadLoop::~AudioThreadLoop()
{
   dead.store(true);
   if (mixer_thread.joinable())
      mixer_thread.join();
}

void AudioThreadLoop::mixer_loop()
{
   float buffer[2 * 64];
   int16_t ibuffer[2 * 64];
   synth->set_format(44100, 64, 2);

   while (!dead.load())
   {
      fill(begin(buffer), end(buffer), 0.0f);
      synth->render(buffer, 64);
      float_to_s16(ibuffer, buffer, 2 * 64);
      driver->write(ibuffer, 64);
   }
}

void AudioThreadLoop::float_to_s16(int16_t *out, const float *in, unsigned samples)
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

void Synthesizer::mixer_add(float *out, const float *in, unsigned samples)
{
   for (unsigned s = 0; s < samples; s++)
      out[s] += in[s];
}

