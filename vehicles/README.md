# Vehicle definitions

Each TOML file defines a car type, following the weapon definitions in `items/`.
There are 16 definitions for the downloaded car types, including the three Fiat
variants. The filename stem is its stable type name. Definitions are loaded once
at startup; the default spawned car uses `toyota_corolla_1999.toml`.

| Field | Meaning |
| --- | --- |
| `name` | Display name used by the entry action |
| `seat_count` | Number of seats, from 1 to 4; must match the `[[seats]]` tables |
| `weight_kg` | Total gameplay mass; the chassis uses this minus the four 25 kg wheels |
| `[dimensions]` | `length_m` real-car fitting target; `width_m` and `height_m` measured fitted mesh bounds, including mirrors |
| `[wheels].radius_m`, `width_m` | Per-type physical tire cylinder dimensions in meters |
| `[wheels]` | `front_left`, `front_right`, `rear_left`, `rear_right` wheel centers |
| `[headlights]` | `left` and `right` light-emitter positions |
| `[[seats]].eye_position` | Seated player's eye/camera position |
| `[[seats]].entry_position` | Center of the entry action's interaction region |
| `[[seats]].entry_radius` | Maximum distance from player's eyes to that center |
| `[[seats]].exit_position` | Preferred standing exit candidate |

All coordinates are chassis-local meters: **X right, Y up, -Z forward**, with
origin at a common pivot 0.68 m above the neutral tire support plane. Wheel
centers are suspension anchors at rest; tire bottoms lie at local Y=-0.68.
Headlight beams
follow chassis rotation and point forward with the existing downward tilt.

Seat order is fixed: **0 front-left driver, 1 front-right, 2 rear-left, 3 rear-right**.
For fewer seats, omit seats from the end and update `seat_count`. Only seat 0
controls throttle, steering, braking and the optional chase camera.

Walk near a configured entry point. When within its radius, with line of sight
to the point and vehicle speed below 1.5 m/s, the window title shows
`Enter: <car name> / <seat>`. Enter requests the nearest available seat.
Multiplayer admission is checked by the server; occupied seats are excluded
from the prompt using the newest confirmed snapshot. Simultaneous requests
cannot reserve the same seat. Up to four clients may occupy one vehicle.

Passengers can look around with the mouse and press Enter to leave. Weapons,
scopes, inventory selection, grabbing, movement and chase-camera selection are
disabled while seated. Enter exits below 1.5 m/s when a supported, unobstructed
standing position exists. The occupied seat's exit is tried first, followed by
other exits. A disconnect releases only that player's seat; losing the driver
applies the brakes. Passenger departures never take over the driver controls.

The initial masses are gameplay tuning values. Seat, wheel and light positions
are approximate per-type fits. [Dimensions and reference assumptions](../assets/vehicles/DIMENSIONS.md)
describe the original source scales and the uniform real-length fit. Collision
boxes and tire cylinders follow the per-type configuration. The runtime renders fitted
car LoDs when their cache is available, otherwise the box-and-wheel prototype.
Imported wheels are static visual geometry. Parked cars use two static box
colliders. Driver entry enables all four physical wheels; driver exit/disconnect
returns to boxes after the car brakes and settles. Passenger-only occupancy does
not activate suspension. See [vehicle LoDs and fitting](../assets/vehicles/README.md#vehicle-lods).

All definitions are fingerprinted in multiplayer and included in portable asset
packs and Linux/Windows client packages. Keep their LF endings as required by
`.gitattributes`. Adding or changing a definition requires matching resources on
both peers. Seat indices and vehicle type IDs use wire protocol **11**; rebuild
client and server together.

## Parked positions

A selected map may have a sibling `abandoned_cars.instances.json` publication
(`godollo.abandoned-car-placements`, version 1). `vehicle_placement.*` validates
its map fingerprint, unique IDs, population, finite positions, and unit
`rotation_xyzw` quaternions before creating any cars. It converts the source +Z
heading to runtime -Z and lifts the chassis origin 0.68 m along the stored local
up axis; heights are already in the map's runtime datum. The quaternion is
authoritative, including ground tilt, so `yaw_rad` is not applied again.

Each level/server session assigns a uniformly random type from the complete
config catalog to each position. Multiplayer clients use the server's choices.
Cars retain their per-type dimensions at the published positions; long cars such
as the S 500 exceed the publication's nominal 2×4.7 m footprint. Existing positions
are not repacked, so crowded placements may overlap. Cars remain drivable rigid bodies.
The sidecar is an ignored
map output, preserved locally and included with its map in portable asset ZIPs.
Missing files use the nearby-car fallback; empty files disable it. Invalid or
stale files fail with a diagnostic instead of silently loading another map's
positions. The population limit is 1,024.
