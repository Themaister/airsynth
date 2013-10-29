## AirSynth

AirSynth is a simple softsynth for ALSA. It reads MIDI data in real-time, and synthesizes a breathy pad sound with a fairly low latency.
The sound itself is generated using noise + IIR filtering.

### Running
Connect a MIDI keyboard. Then `airsynth -i <MIDI device, e.g. hw:1,0,0>`. See `airsynth --help` for more.

### Compilation
Have libsndfile and alsa-libs installed, then type `make`. A reasonably up-to-date GCC or Clang++ is required.

