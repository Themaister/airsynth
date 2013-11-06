#include "audio_driver.hpp"
#include <stdexcept>
#include <cstdio>

#include <jack/midiport.h>

using namespace std;

JACKDriver::JACKDriver(shared_ptr<AudioCallback> cb, unsigned channels)
   : AudioDriver(move(cb))
{
   if (!init(channels))
   {
      term();
      throw runtime_error("Failed to initialize JACK.");
   }
}

JACKDriver::~JACKDriver()
{
   term();
}

void JACKDriver::term()
{
   if (client)
   {
      for (auto port : audio_ports)
         jack_port_unregister(client, port);
   }

   if (client && midi_port)
      jack_port_unregister(client, midi_port);

   if (client)
   {
      jack_deactivate(client);
      jack_client_close(client);
   }
}

static int process_cb(jack_nframes_t nframes, void *data)
{
   return reinterpret_cast<JACKDriver*>(data)->process(nframes);
}

bool JACKDriver::init(unsigned channels)
{
   fprintf(stderr, "Initializing JACK ...\n");
   client = jack_client_open("AirSynth", JackNullOption, nullptr);
   if (!client)
   {
      fprintf(stderr, "Failed to create JACK client. Server might not be running ...\n");
      return false;
   }

   if (channels != 1 && channels != 2)
      return false;

   jack_set_process_callback(client, process_cb, this);

   for (unsigned c = 0; c < channels; c++)
   {
      static const char *stereo_chans[] = { "left", "right" };
      jack_port_t *port = jack_port_register(client, channels == 2 ? stereo_chans[c] : "output",
            JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

      if (!port)
         return false;

      audio_ports.push_back(port);
      target_ptrs.push_back(nullptr);
   }

   midi_port = jack_port_register(client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
   if (!midi_port)
      return false;

   auto max_frames = jack_get_buffer_size(client);
   fprintf(stderr, "Got JACK buffer size: %u frames.\n", unsigned(max_frames));
   unsigned sample_rate = jack_get_sample_rate(client);
   fprintf(stderr, "Got JACK sample rate: %u Hz.\n", sample_rate);
   audio_cb->configure_audio(sample_rate, channels);

   if (jack_activate(client) < 0)
      return false;

   return true;
}

int JACKDriver::process(jack_nframes_t frames)
{
   void *midi = jack_port_get_buffer(midi_port, frames);
   auto events = jack_midi_get_event_count(midi);
   for (unsigned i = 0; i < events; i++)
   {
      jack_midi_event_t event;
      jack_midi_event_get(&event, midi, i);
      audio_cb->process_midi({event.buffer[0], event.buffer[1], event.buffer[2]});
   }

   for (unsigned i = 0; i < target_ptrs.size(); i++)
   {
      target_ptrs[i] = static_cast<float*>(jack_port_get_buffer(audio_ports[i], frames));
      fill(target_ptrs[i], target_ptrs[i] + frames, 0.0f);
   }
   audio_cb->process_audio(target_ptrs.data(), frames);

   return 0;
}

