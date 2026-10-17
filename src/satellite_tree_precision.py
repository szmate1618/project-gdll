"""Learn scene-wide colour/context suppression from reviewed negative examples.

Inference consumes pixel features and fitted coefficients only. Annotation
coordinates are calibration inputs, never runtime exclusion geometry.
"""
from __future__ import annotations

import numpy as np
from scipy.ndimage import maximum_filter, uniform_filter, map_coordinates
from scipy.optimize import minimize
from scipy.special import expit

from .satellite_tree_detection import greenness
from .satellite_tree_recall import bilinear_rgb


FEATURE_NAMES = ["red", "green", "blue", "greenness", "mean3_red", "mean3_green", "mean3_blue",
                 "std3_brightness", "mean3_greenness", "max3_greenness", "std3_greenness",
                 "mean5_greenness", "std5_brightness", "brightness_minus_mean3"]


def pixel_features(rgb, valid):
    """Native RGB and mask-normalized 3x3/5x5 texture; invalid pixels add no evidence."""
    rgb = np.asarray(rgb,dtype=float)
    green,brightness = greenness(rgb),rgb.mean(axis=-1)
    weights = valid.astype(float)
    def average(values,size):
        numerator = uniform_filter(np.where(valid,values,0.),size=size,mode="constant",cval=0.)
        denominator = uniform_filter(weights,size=size,mode="constant",cval=0.)
        return numerator/np.maximum(denominator,1e-12)
    def std(values,size):
        return np.sqrt(np.maximum(0.,average(values*values,size)-average(values,size)**2))
    channels = [rgb[...,i] for i in range(3)]+[green]
    channels += [average(rgb[...,i],3) for i in range(3)]
    channels += [std(brightness,3),average(green,3),maximum_filter(np.where(valid,green,-1.),size=3,mode="constant",cval=-1.),
                 std(green,3),average(green,5),std(brightness,5),brightness-average(brightness,3)]
    return np.stack(channels,axis=-1)


def sample_features(rgb, valid, features, columns, rows):
    """Four-pixel colour interpolation, with the same weights for local context."""
    colours,good = bilinear_rgb(rgb,valid,columns,rows)
    result = np.column_stack([map_coordinates(features[...,i],[rows,columns],order=1,mode="constant",cval=np.nan)
                              for i in range(features.shape[-1])])
    result[:,:3] = colours
    result[:,3] = greenness(colours)
    result[:,-1] = colours.mean(axis=1)-result[:,4:7].mean(axis=1)
    good &= np.isfinite(result).all(axis=1)
    return result,good


def _design(features, mean, scale):
    standardized = np.clip((features-mean)/scale,-8.,8.)
    return np.concatenate([standardized,standardized**2],axis=-1)


def fit_precision_model(features, labels, weights, regularization=.03):
    """Class/rectangle-balanced regularized logistic score, not calibrated probability."""
    features,labels,weights = np.asarray(features,float),np.asarray(labels,float),np.asarray(weights,float)
    if (features.ndim != 2 or features.shape[1] != len(FEATURE_NAMES)
            or labels.shape != (len(features),) or weights.shape != labels.shape
            or not np.isfinite(features).all() or not np.isfinite(weights).all()
            or (weights<=0).any() or set(np.unique(labels)) != {0.,1.}):
        raise ValueError("Precision training needs finite features, both labels and positive weights")
    weights = weights/weights.sum()
    mean = np.average(features,axis=0,weights=weights)
    scale = np.sqrt(np.average((features-mean)**2,axis=0,weights=weights))
    scale = np.maximum(scale,1e-3)
    design = _design(features,mean,scale)
    def objective(parameters):
        score = design@parameters[:-1]+parameters[-1]
        loss = np.sum(weights*(np.logaddexp(0.,score)-labels*score))+.5*regularization*np.sum(parameters[:-1]**2)
        residual = weights*(expit(score)-labels)
        gradient = np.r_[design.T@residual+regularization*parameters[:-1],residual.sum()]
        return loss,gradient
    solution = minimize(objective,np.zeros(design.shape[1]+1),jac=True,method="L-BFGS-B",
                        options={"maxiter":300,"ftol":1e-11})
    if not solution.success:
        raise ValueError(f"Precision model did not converge: {solution.message}")
    return {"feature_names":FEATURE_NAMES,"feature_mean":mean.tolist(),"feature_scale":scale.tolist(),
            "coefficients":solution.x[:-1].tolist(),"intercept":float(solution.x[-1]),
            "regularization":regularization,"feature_expansion":"standardized_linear_and_squares_clipped_8",
            "minimum_score":.5}


def precision_scores(features, model):
    design = _design(features,np.asarray(model["feature_mean"]),np.asarray(model["feature_scale"]))
    return expit(design@np.asarray(model["coefficients"])+model["intercept"])


def validate_precision_model(model):
    if (model.get("feature_names") != FEATURE_NAMES
            or model.get("feature_expansion") != "standardized_linear_and_squares_clipped_8"
            or np.asarray(model.get("feature_mean")).shape != (len(FEATURE_NAMES),)
            or np.asarray(model.get("feature_scale")).shape != (len(FEATURE_NAMES),)
            or np.asarray(model.get("coefficients")).shape != (2*len(FEATURE_NAMES),)
            or not np.isfinite([*model["feature_mean"],*model["feature_scale"],*model["coefficients"],model["intercept"],model["minimum_score"]]).all()
            or (np.asarray(model["feature_scale"])<=0).any() or not 0 < model["minimum_score"] < 1):
        raise ValueError("Invalid precision colour/context model")


def validate_precision_profile(profile, map_sha256, forest_sha256, image_sha256, recall_profile_sha256):
    if (profile.get("schema") != "godollo.satellite_tree_precision_model" or profile.get("version") != 1
            or profile.get("map_sha256") != map_sha256 or profile.get("forest_sha256") != forest_sha256
            or profile.get("satellite_sha256") != image_sha256
            or profile.get("recall_profile_sha256") != recall_profile_sha256):
        raise ValueError("Precision profile does not match the map, forest, Sentinel image or recall calibration")
    validate_precision_model(profile["model"])
