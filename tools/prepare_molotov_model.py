"""Author a low-poly green bottle and cloth wick in runtime metres, offline."""
import argparse
import json
import math
from pathlib import Path


def molotov_model():
    parts = []
    # An opaque stylized bottle keeps weapon rendering's existing material path.
    levels = [(-.14, .032), (-.125, .045), (.045, .045), (.08, .021), (.125, .018)]
    for band, ((low, r0), (high, r1)) in enumerate(zip(levels, levels[1:])):
        points = [[round(r * math.cos(i * math.pi / 6), 8), y,
                   round(r * math.sin(i * math.pi / 6), 8)]
                  for y, r in ((low, r0), (high, r1)) for i in range(12)]
        faces = []
        for i in range(12):
            j = (i + 1) % 12
            faces += [[i, i + 12, j + 12], [i, j + 12, j]]
        if band == 0:
            faces += [[0, i, i + 1] for i in range(1, 11)]
        if band == 3:
            faces += [[12, i + 13, i + 12] for i in range(1, 11)]
        parts.append(dict(name=f"Molotov_Bottle_{band}", positions=points,
                          triangles=faces, color=[.12, .29, .08]))
    # A folded cloth protrudes from the neck, clearly distinguishing the bottle.
    points = [[-.014, .116, -.014], [.014, .116, -.014], [.014, .116, .014], [-.014, .116, .014],
              [-.008, .166, -.009], [.019, .166, -.009], [.019, .166, .009], [-.008, .166, .009]]
    faces = [[0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7]]
    for i in range(4):
        j = (i + 1) % 4
        faces += [[i, j, j + 4], [i, j + 4, i + 4]]
    parts.append(dict(name="Molotov_Cloth_Wick", positions=points, triangles=[list(reversed(f)) for f in faces],
                      color=[.68, .58, .38]))
    return dict(id="molotov", copyright="Original project asset, CC0 1.0", parts=parts)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(molotov_model(), indent=2) + "\n")
