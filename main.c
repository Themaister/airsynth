#include <asoundlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define BUFFER_SIZE 16

int main(int argc, char *argv[])
{
   snd_rawmidi_t *mid;
   int ret = snd_rawmidi_open(&mid, NULL, argc >= 2 ? argv[1] : "virtual", 0);
   if (ret < 0)
      return 1;

   uint8_t buffer[BUFFER_SIZE];

   for (;;)
   {
      ssize_t ret = snd_rawmidi_read(mid, buffer, sizeof(buffer));
      if (ret < 0)
         break;

      for (int i = 0; i < ret; i++)
         fprintf(stderr, "Data: 0x%x\n", buffer[i]);
   }

   snd_rawmidi_close(mid);
}

