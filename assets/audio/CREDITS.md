# Sound effect credits and licenses

These 37 edited recordings are repository inputs. Keep this file with the sounds
when distributing them. The weapon shots require attribution under CC-BY 3.0;
the other selected recordings are CC0. Original source licenses were checked on
2026-10-15; the shrub and twig recordings were checked on 2026-10-19, and impact
recordings on 2026-11-05.
Source URLs, download URLs, original SHA-256 hashes, crop parameters,
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
| `rustle.wav`, `rustle2.wav`, `rustle3.wav`, `dry_rustle.wav`, `dry_rustle2.wav` | **Rustling leaves — Kinoton**, reed-mat foliage foley | [Freesound 347557](https://freesound.org/people/Kinoton/sounds/347557/) |
| `twig_snap.wav` | **Twig Snap — Allan Legemaate (alegemaate)** | [Freesound 364699](https://freesound.org/people/alegemaate/sounds/364699/) |

The Freesound inputs are the public **high-quality MP3 previews**, not the
login-only original WAV downloads. Their pages explicitly label the sounds
Creative Commons 0. The local ignored source cache retains the license-page HTML
snapshots; their hashes are included in the manifest. `intrigued` contains many
takes across about 19 seconds; only the first isolated reaction is used.
The foliage foley supplies five short 0.34–0.42-second bursts, with crisper
excerpts used for dry undergrowth; the twig snap is a separate 0.16-second clip.

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

## Material impact recordings

The following sources were checked on 2026-11-05 and are all
[CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/).
The ten short mono PCM16 outputs are cropped from nine recordings. The Freesound
inputs are public high-quality MP3 previews; the wooden-chair source is the
original WAV. Crops, download hashes, source-page evidence and output hashes are
recorded in `sources.json`, using the same offline preparation tool as above.

| Outputs | Creator and recording | Source |
| --- | --- | --- |
| `impact_metal_soft.wav` | imnotdubMass — metal hanger bar dropped onto carpet | [Freesound 679994](https://freesound.org/people/imnotdubMass/sounds/679994/) |
| `impact_metal_hard.wav` | rifualk — iron ring hitting concrete | [Freesound 613466](https://freesound.org/people/rifualk/sounds/613466/) |
| `impact_wood_soft.wav` | Joseph SARDIN — wooden chair falling onto carpet | [BigSoundBank 0516](https://bigsoundbank.com/wooden-chair-falling-on-carpet-s0516.html) |
| `impact_wood_hard.wav` | mitmy — wooden stick dropped onto concrete | [Freesound 186909](https://freesound.org/people/mitmy/sounds/186909/) |
| `impact_glass_soft.wav`, `impact_glass_hard.wav` | ventousawins — glass bottle placed on cloth-covered and bare wood | [Freesound 681410](https://freesound.org/people/ventousawins/sounds/681410/) |
| `impact_rock_soft.wav` | dichardson — stone dropped onto grass | [Freesound 570976](https://freesound.org/people/dichardson/sounds/570976/) |
| `impact_rock_hard.wav` | Fenodyrie — brick hitting concrete | [Freesound 565914](https://freesound.org/people/Fenodyrie/sounds/565914/) |
| `impact_soft_soft.wav` | cmorris035 — hardcover book tossed onto a bed | [Freesound 319153](https://freesound.org/people/cmorris035/sounds/319153/) |
| `impact_soft_hard.wav` | VSokorelos — cardboard box dropped onto concrete | [Freesound 346169](https://freesound.org/people/VSokorelos/sounds/346169/) |

These are representative material/surface sounds, not matched recordings of
one object at calibrated impact energies. In particular the soft-material pair
uses a book and a cardboard box. The glass recording describes both surfaces;
the early muted and later bare-table takes supply the two crops. Their exact
surface assignment and the overall timbre still merit a listening review.

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
