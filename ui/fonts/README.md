# GUI fonts

## IBM Plex

The HUD and Modern menu bundle three unmodified static TrueType fonts from
[IBM Plex](https://github.com/IBM/plex):

| File in `plex/` | Renderer family | Role | Bytes |
| --- | --- | --- | ---: |
| `IBMPlexSans-Regular.ttf` | `hud-system` | Content and standard controls | 200,500 |
| `IBMPlexSansCondensed-Medium.ttf` | `plex-heading` | Titles, tabs and panel headings | 202,500 |
| `IBMPlexMono-Regular.ttf` | `plex-mono` | Numeric readouts, clocks and coordinates | 173,052 |

Total font data: 576,052 bytes (562.6 KiB). The Medium face is registered as
the normal weight of its own renderer family; these aliases do not alter the
upstream files. `GD_HUD_FONT=/path/to/font.ttf` overrides only `hud-system`;
headings and numeric readouts continue to use the bundled Plex faces.

Copyright © 2017 IBM Corp. with Reserved Font Name "Plex".
Licensed under the SIL Open Font License, Version 1.1. See the
[font attribution](../../credits/assets/FONTS.txt) and
[complete original copyright notice and license](../../credits/assets/IBM_PLEX_OFL.txt).
Linux and Windows packages include the font files and credits, plus a copy
of the license at `licenses/IBM-Plex.txt`. No system font installation or
runtime font download is needed. Keep the notices with redistributed fonts.

Downloaded from these upstream paths under `https://github.com/IBM/plex/blob/master/`:

- `packages/plex-sans/fonts/complete/ttf/IBMPlexSans-Regular.ttf`
- `packages/plex-sans-condensed/fonts/complete/ttf/IBMPlexSansCondensed-Medium.ttf`
- `packages/plex-mono/fonts/complete/ttf/IBMPlexMono-Regular.ttf`

SHA-256 hashes of the bundled files:

```text
975dcda37d80f038dcd143c22e33ca2d97a0cc5a929aace1c749153b0fe1afa5  IBMPlexSans-Regular.ttf
0c6b79ef4a7de76ffc6bf2091980e5f81a1a87ac3967566bc98aae0baa518f54  IBMPlexSansCondensed-Medium.ttf
7c6fbddca4b700be918f5f6183d9bd4464fa427fe435f0b480d77fe2bb8c5a43  IBMPlexMono-Regular.ttf
```

## Menu Pixel

`menu-pixel.ttf` preserves the original main menu's hand-authored 5×7 glyphs.
`menu-pixel.json` contains the column bitmasks moved from `menu.cpp`: bit zero
is the top row. Lowercase characters use the original uppercase shapes, and
unsupported printable ASCII uses the original marker. No third-party font is
used for these glyphs.
The hash sign is an added 5×7 glyph for the character editor's hex color fields.

Rebuild the checked-in font from the repository root:

```sh
python3 tools/prepare_menu_font.py
```

The builder uses only Python's standard library and produces deterministic
TrueType outlines, with one square per lit pixel. Ten original pixels make one
em; glyphs advance six pixels. A font size of 20 gives the original 2× lettering
at the menu's 960×600 design size. Other window sizes scale the layout together.
Classic headings and controls use this font. Modern uses the Plex roles above.
