# Viewer audio

The viewer uses [OpenAL Soft 1.24.3](https://github.com/kcat/openal-soft/releases/tag/1.24.3)
for positional playback. CMake downloads a pinned, SHA-256-checked archive and
builds it in the build tree. No system OpenAL development package is required.
On Linux it detects available audio backends, including PulseAudio and ALSA.
Their runtime libraries and a working audio session are needed for audible output.
If the output device cannot be opened, the viewer reports that audio is disabled
and continues running.

```bash
cmake -S . -B build
cmake --build build -j4
./build/godollo_viewer output/godollo.glb --fps
./build/godollo_viewer output/godollo.glb --fps --rain
./build/godollo_viewer output/godollo.glb --audio-dir /path/to/audio
./build/godollo_viewer output/godollo.glb --no-audio
./build/godollo_viewer output/godollo.glb --no-hrtf
```

`--no-audio` creates no playback device. `--no-hrtf` requests ordinary positional
audio. By default the viewer requests HRTF processing, using OpenAL Soft's
embedded dataset; startup prints the actual HRTF state. This uses ordinary stereo
headphones. There is no in-game profile calibration UI yet. OpenAL Soft also
supports [custom HRTF datasets](https://github.com/kcat/openal-soft/blob/1.24.3/docs/hrtf.txt)
through its own configuration.

## Sounds and replacement files

The build runs `tools/create_audio_assets.py`, using only Python's standard
library, to create original synthesized **placeholder** effects and 30-second
ambience files in `build/audio/`. These are functional cues for testing, rather
than finished sound design. The generator does not download recordings or change
the generated town. The executable records the absolute build audio directory,
so launching from another working directory still finds the files.

Use `--audio-dir DIR` to replace the entire sound directory. Supply these names:

| File | Playback |
| --- | --- |
| `footstep.wav` | Player walking/running steps from actual fixed-step movement |
| `landing.wav` | Player airborne-to-grounded transition |
| `pistol.wav`, `smg.wav`, `sniper.wav`, `shotgun.wav` | Successful weapon shots, including automatic fire |
| `reload.wav` | Reloads that transfer ammunition |
| `zombie.wav` | Nearby living zombies, with staggered loop offsets and slightly varied pitch |
| `wind.wav` | Continuous background ambience |
| `rain.wav` | Additional continuous ambience with `--rain` |

All files must be RIFF/WAVE **signed 16-bit PCM**, with sample rates from 8 to
192 kHz. Effects must be **mono** and no longer than ten seconds. Ambience can
be mono or stereo and should have a suitable loop boundary. Compressed WAV,
floating-point WAV, MP3, Ogg, and RF64 are not decoded. Missing or unsupported
files produce a diagnostic and disable only that sound.

Short effects are decoded and uploaded once at startup. Repeated footsteps,
shots, and zombie calls share their preloaded buffers; playback does not reopen
those files. Wind/rain remain open and are decoded in chunks: each stream has
four 4096-frame buffers, about 64 KiB of queued PCM for stereo. Memory does not
grow with the recording's duration. Finished buffers are refilled during app
updates, including while minimized; a completely drained queue restarts after
a stall. Reads run on the app thread, so use local files. Disk stalls can cause
an audible gap. OpenAL mixes asynchronously.

## Listener and voice selection

The listener follows the camera's **eye position**, forward vector, and camera
up vector after movement in both free-fly and walking modes, including look
pitch and stance changes. World positions remain X east, Y up, Z south in meters;
there is no additional axis flip. Camera mode changes and teleports do not cause
Doppler pitch spikes. The master gain defaults to 0.6.

There are separate fixed pools of **24 short-effect sources** and **eight zombie
sources**, plus the wind/rain streaming sources. World effects can replace a
farther world effect but cannot evict the player's attached gunfire/reload sounds.
Sustained local effects reuse the pool when full.

Zombie selection queries the existing living-body spatial index at 5 Hz, then
chooses the closest eight voice positions within **40 meters**, using exact 3D
distance and stable ID tie-breaking. Calls originate 1.5 m above the feet.
Selected positions, range, and corpse/removal eligibility update every frame;
existing selected voices retain their loop phase. The loops should include quiet
intervals between vocalizations. This does not depend on frustum culling, render
LoD, or an activated AI behavior object. Distance attenuation uses an inverse
distance model with a three-meter reference distance. Walls do not currently
occlude playback, and there is no automatic room reverb.

## Gameplay hearing stays independent

`NoiseEvent` retains simulation position and game-tuned decibels. Footstep and
landing events also identify their action, so presentation can select a sound
without interpreting AI hearing levels. The existing AI handler receives the same
events and propagation values as before. The audio backend has no dependency on
hearing thresholds or AI state and never calls `ZombieLayer::makeNoise`.

Muting, missing files, playback voice limits, and device failures cannot suppress
AI noise. Conversely, playing a gunshot, reload, or zombie sound does not itself
create an AI hearing event. Gunfire's existing simulation behavior is unchanged.

## Checks and code

```bash
ctest --test-dir build -R '^viewer_(audio.*|scene_audio|player_noise|zombie_behavior)$' --output-on-failure
```

The backend tests use OpenAL Soft's **null output**, so no audio device or display
is required. They check camera coordinates/orientation, fixed voice limits,
nearby priority, HRTF initialization, preload reuse, bounded streaming and restart
after underrun. A separate process tests unavailable-device fallback. Scene tests
check corpse removal, neighborhood changes, movement playback without zombies,
and independent AI hearing with audio enabled and muted. Decoder tests use small
synthetic WAVs, including malformed and truncated files. These checks do not
replace a headphone listening check for perceived direction or asset quality.

- `src/audio_wave.hpp/.cpp`: PCM16 parsing and incremental reading.
- `src/audio.hpp/.cpp`: OpenAL device/context lifetime, preloaded buffers, sources,
  listener, voice priority, and streaming queues.
- `src/scene_audio.hpp/.cpp`: Camera/gameplay routing and indexed zombie selection.
- `tools/create_audio_assets.py`: Reproducible synthesized placeholder sounds.

OpenAL Soft is LGPL-2.0-or-later and is linked as a shared library by default.
Its embedded SADIE II HRTF data is Apache-2.0. Preserve the upstream `COPYING` and
`LICENSE.Apache-2.0.txt` notices when redistributing the library/data.
