"""Additional mixed-pixel colour proposals learned from missed reference trees.

These are positive-only calibration samples, not ground truth or a probability
model. The original Astra candidates remain authoritative seeds for spacing.
"""
from __future__ import annotations

import numpy as np
from scipy.ndimage import maximum_filter

from .satellite_tree_detection import greenness


def bilinear_rgb(rgb, valid, columns, rows):
    """Sample four neighbours in pixel-centre coordinates; reject invalid stencils.

    Integer coordinates are native pixel centres. Convert a GeoTIFF pixel-edge
    affine result by subtracting 0.5 before calling. No extrapolation or nodata
    renormalization is performed. Invalid samples return NaN and a false mask.
    """
    columns, rows = np.asarray(columns,dtype=float), np.asarray(rows,dtype=float)
    if columns.shape != rows.shape or columns.ndim != 1 or not np.isfinite([columns,rows]).all():
        raise ValueError("Bilinear coordinates must be finite one-dimensional arrays")
    left, top = np.floor(columns).astype(int), np.floor(rows).astype(int)
    good = (left>=0)&(top>=0)&(left+1<rgb.shape[1])&(top+1<rgb.shape[0])
    indices = np.flatnonzero(good)
    c,r = left[indices],top[indices]
    good[indices] &= valid[r,c]&valid[r,c+1]&valid[r+1,c]&valid[r+1,c+1]
    indices = np.flatnonzero(good)
    c,r = left[indices],top[indices]
    dx,dy = columns[indices]-c,rows[indices]-r
    result = np.full((len(columns),3),np.nan)
    result[indices] = (rgb[r,c]*((1-dx)*(1-dy))[:,None]+rgb[r,c+1]*(dx*(1-dy))[:,None]+
                       rgb[r+1,c]*((1-dx)*dy)[:,None]+rgb[r+1,c+1]*(dx*dy)[:,None])
    return result,good


def fit_recall_color(samples, neighbourhood_greenness, minimum_canopy_greenness=0.):
    """Fit the fixed recipe to training samples only; evaluation stays separate."""
    samples = np.asarray(samples, dtype=float)
    neighbourhood_greenness = np.asarray(neighbourhood_greenness, dtype=float)
    if (samples.ndim != 2 or samples.shape[1] != 3 or len(samples) < 25
            or neighbourhood_greenness.shape != (len(samples),)
            or not np.isfinite(samples).all() or not np.isfinite(neighbourhood_greenness).all()
            or not np.isfinite(minimum_canopy_greenness) or minimum_canopy_greenness < 0):
        raise ValueError("Recall calibration needs at least 25 finite RGB/context samples")
    mean = samples.mean(axis=0)
    covariance = np.cov(samples, rowvar=False)+np.eye(3)*25.
    inverse = np.linalg.inv(covariance)
    residual = samples-mean
    distances = np.einsum("ni,ij,nj->n", residual, inverse, residual)
    return {"mean_rgb": mean.tolist(), "covariance_rgb": covariance.tolist(),
            "inverse_covariance_rgb": inverse.tolist(),
            "distance_squared_limit": max(1., float(np.quantile(distances,.85))),
            "minimum_greenness": max(0.,float(np.quantile(greenness(samples),.35))),
            "minimum_neighbourhood_greenness": max(minimum_canopy_greenness,float(np.quantile(neighbourhood_greenness,.25))),
            "reference_canopy_minimum_greenness": minimum_canopy_greenness,
            "training_samples": len(samples), "color_quantile": .85,
            "greenness_quantile": .35, "neighbourhood_greenness_quantile": .25,
            "neighbourhood_size_pixels": 3, "reference_rgb_noise_floor": 5.}


def classify_recall_color(rgb, valid, excluded, model):
    """Add a broader colour ellipsoid with weak-green and nearby-green gates."""
    residual = np.asarray(rgb,dtype=float)-model["mean_rgb"]
    distance = np.einsum("...i,ij,...j->...",residual,model["inverse_covariance_rgb"],residual)
    green = greenness(rgb)
    context = maximum_filter(np.where(valid,green,-1.),size=model["neighbourhood_size_pixels"],
                             mode="constant",cval=-1.)
    matches = (valid & (distance<=model["distance_squared_limit"])
               & (green>=model["minimum_greenness"])
               & (context>=model["minimum_neighbourhood_greenness"]))
    scores = np.exp(-.5*distance/model["distance_squared_limit"])
    return matches, matches & ~excluded, scores


def validate_recall_profile(profile, map_sha256, forest_sha256, image_sha256, baseline_model):
    """Reject a calibration trained on a different scene, mask or base model."""
    if (profile.get("schema") != "godollo.satellite_tree_recall_model" or profile.get("version") != 1
            or profile.get("map_sha256") != map_sha256
            or profile.get("forest_sha256") != forest_sha256
            or profile.get("satellite_sha256") != image_sha256
            or profile.get("baseline_color_model") != baseline_model):
        raise ValueError("Recall profile does not match the map, Astra forest, Sentinel image or baseline colour model")
    model = profile["model"]
    inverse = np.asarray(model["inverse_covariance_rgb"],dtype=float)
    if (np.asarray(model["mean_rgb"]).shape != (3,) or inverse.shape != (3,3)
            or not np.isfinite(model["mean_rgb"]).all() or not np.isfinite(inverse).all()
            or not np.allclose(inverse,inverse.T) or np.linalg.eigvalsh(inverse).min() <= 0
            or not np.isfinite(model["distance_squared_limit"]) or model["distance_squared_limit"] <= 0
            or model["neighbourhood_size_pixels"] != 3
            or any(not np.isfinite(model[key]) or not 0 <= model[key] <= 2
                   for key in ("minimum_greenness","minimum_neighbourhood_greenness"))):
        raise ValueError("Recall profile contains invalid colour/context parameters")
