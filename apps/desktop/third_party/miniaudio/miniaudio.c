/* The miniaudio implementation (third_party/README.md): decoding (MP3 through dr_mp3), the mixer and the
 * playback backends (Core Audio on macOS; PulseAudio, ALSA or JACK opened at run time on Linux). Nothing
 * is encoded, and generation/WAV/FLAC writers are left out. */
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
