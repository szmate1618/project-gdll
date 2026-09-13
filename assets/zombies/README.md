# Polyart Zombies

Ten characters by **Denys Almaral** from the
[Polyart Zombies with Animations Free Pack](https://sketchfab.com/3d-models/polyart-zombies-with-animations-free-pack-d9bcfdd88f5348549bc947226af7c314).
The creator links the [official Three.js demo](https://denysalmaral.com/gamedev/free-zombies/)
in that listing. Its public model files are the source used here. The complete
authoring pack is also available from the creator's
[Fab listing](https://www.fab.com/listings/2a85aa17-6eae-4c9d-b805-aeb1d722962f).

The Sketchfab listing's current public metadata identifies the asset license as
[Free Standard](https://sketchfab.com/licenses). The original creator retains
copyright. Keep the downloaded source assets local; this repository records the
download locations and SHA-256 hashes in `sources.json` rather than redistributing
the model binaries. The listing also carries the creator's NoAI restriction.

From the repository root, download and prepare the runtime files:

```bash
python3 tools/download_zombies.py
```

Rebuild the same GLBs from the cached originals without network access:

```bash
python3 tools/download_zombies.py --offline
```

`source/` preserves the original glTF files, buffers, and shared palette PNGs.
The importer downloads the ten `ZombieFemale_A` through `ZombieFemale_E` and
`ZombieMale_A` through `ZombieMale_E` rigs and their two matching `idle_220f`
clips. Other animations remain available in the creator's complete pack.
`models/` contains one self-contained GLB per character with its matching idle
clip and embedded palette. Both directories are ignored by Git.

The source models use centimeters and Y-up. Preparation adds a parent scale of
0.01 to the whole original scene, preserving mesh transforms, bone hierarchy,
inverse bind matrices, and animation values. The resulting GLBs use meters.
Animation channels are matched by exact node names and require identical
skeleton parent relationships; animation data, buffer views, and accessors are
remapped into each character without replacing its mesh or bind matrices.

Downloads and output GLBs are written to temporary files before publication.
Subsequent runs verify cached or downloaded originals against `sources.json`.
A source hash mismatch stops preparation so upstream changes can be reviewed.
