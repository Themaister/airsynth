#ifndef AUDIO_DRIVER_HPP__
#define AUDIO_DRIVER_HPP__

#include <asoundlib.h>
#include <cstdint>

class AudioDriver
{
   public:
      virtual ~AudioDriver() = default;
      virtual bool write(const std::int16_t *data, unsigned frames) = 0;
};

class ALSADriver : public AudioDriver
{
   public:
      ALSADriver(const char *device, unsigned rate, unsigned channels);
      ~ALSADriver();

      ALSADriver(ALSADriver&&) = delete;
      void operator=(ALSADriver&&) = delete;

      bool write(const std::int16_t *data, unsigned frames) override;

   private:
      snd_pcm_t *pcm = nullptr;
      bool init(const char *device, unsigned rate, unsigned channels);
};


#endif

