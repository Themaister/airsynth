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

#include <lv2synth.hpp>
#include "../synth.hpp"
#include "noise.peg"
#include <cmath>

using namespace std;

static inline float clamp(float v, float minimum, float maximum)
{
   if (v < minimum)
      return minimum;
   else if (v > maximum)
      return maximum;
   return v;
}

template<typename VoiceType>
class AirSynthVoice : public LV2::Voice
{
   public:
      AirSynthVoice(double rate)
         : m_key(LV2::INVALID_KEY), m_rate(rate)
      {}

      void on(unsigned char key, unsigned char velocity)
      {
         m_key = key;
         if (key == LV2::INVALID_KEY)
         {
            for (auto &voice : m_voice)
               voice.active(false);
         }
         else
         {
            Envelope env;
            env.gain = exp(clamp(*p(peg_rolloff), peg_ports[peg_rolloff].min, peg_ports[peg_rolloff].max) * (69.0f - key));
            env.attack = clamp(*p(peg_attack), peg_ports[peg_attack].min, peg_ports[peg_attack].max);
            env.delay = clamp(*p(peg_delay), peg_ports[peg_delay].min, peg_ports[peg_delay].max);
            env.sustain_level = clamp(*p(peg_sustain), peg_ports[peg_sustain].min, peg_ports[peg_sustain].max);
            env.release = clamp(*p(peg_release), peg_ports[peg_release].min, peg_ports[peg_release].max);

            m_num_voices = unsigned(round(clamp(*p(peg_num_osc), peg_ports[peg_num_osc].min, peg_ports[peg_num_osc].max)));

            for (unsigned i = 0; i < m_num_voices; i++)
            {
               int transpose = int(*p(peg_transpose0 + i));
               int out_key = max(int(key) + transpose, 0);
               m_voice[i].set_envelope(env);
               m_voice[i].trigger(out_key, velocity, m_rate,
                     clamp(*p(peg_detune0 + i), peg_ports[peg_detune0 + i].min, peg_ports[peg_detune0 + i].max));
            }
         }
      }

      void off(unsigned char velocity)
      {
         for (unsigned i = 0; i < m_num_voices; i++)
            m_voice[i].release(m_sustained);
      }

      void sustain(bool enable)
      {
         m_sustained = enable;
         if (!m_sustained)
         {
            for (unsigned i = 0; i < m_num_voices; i++)
               m_voice[i].release_sustain();
         }
      }

      unsigned char get_key() const { return m_key; }

      void render(uint32_t from, uint32_t to)
      {
         if (m_key == LV2::INVALID_KEY)
            return;

         float *buf[2] = { p(peg_output_left) + from, p(peg_output_right) + from };
         for (unsigned i = 0; i < m_num_voices; i++)
         {
            float panning = clamp(*p(peg_pan0 + i), -1.0f, 1.0f);
            float amp[2] = { min(1.0f - panning, 1.0f), min(1.0f + panning, 1.0f) };
            m_voice[i].render(buf, amp, to - from, 2);
         }
         if (!m_voice[0].active())
            m_key = LV2::INVALID_KEY;
      }

      void panic()
      {
         on(LV2::INVALID_KEY, 0);
      }

   private:
      unsigned char m_key;
      unsigned m_rate;
      bool m_sustained = false;
      unsigned m_num_voices = 1;
      VoiceType m_voice[4];
};

using AirSynthNoiseIIR = AirSynthVoice<NoiseIIR>;
using AirSynthSquare = AirSynthVoice<Square>;
using AirSynthSawtooth = AirSynthVoice<Sawtooth>;

template<typename VoiceType>
class AirSynthLV2 : public LV2::Synth<VoiceType, AirSynthLV2<VoiceType>>
{
   public:
      AirSynthLV2(double rate)
         : LV2::Synth<VoiceType, AirSynthLV2<VoiceType>>(peg_n_ports, peg_midi)
      {
         for (unsigned i = 0; i < 64; i++)
            this->add_voices(new VoiceType(rate));
         this->add_audio_outputs(peg_output_left, peg_output_right);
      }

      void handle_midi(uint32_t size, unsigned char *data)
      {
         if (size != 3)
            return;

         if (size == 3 && ((data[0] & 0xf0) == 0x90))
         {
            for (auto &voice : this->m_voices)
            {
               if (voice->get_key() == LV2::INVALID_KEY)
               {
                  voice->on(data[1], data[2]);
                  break;
               }
            }
         }
         else if (size == 3 && ((data[0] & 0xf0) == 0x80))
         {
            for (auto &voice : this->m_voices)
               if (voice->get_key() == data[1])
                  voice->off(data[2]);
         }
         else if (size == 3 && ((data[0] & 0xf0) == 0xb0 && data[1] == 64)) // Sustain on my CP33. Not sure if there's a standard control for sustain ...
         {
            for (auto &voice : this->m_voices)
               voice->sustain(data[2]);
         }
         else if ((size == 1 && data[0] == 0xff) ||
                  (size == 3 && ((data[0] & 0xf0) == 0xb0) && data[1] == 120)) // Reset, All Sound Off
         {
            for (auto &voice : this->m_voices)
               voice->panic();
         }
         else if ((size == 3 && ((data[0] & 0xf0) == 0xb0) && data[1] == 123) ||
                     (size == 1 && data[0] == 0xfc)) // All Notes Off, STOP
         {
            for (auto &voice : this->m_voices)
            {
               voice->sustain(false);
               voice->off(0);
            }
         }
      }
};

int airsynth_register_noiseiir = AirSynthLV2<AirSynthNoiseIIR>::register_class("git://github.com/Themaister/airsynth/noise");
int airsynth_register_sawtooth = AirSynthLV2<AirSynthSawtooth>::register_class("git://github.com/Themaister/airsynth/saw");
int airsynth_register_square = AirSynthLV2<AirSynthSquare>::register_class("git://github.com/Themaister/airsynth/square");

