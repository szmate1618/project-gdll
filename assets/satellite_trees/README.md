# Tuned2 Sentinel colour profiles

These saved profiles support `tools/detect_tuned2_trees.py`:

- `astra_recall_model.json`: mixed-pixel colour statistics learned from missed
  original detections, combined with nearby greenness checks.
- `astra_precision_model.json`: colour and neighbourhood texture filter learned
  from reviewed false detections, with minimum keep score **0.40**.

Both profiles are bound to the recorded map, native Sentinel image and Astra
colour-reference forests by SHA-256. The precision profile also binds the recall
profile. The caller's forest exclusion file is a separate input; changing it
does not alter the learned colour model.

Inference reads image features and these saved statistics. It does not read
orthophotos, original tree positions or reviewed rectangle coordinates. The
scores are not calibrated probabilities, and positions are native pixel-centre
proposals rather than resolved trunks.

Calibration recipes, reviewed annotations, audit tools and the other experimental
detectors are preserved on the local `experiments/satellite-tree-detection`
branch. Provenance hashes in these profiles may refer to inputs available only
there or in ignored local datasets.

See [tuned2 preparation](../../docs/TUNED2_TREES.md) for usage and input contracts.
