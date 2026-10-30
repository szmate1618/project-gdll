# Menu Pixel

`menu-pixel.ttf` preserves the original main menu's hand-authored 5×7 glyphs.
`menu-pixel.json` contains the column bitmasks moved from `menu.cpp`: bit zero
is the top row. Lowercase characters use the original uppercase shapes, and
unsupported printable ASCII uses the original marker. No third-party font is
used for these glyphs.

Rebuild the checked-in font from the repository root:

```sh
python3 tools/prepare_menu_font.py
```

The builder uses only Python's standard library and produces deterministic
TrueType outlines, with one square per lit pixel. Ten original pixels make one
em; glyphs advance six pixels. A font size of 20 gives the original 2× lettering
at the menu's 960×600 design size. Other window sizes scale the layout together.
The Modern heading uses this font; its controls use the existing `hud-system`
font and `GD_HUD_FONT` override.
