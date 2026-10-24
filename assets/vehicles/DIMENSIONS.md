# Vehicle dimensions and source scale

Vehicle TOMLs are the runtime fitting inputs. `dimensions.length_m` is the chosen
real-car overall length. The preparation command applies **one uniform scale**
to the selected mesh after its reviewed yaw correction and scene transforms.
`width_m` and `height_m` record the resulting mesh bounds, including mirrors and
other protrusions; they are not claims about factory body width or roof height.
The generator checks these measurements against the TOML before publication.

The downloaded models do not share reliable meter units. Several use arbitrary
scales, and even sources close to meter scale have stylized proportions. The old
runtime preparation then fitted every car to 3.6 m, making most too short and the
Fiats too long. Original and selected source GLBs remain untouched. Runtime LoDs
are rebuilt independently from the newly fitted selected mesh.

All measurements below include active scene-node transforms and the reviewed yaw.
Original length is in authored units. Fitted values are meters, measured from the
selected mesh; simplification can slightly change its original bounds.

| Asset | Original length | Chosen length | Fitted width | Fitted height | Uniform scale of selected mesh |
| --- | ---: | ---: | ---: | ---: | ---: |
| toyota-corolla-1999 | 11.5457 | 4.1000 | 1.5209 | 1.4546 | 0.355112 |
| volkswagen-golf-v-2006 | 20.2772 | 4.2040 | 1.8687 | 1.7087 | 0.207327 |
| mercedes-benz-s-500-1995 | 11.9611 | 5.1130 | 1.7920 | 1.4517 | 0.427468 |
| volvo-v70 | 8.2039 | 4.7220 | 1.7925 | 1.5289 | 0.575581 |
| suzuki-grand-vitara-2000 | 14.5439 | 4.1800 | 1.5794 | 1.6184 | 0.287406 |
| ford-focus-2005 | 0.8367 | 4.4540 | 1.4135 | 1.6855 | 5.323528 |
| volkswagen-golf-mk1-1983 | 4.2618 | 3.8150 | 1.9420 | 1.3719 | 0.895159 |
| lada-2107 | 9.2731 | 4.1450 | 1.8580 | 1.4416 | 0.446990 |
| opel-astra-2008 | 4.6327 | 4.2490 | 2.0657 | 1.4265 | 0.917323 |
| opel-corsa-b | 3.3655 | 3.7290 | 1.7290 | 1.4251 | 1.105998 |
| fiat-126p-maluch-basic | 3.0549 | 3.1290 | 1.5941 | 1.4405 | 1.031996 |
| fiat-126p-maluch-flat | 3.0549 | 3.1290 | 1.5782 | 1.4260 | 1.021680 |
| fiat-126p-maluch-tuned | 3.0178 | 3.1290 | 1.6202 | 1.5307 | 1.036859 |
| volkswagen-passat-b8 | 2.9477 | 4.7670 | 2.0459 | 1.4967 | 1.619143 |
| suzuki-swift-2024 | 3.8269 | 3.8600 | 1.8694 | 1.5196 | 1.009339 |
| skoda-octavia-2001 | 3.0378 | 4.5070 | 2.0331 | 1.4367 | 1.484909 |

## Reference lengths and assumptions

- Corolla: European 1997–1999 3-door hatchback, **4.100 m**.
  [Toyota brochure](https://autocatalogarchive.com/wp-content/uploads/2019/05/Toyota-Corolla-1998-UK.pdf).
- Golf V: standard hatchback, **4.204 m**.
  [Volkswagen brochure](https://autocatalogarchive.com/wp-content/uploads/2023/08/VW-Golf-2006-AU.pdf).
- S 500: assume standard wheelbase, **5.113 m**. The long version is 5.213 m;
  the low-poly listing does not establish wheelbase.
  [Mercedes model history](https://mercedes-benz-publicarchive.com/marsClassic/en/instance/ko/140-series-S-Class-Saloons-1991---1994.xhtml?oid=4924).
- V70: visually first generation; use **4.722 m**, approximately the 4.72 m
  dimension in the [Volvo brochure](https://www.volvoclub.org.uk/tech/V70TechData1997.pdf).
  Exact model year and drivetrain are unspecified.
- Grand Vitara: 5-door first generation, **4.180 m**, using Canadian-market
  [Suzuki specifications](https://www.centuryu.com/uploads/1/4/4/6/144698822/2000_vitara.pdf).
  Market/body accessories and the rear spare can affect overall extent.
- Focus: visually European Mk1 estate, **4.454 m**, rather than the 4.531 m
  North American ZXW. The asset's year/name alone is ambiguous.
  [Ford manual](https://safe-manuals.com/user-manual/ford/focus-2005).
- Golf Mk1: 1983 hatchback, **3.815 m**.
  [Volkswagen archive](https://www.volkswagen-newsroom.com/en/vehicle-data-golf-1-profile-19468).
- Lada 2107: **4.145 m**.
  [Technical handbook](https://doksi.net/hu/get.php?lid=26915).
- Astra H: 5-door hatchback, **4.249 m**.
  [Vauxhall brochure](https://www.vauxhall.co.uk/content/dam/vauxhall/Home/PDFs/historical-brochures/discounted-models/astra-h/AstraH_September_2007.pdf).
- Corsa B: assume earlier 3-door body, **3.729 m**, rather than the later 3.741 m.
  [FIA homologation](https://historicdb.fia.com/sites/default/files/car_attachment/1601047801/homologation_form_number_5516_group_n.pdf).
- Fiat 126p: use the later bumper reference **3.129 m** for all three derivatives.
  Earlier cars are 3.054 m; source listings do not specify year. The tuned car
  has no factory counterpart, so its target is a gameplay approximation.
  [Model history](https://www.fiat500club.fi/126/Yleistietoa/126-126_Personal-All_the_Fiats.pdf),
  [early-car museum measurements](https://www.mit.krakow.pl/wp-content/uploads/2023/12/9dba51968f6988d6e325f722f1da6092_polski-fiat-126p-passenger-car.pdf).
- Passat B8: pre-facelift saloon, **4.767 m**.
  [Volkswagen specifications](https://www.volkswagen.ie/en/owners-and-services/previous-models/mid-size-class/passat-b8/passat-b8.html).
- Swift: 2024 reference, **3.860 m**.
  [Suzuki dimensions](https://suzuki.bg/data/files/SUZUKI_SWIFT_MY24_PRICE_LIST_20260312.pdf).
- Octavia I: 2001 facelift liftback reference, **4.507 m**. If the stylized
  asset instead represents the earlier front end, that car is 4.511 m.
  [Skoda brochure](https://www.allcarcentral.com/Skoda_pdf/Skoda_OctaviaTour_Catalogue_2009.pdf).

## Physics and occupants

Every type retains the common chassis pivot 0.68 m above its neutral tire support
plane. This preserves ground placement and tilt without changing map sidecars.
Collision boxes use fitted width/height/length; they remain coarse body/cabin
approximations. Active suspension still uses the original single-body-box policy.

TOMLs also specify per-type tire radius/width, approximate axle centers using the
corresponding wheelbase, lights, and four seat/entry/exit positions. These are
initial fitting estimates, not measurements of detailed interiors or separate
wheel meshes. Eye positions stay below fitted roofs, and exits clear the sides.
Imported wheels remain static visual geometry; physical wheels have per-type
cylinder shapes. The procedural fallback scales its tires and body to the config.
Masses remain the existing gameplay values.

## LoD publication

Settings and runtime LoD manifests use version **2**. The manifest records fitting
bounds and ground offset alongside source/output fingerprints. The runtime checks
these against the loaded vehicle definition, rejects old/mismatched fitting as a
whole, and uses the procedural fallback. LOD0 must match the recorded fit closely;
reduced levels may change bounds within the geometric error allowance. Each level
inherits the same fit, without a second normalization.

Run from the repository root:

```bash
.venv/bin/python tools/prepare_vehicle_lods.py
```

After changing reference length, update the measured uniform-fit width/height and
review attachment coordinates in the TOML before regenerating. Rebuild and ship
matching TOMLs and the active version-2 LoD publication to multiplayer peers;
existing raw-byte resource fingerprints remain enforced. Old content-addressed
GLBs and downloaded originals are preserved.
