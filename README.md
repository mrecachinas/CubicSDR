CubicSDR [![CircleCI](https://dl.circleci.com/status-badge/img/gh/cjcliffe/CubicSDR/tree/master.svg?style=svg)](https://dl.circleci.com/status-badge/redirect/gh/cjcliffe/CubicSDR/tree/master)
========

Cross-Platform Software-Defined Radio Application

- The latest releases are available on the [CubicSDR Releases](https://github.com/cjcliffe/CubicSDR/releases) page.
- Build instructions can be found at the [CubicSDR Wiki](https://github.com/cjcliffe/CubicSDR/wiki) page.
- Manual is available at [cubicsdr.readthedocs.io](http://cubicsdr.readthedocs.io).
- Manual contributions can be submitted to the [CubicSDR-Manual](https://github.com/cjcliffe/CubicSDR-Manual) repository.

Utilizes: 
--------
  - liquid-dsp (http://liquidsdr.org/ -- https://github.com/jgaeddert/liquid-dsp)
  - SoapySDR (http://www.pothosware.com/ -- https://github.com/pothosware/SoapySDR)
  - RtAudio (http://www.music.mcgill.ca/~gary/rtaudio/ -- http://github.com/thestk/rtaudio/)
  - LodePNG (http://lodev.org/lodepng/)
  - BMFont (http://www.angelcode.com/ -- http://www.angelcode.com/products/bmfont/)
  - Bitstream Vera font (http://en.wikipedia.org/wiki/Bitstream_Vera)
  - OpenGL (https://www.opengl.org/)
  - wxWidgets (https://www.wxwidgets.org/)
  - CMake (http://www.cmake.org/)

Optional Libs:
--------
  - FFTW3 (can be compiled into liquid-dsp if desired) (http://www.fftw.org/ -- https://github.com/FFTW/fftw3)
  - hamlib (https://github.com/Hamlib/Hamlib)
  - uWebSockets + uSockets for WebSocket streaming (https://github.com/uNetworking/uWebSockets)

WebSocket Streaming:
--------------------

CubicSDR can optionally stream real-time data to web clients via WebSocket. Enable with:

```
cmake .. -DUSE_WEBSOCKET=ON
```

**Requirements:** zlib (typically already available on most systems).

**Streams available:**
  - `spectrum` — FFT magnitude data (float32 array)
  - `waterfall` — Waterfall line data (float32 array)
  - `iq` — Raw IQ samples (interleaved complex float32)
  - `audio` — Demodulated audio samples (placeholder, future enhancement)

**Default port:** 9002 (configurable via AppConfig).

**Web test page:** Open `web/index.html` in a browser to connect and visualize data using [SigPlot](https://github.com/LGSInnovations/sigplot).

**Wire protocol:**
  - Control messages (subscribe/unsubscribe) use JSON text frames
  - Sample data uses binary frames with a 24-byte header (magic `0x43534452`, stream type, data format, center freq, sample rate, sample count) followed by the payload

Recommended minimum requirements:
--------------------
  - Multi-core processor system with at least 1GB RAM.
  - Graphics card with at least 128MB video memory and OpenGL 3.x or ES 2.0 support.
  - OSX 10.9+ for Mac binary releases.
  - Windows 7+ for 64 or 32-bit Windows binary releases.
  - Linux and other embedded distribution support yet to be indexed, known to at least work on Debian 8+ and Ubuntu 14+.

Platform build scripts and test builds:
--------------------------------------
  - MacOS (https://github.com/cjcliffe/CubicSDR-macOSBuild)
  - Windows (https://github.com/cjcliffe/CubicSDR-WinBuild)
  - Linux (AppImage) (https://github.com/cjcliffe/CubicSDR-AppImageKit)


License:
-------
  - GPL-2.0+
