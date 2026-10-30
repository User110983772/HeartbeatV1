# Signalsmith Stretch (vendored)

Pitch shifting with the length kept, for the sample engine's STRETCH and SYNC modes.

- `signalsmith-stretch.h`: https://github.com/Signalsmith-Audio/signalsmith-stretch, tag 1.4.0
  (a670068d9aeb64913331d5cc29337b19a457a7df), MIT licence (LICENSE-stretch.txt).
- `signalsmith-linear/fft.h`, `stft.h`: https://github.com/Signalsmith-Audio/linear, tag 0.6.4
  (de55e6a50ffcf6f8f43f649692d94691c7025151), MIT licence (LICENSE-linear.txt). Only the two headers the
  stretcher includes; the optional platform FFT back-ends are left out (they're opt-in by macro).

Unmodified copies. Include with `third_party/signalsmith` on the include path.
