# Sound effect credits and licenses

These 21 edited recordings are repository inputs. Keep this file with the sounds
when distributing them. The weapon shots require attribution under CC-BY 3.0;
the other selected recordings are CC0. All source licenses were checked on
2026-10-15. Source URLs, download URLs, original SHA-256 hashes, crop parameters,
and final WAV hashes are recorded in [sources.json](sources.json).

## Required attribution: weapon shots

**Chaingun, pistol, rifle, shotgun shots — Michel Baradari (apollo-music.de),
submitted by qubodup.**

- Source: <https://opengameart.org/content/chaingun-pistol-rifle-shotgun-shots>
- License: [Creative Commons Attribution 3.0 Unported](https://creativecommons.org/licenses/by/3.0/).
- Files used: `shots/pistol.wav`, `shots/cg1.wav`, `shots/rifle.wav`,
  `shots/shotgun.wav` from `shots.7z`.
- Outputs: `pistol.wav`, `smg.wav`, `sniper.wav`, `shotgun.wav`.
- Changes: shortened one-shot excerpts, mono downmix, resampling to 48 kHz,
  peak normalization, and short boundary fades. The SMG uses the pack's chaingun
  shot as a prototype automatic-weapon sound; it is not a recording of a specific
  SMG model. Rifle/shotgun excerpts shorten the embedded mechanical tails.

The original archive's `shots/info.txt` also identifies Michel Baradari and
CC-BY 3.0. Attribution does not imply endorsement of this project.

## CC0 recordings

All entries below use [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/).
Credits are retained for provenance even where attribution is not required.

| Outputs | Work and creator | Source |
| --- | --- | --- |
| `huh.wav` | **huh.wav — TripleDStudios8** | [Freesound 194467](https://freesound.org/people/TripleDStudios8/sounds/194467/) |
| `intrigued.wav` | **intrigued — andrutzab** | [Freesound 337615](https://freesound.org/people/andrutzab/sounds/337615/) |
| `zombie.wav`, `zombie2.wav`, `zombie3.wav` | **Zombie moans — Darsycho** | [OpenGameArt](https://opengameart.org/content/zombie-moans) |
| `chase.wav`, `chase2.wav` | **80 CC0 creature SFX — rubberduck**, members `grunt_01.ogg` and `grunt_03.ogg` | [OpenGameArt](https://opengameart.org/content/80-cc0-creature-sfx) |
| `footstep.wav`, `footstep2.wav`, `footstep3.wav`, `footstep4.wav` | **Fantozzi's Footsteps (Grass/Sand & Stone) — Fantozzi**, curated by qubodup; Stone L1/R1/L2/R2 | [OpenGameArt](https://opengameart.org/content/fantozzis-footsteps-grasssand-stone) |
| `door_latch.wav`, `door_slam.wav` | **Door Open, Door Close Set — qubodup**, `DoorOpen01` and `DoorClose01` | [OpenGameArt](https://opengameart.org/content/door-open-door-close-set) |
| `reload.wav`, `reload_rifle.wav`, `reload_shotgun.wav` | **Gun reload sounds — SpringySpringo** | [OpenGameArt](https://opengameart.org/content/gun-reload-sounds) |
| `empty_click.wav` | **Gun reload, lock or click sound — pauliuw** | [OpenGameArt](https://opengameart.org/content/gun-reload-lock-or-click-sound) |

The two Freesound inputs are the public **high-quality MP3 previews**, not the
login-only original WAV downloads. Both pages explicitly label the sounds
Creative Commons 0. The local ignored source cache retains the license-page HTML
snapshots; their hashes are included in the manifest. `intrigued` contains many
takes across about 19 seconds; only the first isolated reaction is used.

All selected clips were decoded, downmixed to mono, resampled to 48 kHz PCM16,
cropped as listed in the manifest, peak-normalized, and given a 5 ms fade-in and
15 ms fade-out. Playback applies separate category gains and small pitch
variations; the repository files retain the processing above.

Only moans and explicitly labeled **grunt** members are used for zombie vocals.
No hurt, pain, injury, death, or damage reactions are selected or triggered.

## Other downloads considered

**Zombies Sound Pack — artisticdude**, [OpenGameArt](https://opengameart.org/content/zombies-sound-pack),
CC0. `zombies.zip` was downloaded for consideration and is documented in the
manifest, but none of its clips are shipped or referenced. Its numbered files do
not distinguish ordinary vocalizations from hurt/death clips, so the selected
moan/grunt sources above provide clearer separation. Archives may contain unused
material; preprocessing extracts only the members explicitly listed for use.

The male death/pain and female hurt packs suggested for consideration are not
downloaded or used.

## Rebuilding the edited WAVs

The ignored download cache is `data/tmp/audio-sources/`. Normal viewer builds
copy the checked-in WAVs and need no sound downloads, ffmpeg, or archive tools.
To reproduce the edited assets, install ffmpeg and use the existing CMake:

```bash
# Offline: requires the original files already in the source cache.
python3 tools/prepare_audio_assets.py
# Explicitly download missing, hash-checked inputs, then prepare them.
python3 tools/prepare_audio_assets.py --download
```

Preprocessing checks source hashes before extracting or decoding. It prepares and
validates all clips before replacing individual output files atomically. The
manifest's output hashes identify the shipped files; ffmpeg decoder/resampler
versions may produce slightly different samples when rebuilding from scratch.
Review edits and refresh the recorded output metadata when intentionally changing
the preparation recipe.

The synthesized landing and explosion sounds, wind, and rain are created by
`tools/create_audio_assets.py` in the build directory; they are original project
placeholders and contain no downloaded samples.
