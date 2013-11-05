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


#include <getopt.h>

#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <deque>
#include <stdexcept>
#include <utility>
#include <functional>

#include <cstring>
#include <signal.h>
#include "synth.hpp"

using namespace std;

namespace Signal
{
   static function<void ()> signal_func;
   static void handler(int)
   {
      signal_func();
   }
}

static void print_help(void)
{
   fprintf(stderr, "Usage: airsynth [-o/--output <wav file>] [-h/--help]\n");
}

static void parse_cmdline(int argc, char *argv[])
{
   const struct option opts[] = {
      { "help", 0, NULL, 'h' },
   };

   const char *optstring = "h";
   for (;;)
   {
      int c = getopt_long(argc, argv, optstring, opts, NULL);
      if (c == -1)
         break;

      switch (c)
      {
         case 'h':
            print_help();
            exit(EXIT_SUCCESS);
            break;

         case '?':
            print_help();
            exit(EXIT_FAILURE);
      }
   }

   if (optind < argc)
   {
      print_help();
      exit(EXIT_FAILURE);
   }
}

static void register_signals(std::function<void ()> func)
{
   Signal::signal_func = func;

   struct sigaction sa;
   memset(&sa, 0, sizeof(sa));
   sa.sa_handler = Signal::handler;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = SA_RESTART;
   sigaction(SIGINT, &sa, NULL);
   sigaction(SIGTERM, &sa, NULL);
}

int main(int argc, char *argv[])
{
   parse_cmdline(argc, argv);

   try
   {
      shared_ptr<Synthesizer> synth = make_shared<AirSynth>();
      auto audio_driver = make_shared<JACKDriver>(synth, 2);

      register_signals([&audio_driver] {
         audio_driver->kill();
      });

      audio_driver->run();

      fprintf(stderr, "Quitting ...\n");
      return EXIT_SUCCESS;
   }
   catch (const exception &e)
   {
      fprintf(stderr, "Fatal exception: %s.\n", e.what());
      return EXIT_FAILURE;
   }
}

