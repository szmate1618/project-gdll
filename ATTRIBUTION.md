# Data attribution

The generated scene combines the following independent datasets. These notices
also accompany the GLB in `output/ATTRIBUTION.md` and in its scene metadata.
Multiplayer client packages include plain-text notices in `credits/assets/`.
The portable town asset pack adds `credits/assets/MAP.txt` using the published
GLB's provenance, including its actual Sentinel acquisition year. It also adds
`credits/assets/VEGETATION.txt` from the selected tree/forest manifests so their
Sentinel acquisition dates and layer hashes remain separate from the town
texture. OSM attribution includes mapped trees, sampled tree rows and forests.
Generated artwork provenance and the preparation/runtime software inventory
are recorded in `credits/assets/GENERATED_ARTWORK.txt` and `credits/software/`.

* **OpenStreetMap:** © OpenStreetMap contributors, available under the
  [Open Database License (ODbL)](https://www.openstreetmap.org/copyright).
  Retain attribution when sharing the scene. Distribution of an adapted OSM
  database also has ODbL requirements; consult the linked license and
  [OSMF guidance on produced works](https://osmfoundation.org/wiki/Licence/Community_Guidelines/Produced_Work_-_Guideline).
* **Copernicus DEM GLO-30:** produced using Copernicus WorldDEM-30 © DLR e.V.
  2010-2014 and © Airbus Defence and Space GmbH 2014-2018 provided under
  COPERNICUS by the European Union and ESA; all rights reserved.
  The organisations in charge of the Copernicus programme by law or by delegation
  do not incur any liability for any use of the Copernicus WorldDEM-30.
  See the [dataset and license](https://dataspace.copernicus.eu/explore-data/data-collections/copernicus-contributing-missions/collections-description/COP-DEM)
  and [public AWS distribution](https://registry.opendata.aws/copernicus-dem/).
* **Sentinel-2:** Contains modified Copernicus Sentinel data 2025 (default
  acquisition range). The generated scene's metadata and output notice record
  the actual selected acquisition year. Access is free and open under the
  [Copernicus Sentinel data terms](https://cds.climate.copernicus.eu/licences/ec-sentinel).
  The [COG archive](https://registry.opendata.aws/sentinel-2-l2a-cogs/) is indexed
  by [Element 84 Earth Search](https://github.com/Element84/earth-search).

Data providers do not endorse this prototype. Geometry, resampling, coloring,
and inferred building/road dimensions are approximations produced by this code.
The software and third-party data have separate licensing; this project does
not relicense downloaded data.

## Pigeon model attribution

The bird flocks use [Animated Bird, Pigeon](https://sketchfab.com/3d-models/animated-bird-pigeon-797d27b68af3453e865149435df6aa30)
by **Paul Spooner (dudecon / Peripheral Arbor)**, listed under
[Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/).
The model page also links the author's [public domain declaration](https://ip.tryop.com/).
Project modifications include undead-palette recoloring, a yellow-beige 16×16
atlas, a 65 cm wingspan, baked flight animation and reduced meshes for distant
flocks. The model remains separate from the town GLB.

Client packages and content packs retain [PIGEONS.txt](credits/assets/PIGEONS.txt).
See the [pigeon asset documentation](assets/pigeon/README.md) for preparation
details and source fingerprints.

## Software attribution

The viewer and authoritative server use **Recast Navigation / Detour 1.6.0**
by Mikko Mononen and contributors for local zombie obstacle avoidance. The
vendored source subset is pinned to commit
`6dc1667f580357e8a2154c28b7867bea7e8ad3a7` and uses the zlib license. Its
complete notice is in [recastnavigation.txt](credits/software/recastnavigation.txt).
The global navmesh baker and route search are this project's code.

## Gameplay research references

### Mass-dependent object throwing

Background on how throwing performance changes with load:

* Roland van den Tillaar and Gertjan Ettema (2004). **A Force-Velocity
  Relationship and Coordination Patterns in Overarm Throwing.** *Journal of
  Sports Science and Medicine*, 3(4), 211–219.
  [Original paper](https://www.jssm.org/volume03/iss4/cap/jssm-03-211.pdf).
  The study found decreasing release speed and increasing throwing time as
  ball mass increased over the tested 0.2–0.8 kg range.
* Mário C. Marques, Daniel A. Marinho and Roland van den Tillaar (2010).
  **A Load-Velocity Relationship for Men and Women in Overhead Throwing
  Performance.** *Journal of Sports Science and Medicine*, 9(3), 524–526.
  [Publication record and full-text link](https://pubmed.ncbi.nlm.nih.gov/24137102/).
  This study examined the load–velocity relationship with balls of 0.45–5 kg.

These papers support using a load-dependent release speed. **The following
equation is this project's gameplay approximation, not an equation or fitted
model taken from either paper:**

```text
speedScale(m) = sqrt((0.45 + 3) / (m + 3))
velocityGain = (14 * aimDirection + [0, 2, 0]) * speedScale(m)
fullImpulse = limitMagnitude(m * velocityGain, 90)
addedImpulse = charge * fullImpulse
```

Mass `m` is in kilograms, velocity in m/s, and impulse in N·s. The 0.45 kg
reference anchors the existing grenade throw. The 3 kg effective arm/body mass
and 90 N·s strength ceiling are project tuning choices, not anatomical
measurements or experimentally fitted parameters. The square-root curve is a
simple effective-inertia approximation; the ceiling bounds its extrapolation
for heavy objects. It is not validated human-performance data for 10 kg or
heavier props. Grip shape, technique and individual strength are not modeled.

The implementation is in [object_throw.cpp](src/object_throw.cpp). It adds an
impulse without replacing existing body motion. Grenade throws and hinged-door
shoves retain their existing launch and impulse rules, respectively.

**PROJ**, by Frank Warmerdam, Gerald Evenden and PROJ contributors, converts
geographic coordinates during Python map/asset preparation through `pyproj`.
The C++ viewer, server and client use world meter locations and do not depend
on PROJ. Its main MIT-style notice is in [proj.txt](credits/software/proj.txt).
Retain the complete notices for the actual preparation environment, bundled
data and transitive dependencies when distributing them.

The [software inventory](credits/software/README.txt) lists the other runtime
libraries and links to the preparation toolchain credits. Asset ZIPs carry
these credits; Linux and Windows client bundles also copy the Detour notice
into `licenses/`, alongside platform dependency notices.
