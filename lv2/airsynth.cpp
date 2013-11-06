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
            noise.reset(0, key, velocity, false);
      }

      void off(unsigned char velocity)
      {
         noise.release_note(0, m_key, false);
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
};

int airsynth_register_key = AirSynthLV2::register_class(peg_uri);

