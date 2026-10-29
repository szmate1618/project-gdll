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

The build copies 27 curated recordings from `assets/audio/` into `build/audio/`.
Their [credits and licenses](../assets/audio/CREDITS.md) and
[provenance manifest](../assets/audio/sources.json) record every download,
creator, source license, input/output hash, and edit. Most recordings are CC0;
the four weapon shots are CC-BY 3.0 and require the included Michel Baradari
attribution. Credits and the manifest are copied alongside the runtime sounds.
Normal builds do not download audio or require ffmpeg.

`tools/create_audio_assets.py` still generates the original landing effect and
30-second wind/rain placeholders. The executable records the absolute build audio
directory, so launching from another working directory still finds the files.
`tools/prepare_audio_assets.py` reproduces the edited recordings from the cached
sources, with explicit `--download` available for missing inputs; see the credits.

Use `--audio-dir DIR` to replace the sound directory. Supply these names:

| File | Playback |
| --- | --- |
| `footstep.wav`, `footstep2.wav`, `footstep3.wav`, `footstep4.wav` | Varied player and zombie steps from actual movement |
| `landing.wav` | Original synthesized player landing effect |
| `pistol.wav`, `smg.wav`, `sniper.wav`, `shotgun.wav` | Successful weapon shots, including automatic fire |
| `empty_click.wav` | Empty trigger, at most once per hold; release/press allows another click |
| `reload.wav`, `reload_rifle.wav`, `reload_shotgun.wav` | Successful pistol, SMG/sniper, or shotgun reload |
| `huh.wav`, `intrigued.wav` | Deterministically varied reaction when an idle living zombie accepts a hearing event |
| `chase.wav`, `chase2.wav` | Grunt on acquisition of a full-sight chase target |
| `zombie.wav`, `zombie2.wav`, `zombie3.wav` | Intermittent nearby zombie moans |
| `door_latch.wav` | Successful grab of a fully closed door |
| `door_slam.wav` | Fast closure of a previously opened door |
| `rustle.wav`, `rustle2.wav`, `rustle3.wav` | Movement through leafy shrubs |
| `dry_rustle.wav`, `dry_rustle2.wav`, `twig_snap.wav` | Dry shrub movement and occasional running cracks |
| `wind.wav` | Continuous background ambience |
| `rain.wav` | Additional continuous ambience with `--rain` |

There are no pain, injury, damage, or death audio cues. Reload playback begins
when a reload starts; gameplay timing follows weapon handling and storage access.

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
reserves the closest eight voice positions within **40 meters**, using exact 3D
distance and stable ID tie-breaking. Calls originate 1.5 m above the feet.
Selected positions, range, and corpse/removal eligibility update every frame.
This does not depend on frustum culling, render LoD, or an activated AI behavior
object. Distance attenuation uses an inverse distance model with a three-meter
reference distance. Walls do not currently occlude playback, and there is no
automatic room reverb.

Vocals are **one-shots**, not continuously looping voices. Initial ordinary calls
are staggered by 2–9 seconds; subsequent calls start 8–18 seconds apart.
A fixed hash of the stable zombie ID and event/call count selects recordings and
small pitch variations without changing gameplay RNG. Chase-start grunts can
interrupt hearing reactions; hearing reactions can interrupt ordinary moans.
Ordinary calls never interrupt a reaction. Reactions older than 0.3 simulation
seconds are discarded rather than replayed when a zombie enters audible range.
Accepted hearing and chase-acquisition counters are presentation notifications;
remaining in a behavior state cannot repeat them each frame. Attack/chase range
transitions do not count as acquiring a new target.

Zombie footsteps use the actual locomotion clock and foot displacement, with
roughly 0.8 m walking and 1 m running strides. They are quieter than the player's
steps, use the shared four recorded variations, and originate at the zombie feet.
Only the selected nearby zombies generate these cues. Blocked or stationary
zombies, teleports, and corpses do not produce catch-up footsteps.

Door audio observes nearby physics poses without changing physics. Grabbing a
door within 0.025 radians of closed plays a latch click. A slam requires opening
past 0.12 radians, crossing back into the 0.035-radian closed region at a closing
speed of at least 0.8 rad/s, and a 0.4-second debounce. Slowly closing doors,
initially closed doors, and closed-stop jitter stay silent. The last pre-impact
angular speed also counts because the hinge solver can clamp the final sample.

## Gameplay hearing stays independent

### Undergrowth contact

Only walking-mode players produce shrub contact events. The immutable
`UndergrowthIndex` builds once from explicitly marked shrubs; trees and bare
dry-tree assets are excluded. Gameplay contact uses an ellipse occupying 85%
of the local card width/depth and the shrub's vertical extent, transformed into
world space. The player radius and stance height expand contact conservatively.
This approximate foliage volume ignores billboard padding, rendering LoD,
visibility cutoffs and cosmetic blast deformation. It adds no rigid bodies,
mesh triangles or GPU queries.

Each player's emitter batches actual swept movement at 20 Hz. Straight segments
coalesce; turns retain their separate sweeps so a shortcut cannot cut a corner
through foliage. BVH queries visit only nearby volumes. Overlapping shrubs share
one cadence, with at least 0.65 m of foliage travel between continuing bursts
and at least 0.25 s between any bursts. Initial contact requires 5 cm of travel.
There are no catch-up bursts: jumps, teleports, discontinuous corrections and
stalls over 0.2 s clear pending contact. Brief stationary samples between physics
steps preserve paths for high-refresh rendering; 80 ms without movement clears
them. Standing still and free-fly movement stay silent.

The three leafy and two dry recordings last 0.34–0.42 s; a separate twig snap
lasts 0.16 s. Selection and small pitch changes use the event counter. Stopping
or leaving schedules no new sounds; the current one-shot finishes naturally.
No per-shrub loop or per-contact file decoding is created. Existing fixed effect
voice limits apply.

A stable 25% subset of generated shrubs is tagged dry independently of position,
size or forest visibility. Leafy walk/run noise is 45/52 dB at one meter; dry
walk/run rustle is 47/55 dB. Every third running dry contact, offset by player ID
in multiplayer, produces a 58 dB twig snap. Crouching reduces noise by 6 dB and
prone movement by 10 dB. These are game tuning values, separate from audio gain.

Single player routes each event independently to playback and zombie hearing.
Multiplayer servers calculate contact and AI noise from authoritative movement.
Graphical clients play their own predicted movement contacts; reconciliation
replays are silent and never submit noise to the server. Remote-player shrub
audio is not replicated yet. `--no-audio` disables client playback without
changing hearing. Multiplayer packages include the runtime WAVs, their credits,
and the dynamically linked OpenAL library; other multiplayer sounds remain
outside this initial audio integration.

Build `benchmark_undergrowth` explicitly to measure the CPU detector without
audio mixing, AI hearing, physics or rendering. With no arguments it uses
40,000 synthetic shrubs. Optional arguments are a map GLB, its tree-instance
manifest and a runtime forest configuration; those are loaded read-only.

```bash
cmake --build build --target benchmark_undergrowth -j4
./build/benchmark_undergrowth
```

On 2026-10-19, a Release build on Ryzen 5 7535HS measured the current map's
26,099-shrub index at 8.2 ms to build. Seven paired runs, each with 600 warmup
and 12,000 measured 60 Hz ticks, compared an empty index with contact enabled.
Moving paths sampled eight shrub locations; medians of run means added about
0.21 microseconds/tick for one player and 1.6 microseconds/tick for eight players.
Idle overhead was about 0.006/0.02 microseconds respectively. These are detector
costs, excluding AI hearing, OpenAL mixing, physics, scene loading and rendering;
they are not a whole-game FPS benchmark.

## Simulation and playback separation

`NoiseEvent` retains simulation position and game-tuned decibels. Footstep and
landing events also identify their action, so presentation can select a sound
without interpreting AI hearing levels. The existing AI handler receives the same
events and propagation values as before. The audio backend has no dependency on
hearing thresholds or AI state and never calls `ZombieLayer::makeNoise`.

Muting, missing files, playback voice limits, and device failures cannot suppress
AI noise. Conversely, playing a gunshot, reload, or zombie sound does not itself
create an AI hearing event. Standalone and multiplayer firearm handlers separately
emit AI noise for accepted shots, independently of playback.

## Multiplayer playback

The graphical game client shares the same clips, HRTF playback, 24-effect pool,
eight zombie voices and 40 m world-sound range. Wind is streamed locally.
`--no-audio` skips playback and audio candidate queries; AI hearing is unchanged.

Own firearm shots, reloads and empty clicks use presentation-only prediction.
The predictor tracks pending magazine consumption and weapon cooldowns, shares
the authoritative reload eligibility calculation, and retires predictions when
commands are acknowledged. It never changes gameplay ammo or hit results.
Confirmation and movement reconciliation never replay sounds. A rejected
prediction may already have been heard. Recoil and hit effects remain confirmed.

Own footsteps, landings, door latches and shrub contacts also play locally.
Other players' footsteps/landings and shrub contacts are reconstructed from
interpolated movement, with initial baselines and teleport/vehicle resets.
No movement-sound packets are sent.

Every 0.2 s, the client samples at most ten tracked zombie IDs to fill a small
audible candidate set. Candidates stay while alive and within range, so moving
the sample cursor cannot constantly restart moan timers. The existing local
scheduler chooses ordinary moans and zombie footsteps. This is approximate
selection rather than an exact nearest-population query; a sparse random sample
can leave audible actors silent temporarily. Moans never enter the protocol.

Server sounds cover remote firearm shots/reloads, door latches, and actual
zombie hearing/chase reactions. Gameplay records bounded reaction notifications
where they occur; there is no extra population scan. A history of at most 128
events repeats for 30 server ticks (0.5 s) in snapshots, selected within 40 m of
each listener. An originating player never receives its own predicted actions.
Clients deduplicate event IDs, suppress pre-join history, and discard events more
than 18 ticks (0.3 s) old in the newest accepted snapshot. A receive backlog is
coalesced before playback. Loss beyond that short history drops a sound rather
than replaying a burst. Explosion sounds reuse grenade IDs/ages; door slams use
the existing angle/speed tracker. These are presentation cues, independent of
AI noise and perception.

Protocol 22 requires matching rebuilt peers. Audio is positional with distance
attenuation; wall occlusion, room acoustics and voice chat are not implemented.

## Checks and code

```bash
ctest --test-dir build -R '^viewer_(audio.*|scene_audio|weapons|player_noise|zombie_behavior)$' --output-on-failure
ctest --test-dir build -R '^multiplayer_(sounds|client_audio|game_sessions|client_weapons)$' --output-on-failure
.venv/bin/python -m unittest discover -s tests -p 'test_audio_assets.py' -v
```

The backend tests use OpenAL Soft's **null output**, so no audio device or display
is required. They check camera coordinates/orientation, fixed voice limits,
nearby priority, HRTF initialization, preload reuse, bounded streaming and restart
after underrun. A separate process tests unavailable-device fallback. Scene tests
check corpse removal, neighborhood changes, movement playback without zombies,
and independent AI hearing with audio enabled and muted. Decoder tests use small
synthetic WAVs, including malformed and truncated files. Pure cue tests check
reaction freshness/variation, movement footsteps, and slam hysteresis/debounce.
The offline asset check verifies complete credits, hashes, and decoded PCM.
These checks do not replace a headphone listening check for perceived direction
or asset quality.

- `src/audio_wave.hpp/.cpp`: PCM16 parsing and incremental reading.
- `src/audio.hpp/.cpp`: OpenAL device/context lifetime, preloaded buffers, sources,
  listener, voice priority, and streaming queues.
- `src/scene_audio.hpp/.cpp`: Camera/gameplay routing and indexed zombie selection.
- `src/zombie_audio.hpp/.cpp`: Deterministic reaction, moan, and footstep scheduling.
- `src/door_audio.hpp/.cpp`: Presentation-only slam detection.
- `src/undergrowth.hpp/.cpp`: Indexed contact volumes, swept movement and AI noise events.
- `src/undergrowth_audio.hpp/.cpp`: Recorded shrub effect selection and playback.
- `multiplayer/client/audio_playback.hpp/.cpp`: Local playback and bounded ambient candidates.
- `multiplayer/client/weapon_audio.hpp/.cpp`: Immediate action sound prediction.
- `multiplayer/client/sound_cues.hpp/.cpp`: Repeated snapshot event filtering/deduplication.
- `multiplayer/server/sounds.hpp/.cpp`: Bounded, range-filtered authoritative event history.
- `tools/create_audio_assets.py`: Original synthesized landing and ambience.
- `tools/prepare_audio_assets.py`: Hash-checked recording preparation.
- `assets/audio/`: Curated WAVs, credits, and provenance manifest.

OpenAL Soft is LGPL-2.0-or-later and is linked as a shared library by default.
Its embedded SADIE II HRTF data is Apache-2.0. Preserve the upstream `COPYING` and
`LICENSE.Apache-2.0.txt` notices when redistributing the library/data.
