"""Author the original octagonal HE grenade mesh, in meters, without external tools."""
import argparse
import json
import math
from pathlib import Path


def grenade_model():
    parts = []
    # Flat caps and three grey bands; red rings sit near both ends of the side.
    levels = [-.075, -.065, -.05, .05, .065, .075]
    for band, (low, high) in enumerate(zip(levels, levels[1:])):
        positions = [[round(.045 * math.cos(side * math.pi / 4), 8), y,
                      round(.045 * math.sin(side * math.pi / 4), 8)]
                     for y in (low, high) for side in range(8)]
        triangles = []
        for side in range(8):
            following = (side + 1) % 8
            triangles += [[side, side + 8, following + 8], [side, following + 8, following]]
        if band == 0:
            triangles += [[0, side, side + 1] for side in range(1, 7)]
        if band == 4:
            triangles += [[8, side + 9, side + 8] for side in range(1, 7)]
        parts.append(dict(name=f"HE_Grenade_Band_{band}", positions=positions, triangles=triangles,
                          color=[.75, .025, .02] if band in (1, 3) else [.72, .74, .76]))
    return dict(id="he_grenade", copyright="Original project asset, CC0 1.0", parts=parts)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(grenade_model(), indent=2) + "\n")
