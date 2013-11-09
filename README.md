## AirSynth

AirSynth is a simple polyphonic softsynth for LV2 (and JACK). It currently features three instruments.

- Noise/IIR. Uses a filter to create sharp resonances at harmonics. The input is white noise. Nice for warm pad-like sounds. This instrument is very CPU intensive, so more than 15 voices at a time can bring the CPU to its knees.
- Bandlimited Sawtooth. Uses the BLIP method to implement a sawtooth without aliasing.
- Bandlimited Square. Same as above.

Sustain pedal is "supported". The sustain signal is assumed to have control ID #64 in MIDI, which maps to my Yamaha CP33 piano.

### Features
The feature set is quite sparse. There is currently no GUI, but the LV2 plugin has some basic tweakables which you can tweak with sliders:

- Standard ADSR. Attack and delay are linear, release rolls off exponentially.
- Up to 4 oscillators per voice, with per-oscillator detuning. This gives a really "phat" sound for especially sawtooth.
- Velocity rolloff for high notes. Noise/IIR instrument has a tendency to have a lower volume for bass notes. The rolloff boosts volume for lower notes, and lowers it for higher notes.

### Building LV2 plugin
Make sure lv2-c++-tools is installed. Airsynth is written in C++11, so a very recent G++ or Clang++ is required.

    cd lv2
    make
    make install
    
By default, the plugin is installed to /usr/lib/lv2/airsynth.lv2.

### Building standalone JACK synth
Airsynth can also function as a self-hosted JACK instrument. It uses JACK for both MIDI and audio.
To build you need libjack installed and a recent G++ or Clang++.
Using this is mostly just for testing purposes. There is no GUI, so you probably have to play around with source to configure it.

    make
    ./airsynth
