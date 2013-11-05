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


#include "audio_driver.hpp"

using namespace std;

void AudioCallback::process_midi(MidiRawData midi_raw)
{
   process_midi(get_event(midi_raw));
}

AudioCallback::MidiEvent AudioCallback::get_event(MidiRawData midi_raw)
{
   Event e = Event::None;
   uint8_t event = midi_raw[0];
   unsigned channel = 0;
   uint8_t type = event >> 4;

   if (type >= 0x8 && type <= 0xe)
   {
      channel = event & 0xf;
      e = static_cast<Event>(type & 0x7);
   }
   else if (type == 0xf)
   {
      type = event & 0xf;
      switch (type)
      {
         case  1: e = Event::TimeCodeQuarter; break;
         case  2: e = Event::SongPosition; break;
         case  3: e = Event::SongSelect; break;
         case  6: e = Event::TuneRequest; break;
         case  8: e = Event::TimingClock; break;
         case 10: e = Event::Start; break;
         case 11: e = Event::Continue; break;
         case 12: e = Event::Stop; break;
         case 14: e = Event::ActiveSensing; break;
         case 15: e = Event::Reset; break;
      }
   }

   return {e, channel, midi_raw[1], midi_raw[2]};
}

void AudioDriver::run()
{
   unique_lock<mutex> unilock;
   unilock.lock();
   while (!dead)
      cond.wait(unilock);
   unilock.unlock();
}

void AudioDriver::kill()
{
   lock_guard<mutex> holder{lock};
   dead = true;
   cond.notify_all();
}

