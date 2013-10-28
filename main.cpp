#include <asoundlib.h>
#include <getopt.h>

#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <deque>
#include <stdexcept>
#include <utility>

#include "synth.hpp"

using namespace std;

class MIDIReader
{
   public:
      MIDIReader(const char *device)
      {
         if (snd_rawmidi_open(&mid, NULL, device ? device : "virtual", 0) < 0)
            throw runtime_error("Failed to init MIDI device.\n");
      }

      ~MIDIReader()
      {
         snd_rawmidi_close(mid);
      }

      unsigned read(uint8_t *buffer, unsigned size)
      {
         ssize_t ret = snd_rawmidi_read(mid, buffer, size);
         if (ret < 0)
            throw runtime_error("Error reading MIDI bytes.\n");

         return ret;
      }

   private:
      snd_rawmidi_t *mid;
};

class MIDIBuffer
{
   public:
      enum class Event : unsigned
      {
         NoteOff = 0,
         NoteOn,
         Aftertouch,
         Control,
         Program,
         PitchWheel,

         SystemExclusive,
         TimeCodeQuarter,
         SongPosition,
         SongSelect,
         TuneRequest,
         SystemExclusiveEnd,

         TimingClock,
         Start,
         Continue,
         Stop,
         ActiveSensing,
         Reset,

         None
      };

      void write(const uint8_t *data, unsigned size)
      {
         buffer.insert(end(buffer), data, data + size);
      }

      void retire_events(Synthesizer &handler)
      {
         unsigned last_parsed = 0;
         unsigned ptr = 0;

         while (ptr < buffer.size())
         {
            Event event;
            unsigned chan;
            unsigned data_size;
            unsigned data[2] = {0};

            get_event(buffer[ptr], event, chan, data_size);

            if (event == Event::None) // Possible desync. Just continue and try to parse on a start byte.
            {
               ptr++;
               if (buffer[ptr] & 0x80)
                  last_parsed = ptr;
            }
            else
            {
               ptr++;
               if (ptr + data_size <= buffer.size())
               {
                  for (unsigned d = 0; d < data_size; d++)
                     data[d] = buffer[ptr + d];
                  handle_event(event, chan, data[0], data[1], handler);
                  ptr += data_size;
                  last_parsed = ptr;
               }
               else
                  ptr = buffer.size();
            }
         }

         buffer.erase(begin(buffer), begin(buffer) + last_parsed);
      }

   private:
      void get_event(uint8_t event, Event &e, unsigned &channel, unsigned &data_size)
      {
         e = Event::None;
         channel = 0;
         data_size = 0;

         uint8_t type = event >> 4;

         if (type >= 0x8 && type <= 0xe)
         {
            channel = event & 0xf;
            data_size = (type != 0xc && type != 0xd) ? 2 : 1;
            e = static_cast<Event>(type & 0x7);
         }
         else if (type == 0xf)
         {
            type = event & 0xf;
            switch (type)
            {
               case  1: e = Event::TimeCodeQuarter; data_size = 1; break;
               case  2: e = Event::SongPosition; data_size = 2; break;
               case  3: e = Event::SongSelect; data_size = 1; break;
               case  6: e = Event::TuneRequest; break;
               case  8: e = Event::TimingClock; break;
               case 10: e = Event::Start; break;
               case 11: e = Event::Continue; break;
               case 12: e = Event::Stop; break;
               case 14: e = Event::ActiveSensing; break;
               case 15: e = Event::Reset; break;
            }
         }
      }

      void handle_event(Event event, unsigned channel, uint8_t lo, uint8_t hi, Synthesizer &synth)
      {
         switch (event)
         {
            case Event::NoteOn:
               synth.set_note(channel, lo, hi);
               fprintf(stderr, "[ON] #%u, Key: %03u, Vel: %03u.\n", channel, lo, hi);
               break;

            case Event::NoteOff: // This doesn't seem to be really used.
               synth.set_note(channel, lo, 0);
               fprintf(stderr, "[OFF] #%u, Key: %03u, Vel: %03u.\n", channel, lo, hi);
               break;

            case Event::Control:
               if (lo == 64) // Sustain controller on my CP33.
                  synth.set_sustain(channel, hi);
               fprintf(stderr, "[CTRL] #%u, Control: %03u, Val: %03u.\n", channel, lo, hi);
               break;

            case Event::TimingClock:
            case Event::ActiveSensing:
               break;

            default:
               fprintf(stderr, "[%u] #%u, Lo: %03u, Hi: %03u.\n", (unsigned)event, channel, lo, hi);
         }
      }

      deque<uint8_t> buffer;
};

static void print_help(void)
{
   fprintf(stderr, "Usage: midi [-i/--input <MIDI input device>] [-d/--device <audio device>] [-h/--help]\n");
}

static char *midi_device;
static char *audio_device;
static void parse_cmdline(int argc, char *argv[])
{
   const struct option opts[] = {
      { "device", 1, NULL, 'd' },
      { "input", 1, NULL, 'i' },
      { "help", 0, NULL, 'h' },
   };

   const char *optstring = "d:i:h";
   for (;;)
   {
      int c = getopt_long(argc, argv, optstring, opts, NULL);
      if (c == -1)
         break;

      switch (c)
      {
         case 'd':
            free(audio_device);
            audio_device = strdup(optarg);
            break;

         case 'i':
            free(midi_device);
            midi_device = strdup(optarg);
            break;

         case 'h':
            print_help();
            exit(EXIT_SUCCESS);
            break;

         case '?':
            print_help();
            exit(EXIT_FAILURE);
      }
   }
}

int main(int argc, char *argv[])
{
   parse_cmdline(argc, argv);

   try
   {
      MIDIReader midi_reader{midi_device};
      MIDIBuffer midi_buffer;
      AirSynth synth{audio_device};

#define MIDI_BUFFER_SIZE 512
      uint8_t buffer[MIDI_BUFFER_SIZE];

      for (;;)
      {
         unsigned ret = midi_reader.read(buffer, sizeof(buffer));
         midi_buffer.write(buffer, ret);
         midi_buffer.retire_events(synth);
      }

      return EXIT_SUCCESS;
   }
   catch (const exception &e)
   {
      fprintf(stderr, "Fatal exception: %s.\n", e.what());
      return EXIT_FAILURE;
   }
}

