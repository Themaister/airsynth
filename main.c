#include <asoundlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

enum midi_event
{
   MIDI_NOTE_OFF = 0,
   MIDI_NOTE_ON,
   MIDI_AFTERTOUCH,
   MIDI_CONTROL,
   MIDI_PROGRAM,
   MIDI_PITCH_WHEEL,

   MIDI_SYSTEM_EXCLUSIVE,
   MIDI_TIME_CODE_QUARTER,
   MIDI_SONG_POSITION,
   MIDI_SONG_SELECT,
   MIDI_TUNE_REQUEST,
   MIDI_SYSTEM_EXCLUSIVE_END,

   MIDI_TIMING_CLOCK,
   MIDI_START,
   MIDI_CONTINUE,
   MIDI_STOP,
   MIDI_ACTIVE_SENSING,
   MIDI_RESET,

   MIDI_DUMMY = INT_MAX
};

typedef void (*midi_event_cb)(enum midi_event event, unsigned channel, uint8_t lo, uint8_t hi);

#define MIDI_BUFFER_SIZE 512

// Don't bother with ring buffering. The buffer is so small, and we parse most of it in one go.
struct midi_buffer
{
   uint8_t buffer[MIDI_BUFFER_SIZE];
   unsigned size;
};

static void midi_buffer_init(struct midi_buffer *buf)
{
   memset(buf, 0, sizeof(*buf));
}

static uint8_t *midi_buffer_data(struct midi_buffer *buf)
{
   return buf->buffer + buf->size;
}

static unsigned midi_buffer_write_avail(struct midi_buffer *buf)
{
   return MIDI_BUFFER_SIZE - buf->size;
}

static void midi_buffer_write(struct midi_buffer *buf, const uint8_t *data, unsigned size)
{
   if (data)
      memcpy(buf->buffer + buf->size, data, size);
   buf->size += size;
}

static void get_event(uint8_t event, enum midi_event *out_event, unsigned *channel, unsigned *data_size)
{
   enum midi_event e = MIDI_DUMMY;
   uint8_t type = event >> 4;

   if (type >= 0x8 && type <= 0xe)
   {
      *channel = event & 0xf;
      *data_size = (type != 0xc && type != 0xd) ? 2 : 1;
      e = type & 0x7;
   }
   else if (type == 0xf)
   {
      *data_size = 0;
      type = event & 0xf;
      switch (type)
      {
         //case  0: e = MIDI_SYSTEM_EXCLUSIVE; break; // Unknown size. Wait for SYSTEM_EXCLUSIVE_END.
         case  1: e = MIDI_TIME_CODE_QUARTER; *data_size = 1; break;
         case  2: e = MIDI_SONG_POSITION; *data_size = 2; break;
         case  3: e = MIDI_SONG_SELECT; *data_size = 1; break;
         case  6: e = MIDI_TUNE_REQUEST; break;
         //case  7: e = MIDI_SYSTEM_EXCLUSIVE_END; break;
         case  8: e = MIDI_TIMING_CLOCK; break;
         case 10: e = MIDI_START; break;
         case 11: e = MIDI_CONTINUE; break;
         case 12: e = MIDI_STOP; break;
         case 14: e = MIDI_ACTIVE_SENSING; break;
         case 15: e = MIDI_RESET; break;
      }
   }

   *out_event = e;
}

static void midi_buffer_retire_events(struct midi_buffer *buf, midi_event_cb cb)
{
   unsigned last_parsed = 0;
   unsigned ptr = 0;
   while (ptr < buf->size)
   {
      enum midi_event event = MIDI_DUMMY;
      unsigned chan = 0;
      unsigned data_size = 0;
      unsigned data[2] = {0};

      get_event(buf->buffer[ptr], &event, &chan, &data_size);

      if (event == MIDI_DUMMY) // Possible desync. Just continue and try to parse on a start byte.
      {
         ptr++;
         if (buf->buffer[ptr] & 0x80)
            last_parsed = ptr;
      }
      else
      {
         ptr++;
         if (ptr + data_size <= buf->size)
         {
            for (unsigned d = 0; d < data_size; d++)
               data[d] = buf->buffer[ptr + d];
            cb(event, chan, data[0], data[1]);
            ptr += data_size;
            last_parsed = ptr;
         }
         else
            ptr = buf->size;
      }
   }

   memmove(buf->buffer, buf->buffer + last_parsed, buf->size - last_parsed);
   buf->size -= last_parsed;
}

static void midi_cb(enum midi_event event, unsigned channel, uint8_t lo, uint8_t hi)
{
   switch (event)
   {
      case MIDI_NOTE_ON:
         fprintf(stderr, "[ON] #%u, Key: %03u, Vel: %03u.\n", channel, lo, hi);
         break;

      case MIDI_NOTE_OFF:
         fprintf(stderr, "[OFF] #%u, Key: %03u, Vel: %03u.\n", channel, lo, hi);
         break;

      case MIDI_CONTROL:
         fprintf(stderr, "[CTRL] #%u, Control: %03u, Val: %03u.\n", channel, lo, hi);
         break;

      case MIDI_TIMING_CLOCK:
      case MIDI_ACTIVE_SENSING:
         break;

      default:
         fprintf(stderr, "[%u] #%u, Lo: %03u, Hi: %03u.\n", (unsigned)event, channel, lo, hi);
   }
}

int main(int argc, char *argv[])
{
   snd_rawmidi_t *mid;
   int ret = snd_rawmidi_open(&mid, NULL, argc >= 2 ? argv[1] : "virtual", 0);
   if (ret < 0)
      return 1;

   struct midi_buffer midi_buf;
   midi_buffer_init(&midi_buf);

   for (;;)
   {
      ssize_t ret = snd_rawmidi_read(mid, midi_buffer_data(&midi_buf), midi_buffer_write_avail(&midi_buf));
      if (ret < 0)
         break;

      midi_buffer_write(&midi_buf, NULL, ret);
      midi_buffer_retire_events(&midi_buf, midi_cb);
   }

   snd_rawmidi_close(mid);
}

