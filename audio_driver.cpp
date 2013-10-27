#include "audio_driver.hpp"
#include <stdexcept>

using namespace std;

ALSADriver::ALSADriver(const char *device, unsigned rate, unsigned channels)
{
   if (!init(device, rate, channels))
   {
      if (pcm)
         snd_pcm_close(pcm);
      throw runtime_error("Failed to open ALSA device.\n");
   }
}

ALSADriver::~ALSADriver()
{
   snd_pcm_drain(pcm);
   snd_pcm_close(pcm);
}

bool ALSADriver::init(const char *device, unsigned rate, unsigned channels)
{
   if (snd_pcm_open(&pcm, device ? device : "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
      return false;

   snd_pcm_hw_params_t *params = NULL;
   snd_pcm_hw_params_alloca(&params);

   unsigned latency_usec = 16000;
   unsigned periods = 4;

   if (snd_pcm_hw_params_any(pcm, params) < 0)
      return false;
   if (snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
      return false;
   if (snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16) < 0)
      return false;
   if (snd_pcm_hw_params_set_channels(pcm, params, channels) < 0)
      return false;
   if (snd_pcm_hw_params_set_rate(pcm, params, rate, 0) < 0)
      return false;
   if (snd_pcm_hw_params_set_buffer_time_near(pcm, params, &latency_usec, nullptr) < 0)
      return false;
   if (snd_pcm_hw_params_set_periods_near(pcm, params, &periods, nullptr) < 0)
      return false;

   if (snd_pcm_hw_params(pcm, params) < 0)
      return false;

   return true;
}

bool ALSADriver::write(const int16_t *data, unsigned frames)
{
   const uint8_t *buffer = reinterpret_cast<const uint8_t*>(data);

   while (frames)
   {
      auto ret = snd_pcm_writei(pcm, buffer, frames);
      if (ret == -EPIPE || ret == -EINTR || ret == -ESTRPIPE)
      {
         if (snd_pcm_recover(pcm, ret, 1) < 0)
            return false;
      }
      else if (ret < 0)
         return false;
      else
      {
         frames -= ret;
         buffer += snd_pcm_frames_to_bytes(pcm, ret);
      }
   }

   return true;
}

