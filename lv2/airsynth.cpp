#include <lv2synth.hpp>
#include "../synth.hpp"
#include "airsynth.peg"

static PolyphaseBank filter_bank{32, 1 << 13};

class AirSynthVoice : public LV2::Voice
{
   public:
      AirSynthVoice(double rate)
         : m_key(LV2::INVALID_KEY), m_rate(rate), noise(&filter_bank)
      {}

      void on(unsigned char key, unsigned char velocity)
      {
         m_key = key;
         if (key == LV2::INVALID_KEY)
            noise.active(false);
         else
            noise.reset(0, key, velocity, m_rate);
      }

      void off(unsigned char velocity)
      {
         noise.release_note(0, m_key, m_sustained);
      }

      void sustain(bool enable)
      {
         m_sustained = true;
         if (!m_sustained)
            noise.release_sustain(0);
      }

      unsigned char get_key() const { return m_key; }

      void render(uint32_t from, uint32_t to)
      {
         if (m_key == LV2::INVALID_KEY)
            return;

         float *buf[2] = { p(peg_output_left) + from, p(peg_output_right) + from };
         noise.render(buf, to - from, 2);
         if (!noise.active())
            m_key = LV2::INVALID_KEY;
      }

   private:
      unsigned char m_key;
      unsigned m_rate;
      bool m_sustained = false;
      NoiseIIR noise;
};

class AirSynthLV2 : public LV2::Synth<AirSynthVoice, AirSynthLV2>
{
   public:
      AirSynthLV2(double rate)
         : LV2::Synth<AirSynthVoice, AirSynthLV2>(peg_n_ports, peg_midi)
      {
         for (unsigned i = 0; i < 32; i++)
            add_voices(new AirSynthVoice(rate));
         add_audio_outputs(peg_output_left, peg_output_right);
      }

      void handle_midi(uint32_t size, unsigned char *data)
      {
         if (size != 3)
            return;

         if ((data[0] & 0xf0) == 0x90)
         {
            for (auto &voice : m_voices)
            {
               if (voice->get_key() == LV2::INVALID_KEY)
               {
                  voice->on(data[1], data[2]);
                  break;
               }
            }
         }
         else if ((data[0] & 0xf0) == 0x80)
         {
            for (auto &voice : m_voices)
               if (voice->get_key() == data[1])
                  voice->off(data[2]);
         }
         else if ((data[0] & 0xf0) == 0xb0 && data[1] == 64) // Sustain on my CP33
         {
            for (auto &voice : m_voices)
               voice->sustain(data[2]);
         }
      }
};

int airsynth_register_key = AirSynthLV2::register_class(peg_uri);

