# Railway texture

`track.png` is a deterministic, locally generated RGB texture. Its 1600 × 240
pixels cover 4 m across a track and 0.60 m along it, at 400 pixels per meter.
The tile repeats along the track and clamps across its gravel shoulders.

- Rail gauge: 1.435 m between the inner edges of the rail heads.
- Rail heads: 0.07 m wide, with a continuous steel strip.
- Timber ties: 2.60 m long and 0.24 m wide, at 0.60 m center spacing.
- Gravel fills the remaining surface between ties and beside the rails.

Recreate the source PNG from the repository root:

```bash
.venv/bin/python tools/create_railway_texture.py
```

The builder uses Pillow and NumPy from the generator environment, with a fixed
random seed. The artwork needs no downloads or external attribution. Rails and
ties are flat texture detail on the existing terrain-following track beds;
there is no additional geometry or draw call per tie.

The generator embeds this image and uses UVs measured in local east/north
meters. Bed width does not change the gauge. Included narrow-gauge ways also
use the standard-gauge tile for now. To update an existing map from its cached
OSM extract, run `.venv/bin/python tools/apply_railway_texture.py`; see the
[map workflow](../../README.md) for validation and backups.
