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
            m_voice.active(false);
         else
         {
            m_voice.set_envelope(
               exp(*p(peg_rolloff) * (69.0f - key)),
               *p(peg_attack), *p(peg_delay), *p(peg_sustain), *p(peg_release)
            );

            m_voice.trigger(key, velocity, m_rate);
         }
      }

      void off(unsigned char velocity)
      {
         m_voice.release(m_sustained);
      }

      void sustain(bool enable)
      {
         m_sustained = enable;
         if (!m_sustained)
            m_voice.release_sustain();
      }

      unsigned char get_key() const { return m_key; }

      void render(uint32_t from, uint32_t to)
      {
         if (m_key == LV2::INVALID_KEY)
            return;

         float *buf[2] = { p(peg_output_left) + from, p(peg_output_right) + from };
         m_voice.render(buf, to - from, 2);
         if (!m_voice.active())
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
      VoiceType m_voice;
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
         else if (size == 1 && data[0] == 0xff)
         {
            for (auto &voice : this->m_voices)
               voice->panic();
         }
      }
};

int airsynth_register_noiseiir = AirSynthLV2<AirSynthNoiseIIR>::register_class("git://github.com/Themaister/airsynth/noise");
int airsynth_register_sawtooth = AirSynthLV2<AirSynthSawtooth>::register_class("git://github.com/Themaister/airsynth/saw");
int airsynth_register_square = AirSynthLV2<AirSynthSquare>::register_class("git://github.com/Themaister/airsynth/square");

