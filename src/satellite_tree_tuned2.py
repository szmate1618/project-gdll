"""Prepare tuned2 Sentinel trees with independently supplied forest exclusions.

The Astra colour-reference forest remains bound to the learned profiles. The
exclusion file controls only where detection is allowed; both frames must match
the published map. No reference-tree or negative-rectangle coordinates are read.
"""
from __future__ import annotations

from dataclasses import asdict
import json
import os
from pathlib import Path

import numpy as np
from pyproj import Transformer
from shapely import intersects_xy

from .config import ROOT, load_config
from .forest_comparison import file_sha256, load_forest
from .satellite_tree_data import (default_satellite_image, make_instance, publish_detections,
                                  read_satellite, validate_forest_frame)
from .satellite_tree_detection import DetectionSettings, classify_tree_color, learn_forest_color, select_candidates
from .satellite_tree_precision import pixel_features, precision_scores, validate_precision_profile
from .satellite_tree_recall import classify_recall_color, validate_recall_profile
from .satellite_tree_review import render_review
from .tree_assets import load_impostor
from .tree_map import load_tree_map


def prepare_tuned2_trees(forest_exclusions, *, config_path=ROOT/"config.json", map_path=ROOT/"output/godollo.glb",
                         training_forest_path=None, image_path=None, output_dir=None, asset_path=None,
                         settings=None, root=ROOT, recall_profile_path=None, precision_profile_path=None,
                         reuse_calibration=False):
    """Run the fixed colour/context model outside a caller-selected forest file."""
    root, map_path, config_path = Path(root).resolve(), Path(map_path).resolve(), Path(config_path).resolve()
    exclusion_path = Path(forest_exclusions).resolve()
    training_path = Path(training_forest_path).resolve() if training_forest_path else root/"forest_patches_astra.json"
    recall_path = Path(recall_profile_path).resolve() if recall_profile_path else root/"assets/satellite_trees/astra_recall_model.json"
    precision_path = Path(precision_profile_path).resolve() if precision_profile_path else root/"assets/satellite_trees/astra_precision_model.json"
    output_dir = Path(output_dir).resolve() if output_dir else map_path.parent/"satellite_trees/sentinel-astra-tuned2"
    settings = settings or DetectionSettings()
    settings.validate()
    if output_dir in (root,map_path.parent,map_path.parent/"osm"):
        raise ValueError("Use a separate output directory to preserve existing tree and forest data")
    fingerprints = {path:file_sha256(path) for path in (exclusion_path,training_path,recall_path,precision_path,config_path)}
    exclusion_doc,exclusion_geometry = load_forest(exclusion_path)
    training_doc,training_geometry = ((exclusion_doc,exclusion_geometry) if training_path==exclusion_path
                                      else load_forest(training_path))
    recall = json.loads(recall_path.read_text(encoding="utf-8"))
    precision = json.loads(precision_path.read_text(encoding="utf-8"))
    image_path = Path(image_path).resolve() if image_path else default_satellite_image(root,training_path,training_doc)
    asset_path = (Path(asset_path).resolve() if asset_path else
                  (training_path.parent/training_doc.get("runtime",{}).get("tree_asset","assets/tree_impostor.glb")).resolve())
    fingerprints.update({path:file_sha256(path) for path in (image_path,asset_path)})
    terrain,frame,map_info = load_tree_map(map_path,load_config(config_path))
    validate_forest_frame(exclusion_doc,frame,terrain,map_info,"exclusion")
    # Rebuilt buildings/roads need not invalidate an image-based colour model.
    # Reuse still requires the exact reference file, native image, pixel-trained
    # base model and geographic frame. Runtime exclusions always bind to this map.
    calibration_map = training_doc["source"]["map_sha256"] if reuse_calibration else map_info["map_sha256"]
    validate_forest_frame(training_doc,frame,terrain,{**map_info,"map_sha256":calibration_map},"training")
    fingerprints[map_path] = map_info["map_sha256"]
    names = ("trees.instances.json","candidates.geojson","report.json","review.png")
    protected = {*fingerprints,root/"forest_patches.json",root/"forest_patches_astra.json",
                 map_path.parent/"trees.instances.json",map_path.parent/"osm/trees.instances.json",
                 map_path.parent/"osm/forest_patches.json"}
    if any(output_dir/name in protected for name in names):
        raise ValueError("Detection outputs must not overwrite an input or existing vegetation dataset")
    rgb,valid,forest_centres,excluded,training,coordinates,raster_info = read_satellite(
        image_path,frame,terrain.bounds,exclusion_geometry,settings,training_geometry=training_geometry)
    reference_source = training_doc.get("source", {})
    if reference_source.get("imagery_date"):
        raster_info["acquisition_datetime"] = reference_source["imagery_date"]
    if reference_source.get("satellite_scene"):
        raster_info["scene"] = reference_source["satellite_scene"]
    model = learn_forest_color(rgb,training,settings.color_quantile)
    # Calibration binds to the colour-reference forest, never the selected exclusion mask.
    validate_recall_profile(recall,calibration_map,fingerprints[training_path],fingerprints[image_path],model)
    validate_precision_profile(precision,calibration_map,fingerprints[training_path],
                               fingerprints[image_path],fingerprints[recall_path])
    all_matches,matches,scores = classify_tree_color(rgb,valid,excluded,model)
    candidates = select_candidates(matches,scores,coordinates,settings.min_spacing_m)
    baseline_count = len(candidates)
    extra_all,extra_matches,extra_scores = classify_recall_color(rgb,valid,excluded,recall["model"])
    candidates = select_candidates(extra_matches,extra_scores,coordinates,settings.min_spacing_m,existing=candidates)
    for candidate in candidates[baseline_count:]:
        candidate["method"] = "missed_reference_colour_and_nearby_greenness"
    all_matches,matches = all_matches|extra_all,matches|extra_matches
    before_precision_count = len(candidates)
    features = pixel_features(rgb,valid)
    keep_scores = np.zeros(valid.shape,dtype=float)
    keep_scores[all_matches] = precision_scores(features[all_matches],precision["model"])
    accepted = keep_scores>=precision["model"]["minimum_score"]
    baseline_retained = sum(bool(accepted[c["row"],c["column"]]) for c in candidates[:baseline_count])
    candidates = [c for c in candidates if accepted[c["row"],c["column"]]]
    for candidate in candidates:
        candidate["negative_example_keep_score"] = float(keep_scores[candidate["row"],candidate["column"]])
    all_matches,matches = all_matches&accepted,matches&accepted
    if candidates and np.any(intersects_xy(exclusion_geometry,[c["east_m"] for c in candidates],
                                           [c["north_m"] for c in candidates])):
        raise ValueError("Candidate intersects excluded forest geometry")
    asset = load_impostor(asset_path)
    offset = raster_info["window"]["column"],raster_info["window"]["row"]
    instances = [make_instance(c,rgb,asset,terrain,offset) for c in candidates]
    source = {"method":"astra_recall_with_negative_colour_context_filter","forest_source":"sentinel-astra-tuned2",
              "training_forest_source":"sentinel-astra","exclusion_forest_source":"configured",
              "calibration_map_sha256":calibration_map,
              "forest":{"path":str(exclusion_path),"sha256":fingerprints[exclusion_path]},
              "training_forest":{"path":str(training_path),"sha256":fingerprints[training_path]},
              "satellite":{**raster_info,"sha256":fingerprints[image_path]},"settings":asdict(settings),
              "color_model":model,"recall_color_model":recall["model"],"recall_calibration":recall["calibration"],
              "recall_profile":{"path":str(recall_path),"sha256":fingerprints[recall_path]},
              "precision_model":precision["model"],"precision_calibration":precision["calibration"],
              "precision_profile":{"path":str(precision_path),"sha256":fingerprints[precision_path]},
              "limitations":["Heuristic 10 m pixel-centre proposals, not resolved trunks or measured crowns.",
                  "Lawns, crops, roofs and shrubs can resemble tree colours; true trees can be missed.",
                  "All pixels touched by the selected forest polygons are excluded; holes remain searchable.",
                  "Older detections and reviewed rectangles calibrate statistics only; inference uses no such coordinates or orthophoto pixels.",
                  "Keep scores are not calibrated probabilities; threshold-selection coverage is validation, not an independent test.",
                  "Tree dimensions use display-asset defaults, not satellite measurements."]}
    counts = {"valid_pixels":int(valid.sum()),"forest_centre_pixels":int(forest_centres.sum()),
              "training_forest_centre_pixels":int(forest_centres.sum()),"forest_excluded_pixels":int((excluded&valid).sum()),
              "training_pixels":int(training.sum()),"matching_pixels":int(all_matches.sum()),
              "nonforest_matching_pixels":int(matches.sum()),"instances":len(instances),
              "baseline_instances_before_precision_filter":baseline_count,"baseline_instances_retained":baseline_retained,
              "tuned_instances_before_precision_filter":before_precision_count,
              "suppressed_instances":before_precision_count-len(instances),"additional_instances":len(instances)-baseline_retained}
    placement = {"schema":"godollo.tree-instances","version":1,
                 "map":{**map_info,"path":os.path.relpath(map_path,output_dir)},"source":source,
                 "assets":{"tree":{**asset,"path":os.path.relpath(asset_path,output_dir)}},
                 "instances":instances,"counts":counts,
                 "transforms":{"units":"meters","axes":{"X":"east","Y":"up","Z":"south"},
                               "matrix_layout":"column-major","matrix_authoritative":True}}
    to_geo = Transformer.from_crs(frame["crs"],"EPSG:4326",always_xy=True)
    features_out = []
    for candidate,instance in zip(candidates,instances):
        lon,lat = to_geo.transform(candidate["east_m"]+frame["origin"][0],candidate["north_m"]+frame["origin"][1],errcheck=True)
        features_out.append({"type":"Feature","id":instance["id"],"geometry":{"type":"Point","coordinates":[lon,lat]},
                             "properties":{**instance["source"],"east_m":candidate["east_m"],"north_m":candidate["north_m"]}})
    report = {"schema":"godollo.satellite_tree_detection","version":1,"source":source,
              "map_sha256":map_info["map_sha256"],"counts":counts,
              "all_forest_mean_rgb":rgb[forest_centres].mean(axis=0).tolist(),"all_forest_mean_source":"training_forest"}
    review = render_review(rgb,excluded,training,matches,candidates,model,"sentinel-astra-tuned2")
    publish_detections(output_dir,{"trees.instances.json":placement,"report.json":report,
                                   "candidates.geojson":{"type":"FeatureCollection","features":features_out}},review,fingerprints)
    return {"output":str(output_dir),"forest_mean_rgb":model["mean_rgb"],**counts}
