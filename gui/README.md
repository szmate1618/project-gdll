# HUD icons

The gameplay HUD uses downloaded [Game-icons.net](https://game-icons.net)
white-on-transparent PNGs. The icon pack, its license and the prepared textures
live in `data/assets/game-icons/`, alongside other downloaded assets and already
ignored by Git. No custom font is needed.

Prepare them from the repository root:

```bash
.venv/bin/python tools/prepare_hud_icons.py
```

This requires the existing Pillow dependency. The script reuses the downloaded
archive, verifies its SHA-256, and downsizes the selected artwork to 128×128.
It downloads the pack only when absent. `hud-icons.json` records the source,
author and original page for each selection. It does not contain artwork.

The PNGs are ordinary straight-alpha images on disk. The HUD loader premultiplies
them once before upload to match RmlUi's blending; RmlUi caches and reuses them.
The C++ backend uses the existing stb image decoder. There is no SVG runtime.
Missing icons do not stop the game; prepare them before running or packaging it.

These icons replace gameplay HUD and weapon-selector placeholders. Inventory,
skill-menu placeholders and the planned rendered item-details preview are separate.
Standing, kneeling and crawling symbols represent the three player stances;
the minimap uses a position dot, without implying a facing direction.

Icons made by sbed, Lorc, Delapouite, John Colburn and Skoll. Available on
[Game-icons.net](https://game-icons.net), licensed under
[CC BY 3.0](https://creativecommons.org/licenses/by/3.0/).
See [the shipped attribution](../credits/assets/HUD_ICONS.txt).
