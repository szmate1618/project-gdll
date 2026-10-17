"""Colour-based tree candidates outside a separate forest exclusion mask.

This is a native-pixel canopy heuristic, not a resolved individual-crown model.
Spacing is measured in local east/north metres, never in image pixels.
"""
from __future__ import annotations

from dataclasses import dataclass
import math

import numpy as np


@dataclass(frozen=True)
class DetectionSettings:
    color_quantile: float = .90
    min_spacing_m: float = 12.
    training_inset_m: float = 10.

    def validate(self):
        if not .5 <= self.color_quantile <= .999:
            raise ValueError("color_quantile must be between 0.5 and 0.999")
        if not math.isfinite(self.min_spacing_m) or not 1 <= self.min_spacing_m <= 100:
            raise ValueError("min_spacing_m must be between 1 and 100 metres")
        if not math.isfinite(self.training_inset_m) or not 0 <= self.training_inset_m <= 100:
            raise ValueError("training_inset_m must be between 0 and 100 metres")


def greenness(rgb):
    rgb = np.asarray(rgb, dtype=np.float64)
    return (2 * rgb[..., 1] - rgb[..., 0] - rgb[..., 2]) / np.maximum(rgb.sum(axis=-1), 1)


def learn_forest_color(rgb, training_mask, quantile=.90):
    """Learn the actual RGB mean and a regularized colour-distance threshold."""
    samples = np.asarray(rgb[training_mask], dtype=np.float64)
    if len(samples) < 25:
        raise ValueError("At least 25 valid interior forest pixels are required for colour training")
    mean = samples.mean(axis=0)
    # The covariance allows correlated illumination changes, rather than treating
    # a shaded crown as an unrelated colour. A five-level RGB noise floor keeps
    # a uniform training fixture or homogeneous stand numerically well-defined.
    covariance = np.cov(samples, rowvar=False) + np.eye(3) * 25.
    inverse = np.linalg.inv(covariance)
    residual = samples - mean
    distances = np.einsum("ni,ij,nj->n", residual, inverse, residual)
    distance_limit = max(1., float(np.quantile(distances, quantile)))
    # Learned from the same forest, not a hand-picked grass/woodland classifier.
    minimum_greenness = max(0., float(np.quantile(greenness(samples), .05)))
    if greenness(mean) <= 0:
        raise ValueError("The selected forest training pixels do not have a green canopy reference")
    return {"mean_rgb": mean.tolist(), "covariance_rgb": covariance.tolist(),
            "inverse_covariance_rgb": inverse.tolist(), "distance_squared_limit": distance_limit,
            "minimum_greenness": minimum_greenness, "training_pixels": len(samples),
            "color_quantile": quantile, "reference_rgb_noise_floor": 5.}


def classify_tree_color(rgb, valid, excluded, model):
    """Return raw colour matches, non-forest matches, and uncalibrated scores."""
    residual = np.asarray(rgb, dtype=np.float64) - model["mean_rgb"]
    distance = np.einsum("...i,ij,...j->...", residual, model["inverse_covariance_rgb"], residual)
    matches = (valid & (distance <= model["distance_squared_limit"])
               & (greenness(rgb) >= model["minimum_greenness"]))
    scores = np.exp(-.5 * distance)
    return matches, matches & ~excluded, scores


def select_candidates(mask, scores, local_coordinates, min_spacing_m, *, existing=None):
    """Greedy score-ranked spacing, with stable row/column ties and a grid index.

    ``local_coordinates`` maps arrays of pixel row/column indices to arrays of
    local east/north metre coordinates at their pixel centres. Optional existing
    candidates are preserved in order and seed the spacing index.
    """
    if not math.isfinite(min_spacing_m) or min_spacing_m <= 0:
        raise ValueError("Candidate spacing must be positive metres")
    rows, columns = np.nonzero(mask)
    if not len(rows):
        return list(existing or ())
    east, north = local_coordinates(rows, columns)
    east, north = np.asarray(east), np.asarray(north)
    if not np.isfinite(east).all() or not np.isfinite(north).all():
        raise ValueError("Candidate pixels cannot be projected to local metres")
    order = np.lexsort((columns, rows, -scores[rows, columns]))
    occupied, candidates = {}, list(existing or ())
    for candidate in candidates:
        x, y = candidate["east_m"], candidate["north_m"]
        key = math.floor(x/min_spacing_m), math.floor(y/min_spacing_m)
        occupied.setdefault(key,[]).append((x,y))
    squared_spacing = min_spacing_m ** 2
    for index in order:
        x, y = float(east[index]), float(north[index])
        key = math.floor(x / min_spacing_m), math.floor(y / min_spacing_m)
        if any((x-a)**2 + (y-b)**2 < squared_spacing - 1e-9
               for gx in range(key[0]-1, key[0]+2) for gy in range(key[1]-1, key[1]+2)
               for a, b in occupied.get((gx, gy), ())):
            continue
        row, column = int(rows[index]), int(columns[index])
        occupied.setdefault(key, []).append((x, y))
        candidates.append({"row": row, "column": column, "east_m": x, "north_m": y,
                           "score": float(scores[row, column])})
    return candidates
