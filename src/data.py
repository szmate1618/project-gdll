"""Public geospatial downloads with validated, workspace-local caches.

Remote COGs are read with HTTP byte ranges and their native-resolution AOI
windows are persisted as GeoTIFFs. No full Sentinel scenes, credentials, or
external temporary directories are needed. Processing reads only these files.
"""

from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import logging
import math
from pathlib import Path
import time

import numpy as np
import rasterio
from rasterio.enums import ColorInterp
from rasterio.warp import transform_bounds
from rasterio.windows import Window, from_bounds
import requests
from shapely.geometry import box, shape


LOG = logging.getLogger(__name__)
EARTH_SEARCH = "https://earth-search.aws.element84.com/v1/search"
OVERPASS_ENDPOINTS = (
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
)
DEM_BASE = "https://copernicus-dem-30m.s3.amazonaws.com"
CACHE_VERSION = 1


class SourceError(RuntimeError):
    """A required source is unavailable or invalid, with actionable context."""


def _key(value: object) -> str:
    serialized = json.dumps(value, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(serialized.encode()).hexdigest()[:16]


def _write_json(path: Path, value: object) -> None:
    temporary = path.with_suffix(path.suffix + ".part")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding="utf-8")
    temporary.replace(path)


def _read_json(path: Path) -> dict | None:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        return value if isinstance(value, dict) else None
    except (OSError, ValueError):
        return None


def _now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _request_json(
    session: requests.Session,
    url: str,
    path: Path,
    *,
    timeout: int,
    retries: int,
    params: dict | None = None,
    data: dict | None = None,
) -> dict:
    """Persist HTTP responses beside their request, including diagnostic errors."""
    _write_json(path.with_suffix(".request.json"), {"url": url, "params": params, "data": data})
    last_error = None
    for attempt in range(retries + 1):
        try:
            response = session.request(
                "POST" if data is not None else "GET", url,
                params=params, data=data, timeout=(min(30, timeout), timeout),
            )
            try:
                response.raise_for_status()
                payload = response.json()
                if not isinstance(payload, dict):
                    raise ValueError("Expected a JSON object")
            except (requests.RequestException, ValueError):
                path.with_suffix(".http-error.txt").write_bytes(response.content)
                raise
            _write_json(path, payload)
            return payload
        except (requests.RequestException, ValueError) as exc:
            last_error = exc
            LOG.warning("Request failed (%s/%s): %s", attempt + 1, retries + 1, exc)
            if attempt < retries:
                time.sleep(min(2 ** attempt, 8))
    raise SourceError(f"Could not retrieve {url}: {last_error}") from last_error


def _valid_osm(payload: dict | None) -> bool:
    return bool(
        payload and not payload.get("remark")
        and isinstance(payload.get("elements"), list)
        and all(isinstance(element, dict) and "type" in element for element in payload["elements"])
    )


def _download_osm(config: dict, directory: Path, session: requests.Session) -> tuple[Path, dict]:
    west, south, east, north = config["bbox"]
    bounds = f"{south},{west},{north},{east}"
    query = (
        '[out:json][timeout:90];\n(\n'
        f'  way["building"]({bounds});\n'
        f'  relation["building"]["type"="multipolygon"]({bounds});\n'
        f'  way["highway"]({bounds});\n'
        ');\nout body geom;\n'
    )
    key = _key({"query": query, "version": CACHE_VERSION})
    path = directory / f"osm_{key}.json"
    metadata_path = path.with_suffix(".meta.json")
    cached = _read_json(path)
    if not config.get("refresh") and _valid_osm(cached):
        LOG.info("Using cached OSM extract: %s", path.name)
        return path, _read_json(metadata_path) or {
            "path": str(path), "license": "ODbL 1.0", "attribution": "© OpenStreetMap contributors",
            "timestamp_osm_base": cached.get("osm3s", {}).get("timestamp_osm_base"),
        }
    if config.get("offline"):
        raise SourceError(f"Offline mode: no valid OSM cache at {path}. Run once with network access.")
    (directory / f"osm_{key}.overpassql").write_text(query, encoding="utf-8")
    errors = []
    network = config.get("network", {})
    for endpoint in OVERPASS_ENDPOINTS:
        LOG.info("Downloading OSM buildings and roads from %s", endpoint)
        try:
            payload = _request_json(
                session, endpoint, path, data={"data": query},
                timeout=int(network.get("timeout_seconds", 120)),
                retries=int(network.get("retries", 3)),
            )
            if not _valid_osm(payload):
                raise SourceError(f"Incomplete Overpass result: {payload.get('remark', 'invalid elements')}")
            metadata = {
                "source": "OpenStreetMap via Overpass API", "url": endpoint,
                "downloaded_at": _now(), "bbox": config["bbox"],
                "timestamp_osm_base": payload.get("osm3s", {}).get("timestamp_osm_base"),
                "elements": len(payload["elements"]), "path": str(path),
                "license": "ODbL 1.0", "attribution": "© OpenStreetMap contributors",
            }
            _write_json(metadata_path, metadata)
            return path, metadata
        except SourceError as exc:
            errors.append(str(exc))
            LOG.warning("Trying the next Overpass endpoint: %s", exc)
    raise SourceError("All Overpass endpoints failed. Retry later or use an existing cache. " + " | ".join(errors))


def _valid_raster(path: Path, rgb: bool = False) -> bool:
    if not path.is_file():
        return False
    try:
        with rasterio.open(path) as dataset:
            if not dataset.crs or dataset.width < 2 or dataset.height < 2:
                return False
            if dataset.count != (3 if rgb else 1):
                return False
            if rgb and any(dtype != "uint8" for dtype in dataset.dtypes):
                return False
            pixels = dataset.read(masked=True)
            values = pixels.compressed()
            return bool(values.size and np.isfinite(values).any() and (not rgb or np.any(values > 0)))
    except (OSError, rasterio.errors.RasterioError, ValueError):
        return False


def _crop_cog(url: str, path: Path, bounds: list[float], network: dict, rgb: bool = False) -> dict:
    """Read native pixels over HTTP ranges; output and any GDAL scratch stay local."""
    timeout = int(network.get("timeout_seconds", 120))
    temporary = path.with_suffix(".part.tif")
    env = {
        "GDAL_DISABLE_READDIR_ON_OPEN": "EMPTY_DIR",
        "CPL_VSIL_CURL_ALLOWED_EXTENSIONS": ".tif,.tiff",
        "GDAL_HTTP_TIMEOUT": str(timeout),
        "GDAL_HTTP_CONNECTTIMEOUT": str(min(30, timeout)),
        "GDAL_HTTP_MAX_RETRY": str(int(network.get("retries", 3))),
        "GDAL_HTTP_RETRY_DELAY": "2",
        "CPL_TMPDIR": str(path.parent),
        "AWS_NO_SIGN_REQUEST": "YES",
        "GDAL_PAM_ENABLED": "NO",
        "VSI_CACHE": False,
    }
    try:
        with rasterio.Env(**env), rasterio.open(url) as source:
            if source.crs is None:
                raise SourceError(f"Raster has no coordinate system: {url}")
            target_bounds = transform_bounds("EPSG:4326", source.crs, *bounds, densify_pts=21)
            raw_window = from_bounds(*target_bounds, transform=source.transform)
            left = max(0, math.floor(raw_window.col_off))
            top = max(0, math.floor(raw_window.row_off))
            right = min(source.width, math.ceil(raw_window.col_off + raw_window.width))
            bottom = min(source.height, math.ceil(raw_window.row_off + raw_window.height))
            if right - left < 2 or bottom - top < 2:
                raise SourceError(f"Raster does not overlap the requested area: {url}")
            window = Window(left, top, right - left, bottom - top)
            indexes = [1, 2, 3] if rgb else [1]
            data = source.read(indexes, window=window, masked=True)
            if rgb:
                if data.dtype != np.uint8:
                    raise SourceError(f"Expected an 8-bit Sentinel visual asset, received {data.dtype}: {url}")
                valid = ~np.any(np.ma.getmaskarray(data), axis=0) & np.any(data.filled(0) > 0, axis=0)
                if np.mean(valid) < 0.98:
                    raise SourceError(f"Sentinel scene has {100 * (1 - np.mean(valid)):.1f}% missing RGB pixels in the AOI")
                nodata = 0
                output = data.filled(nodata)
            else:
                nodata = -9999.0
                output = data.astype("float32").filled(nodata)
                output[~np.isfinite(output)] = nodata
                if not np.any(output != nodata):
                    raise SourceError(f"Copernicus DEM contains no usable elevations: {url}")
            transform = source.window_transform(window)
            profile = {
                "driver": "GTiff", "width": int(window.width), "height": int(window.height),
                "count": len(indexes), "dtype": output.dtype, "crs": source.crs,
                "transform": transform, "nodata": nodata, "compress": "deflate",
                "predictor": 2 if rgb else 3,
            }
            with rasterio.open(temporary, "w", **profile) as target:
                target.write(output)
                if rgb:
                    target.colorinterp = (ColorInterp.red, ColorInterp.green, ColorInterp.blue)
                target.update_tags(source_url=url, requested_bbox=json.dumps(bounds), native_resolution="true")
            metadata = {
                "url": url, "downloaded_at": _now(), "requested_bbox": bounds,
                "crs": source.crs.to_string(), "width": int(window.width), "height": int(window.height),
                "pixel_size": [abs(transform.a), abs(transform.e)], "path": str(path),
                "storage": "native-resolution AOI window read from public COG with HTTP byte ranges",
            }
        if not _valid_raster(temporary, rgb=rgb):
            raise SourceError(f"Downloaded raster failed validation: {temporary}")
        temporary.replace(path)
        return metadata
    except (rasterio.errors.RasterioError, OSError) as exc:
        raise SourceError(f"Unable to read public raster {url}. Check network access and retry: {exc}") from exc
    finally:
        temporary.unlink(missing_ok=True)


def _dem_tile_name(latitude: int, longitude: int) -> str:
    northing = f"{'N' if latitude >= 0 else 'S'}{abs(latitude):02d}_00"
    easting = f"{'E' if longitude >= 0 else 'W'}{abs(longitude):03d}_00"
    return f"Copernicus_DSM_COG_10_{northing}_{easting}_DEM"


def _download_dem(config: dict, directory: Path, bounds: list[float]) -> tuple[list[Path], list[dict]]:
    west, south, east, north = bounds
    key = _key({"bounds": bounds, "version": CACHE_VERSION})
    paths, metadata_list = [], []
    for latitude in range(math.floor(south), math.ceil(north)):
        for longitude in range(math.floor(west), math.ceil(east)):
            tile = _dem_tile_name(latitude, longitude)
            url = f"{DEM_BASE}/{tile}/{tile}.tif"
            path = directory / f"{tile}_{key}.tif"
            metadata_path = path.with_suffix(".meta.json")
            if not config.get("refresh") and _valid_raster(path):
                LOG.info("Using cached Copernicus DEM: %s", path.name)
                metadata = _read_json(metadata_path) or {"url": url, "path": str(path), "tile": tile}
            else:
                if config.get("offline"):
                    raise SourceError(f"Offline mode: no valid DEM cache at {path}. Run once with network access.")
                LOG.info("Downloading native Copernicus GLO-30 pixels from %s", tile)
                _write_json(path.with_suffix(".request.json"), {"url": url, "bbox": bounds})
                metadata = _crop_cog(url, path, bounds, config.get("network", {}))
                metadata.update({
                    "source": "Copernicus DEM GLO-30 Public (AWS 2021 release)", "tile": tile,
                    "vertical_datum": "EGM2008 orthometric height in metres",
                    "license_url": "https://registry.opendata.aws/copernicus-dem/",
                })
                _write_json(metadata_path, metadata)
            paths.append(path)
            metadata_list.append(metadata)
    if not paths:
        raise SourceError("No Copernicus DEM tiles intersect the configured bounds")
    return paths, metadata_list


def _download_sentinel(
    config: dict, directory: Path, bounds: list[float], session: requests.Session,
) -> tuple[Path, dict]:
    settings = config.get("sentinel", {})
    date_range = settings.get("datetime", "2025-05-01T00:00:00Z/2025-09-30T23:59:59Z")
    cloud_limit = float(settings.get("max_cloud_cover", 20))
    key = _key({"bounds": bounds, "datetime": date_range, "cloud_limit": cloud_limit, "version": CACHE_VERSION})
    path = directory / f"sentinel_rgb_{key}.tif"
    metadata_path = path.with_suffix(".meta.json")
    if not config.get("refresh") and _valid_raster(path, rgb=True) and _read_json(metadata_path):
        LOG.info("Using cached Sentinel RGB: %s", path.name)
        return path, _read_json(metadata_path)
    if config.get("offline"):
        raise SourceError(f"Offline mode: no valid Sentinel RGB cache at {path}. Run once with network access.")
    network = config.get("network", {})
    params = {
        "collections": "sentinel-2-l2a", "bbox": ",".join(str(value) for value in bounds),
        "datetime": date_range, "limit": 50,
        "query": json.dumps({"eo:cloud_cover": {"lte": cloud_limit}}),
        "sortby": "+properties.eo:cloud_cover",
    }
    search_path = directory / f"search_{key}.json"
    search = None if config.get("refresh") else _read_json(search_path)
    if not search or not isinstance(search.get("features"), list):
        LOG.info("Searching public Sentinel-2 catalog for cloud cover <= %s%%", cloud_limit)
        search = _request_json(
            session, EARTH_SEARCH, search_path, params=params,
            timeout=int(network.get("timeout_seconds", 120)), retries=int(network.get("retries", 3)),
        )
    features = search.get("features", [])
    if not isinstance(features, list):
        raise SourceError(f"Sentinel catalog returned an invalid response; inspect {search_path}")
    features.sort(key=lambda item: float(item.get("properties", {}).get("eo:cloud_cover", 100)))
    aoi = box(*bounds)
    errors = []
    for feature in features[:20]:
        asset = feature.get("assets", {}).get("visual", {})
        url = asset.get("href", "")
        if not url.startswith("https://") or not feature.get("geometry"):
            continue
        if not shape(feature["geometry"]).covers(aoi):
            continue
        cloud = float(feature.get("properties", {}).get("eo:cloud_cover", 100))
        if cloud > cloud_limit:
            continue
        LOG.info("Downloading Sentinel RGB AOI from %s (tile clouds: %.4f%%)", feature["id"], cloud)
        _write_json(directory / f"selected_item_{key}.json", feature)
        _write_json(path.with_suffix(".request.json"), {"url": url, "bbox": bounds, "item_id": feature["id"]})
        try:
            metadata = _crop_cog(url, path, bounds, network, rgb=True)
            metadata.update({
                "source": "Sentinel-2 L2A true-colour RGB via Earth Search / AWS sentinel-cogs",
                "item_id": feature["id"], "datetime": feature.get("properties", {}).get("datetime"),
                "tile_cloud_cover_percent": cloud, "catalog_url": EARTH_SEARCH,
                "attribution": "Contains modified Copernicus Sentinel data ("
                + str(feature.get("properties", {}).get("datetime", "unknown date"))[:4] + ")",
            })
            _write_json(metadata_path, metadata)
            return path, metadata
        except SourceError as exc:
            errors.append(str(exc))
            LOG.warning("Sentinel candidate failed; trying the next scene: %s", exc)
    raise SourceError(
        "No usable Sentinel RGB scene fully covers the area. Increase sentinel.max_cloud_cover, "
        "widen sentinel.datetime, or choose a smaller bbox (single-tile Sentinel coverage is required). "
        + " | ".join(errors[-3:])
    )


def download_sources(config: dict, root: Path) -> dict:
    """Download/cache OSM JSON, DEM AOI tiles, and a georeferenced RGB AOI.

    Returns ``osm: Path``, ``dem: list[Path]``, ``sentinel: Path``, and JSON-safe
    ``provenance``. ``offline`` forbids network access; ``refresh`` replaces the
    selected caches. An explicit refresh and offline mode cannot be combined.
    """
    root = Path(root).resolve()
    try:
        bounds = [float(value) for value in config["bbox"]]
        west, south, east, north = bounds
        if not all(math.isfinite(value) for value in bounds):
            raise ValueError("non-finite coordinate")
        if not (-180 <= west < east <= 180 and -90 < south < north < 90):
            raise ValueError("invalid ordering or coordinate range")
    except (KeyError, TypeError, ValueError) as exc:
        raise SourceError("bbox must be [west, south, east, north] in longitude/latitude degrees") from exc
    if config.get("offline") and config.get("refresh"):
        raise SourceError("offline and refresh cannot both be enabled")
    config = {**config, "bbox": bounds}
    # Padding covers the UTM envelope and interpolation around the terrain edge.
    raster_bounds = [max(-180, west - 0.002), max(-89.999, south - 0.002),
                     min(180, east + 0.002), min(89.999, north + 0.002)]
    directories = {name: root / "data" / "raw" / name for name in ("osm", "copernicus_dem", "sentinel")}
    for directory in directories.values():
        directory.mkdir(parents=True, exist_ok=True)
    with requests.Session() as session:
        session.headers["User-Agent"] = "godollo-scene-prototype/1.0 (public geospatial research)"
        osm_path, osm_metadata = _download_osm(config, directories["osm"], session)
        dem_paths, dem_metadata = _download_dem(config, directories["copernicus_dem"], raster_bounds)
        rgb_path, rgb_metadata = _download_sentinel(config, directories["sentinel"], raster_bounds, session)
    return {
        "osm": osm_path, "dem": dem_paths, "sentinel": rgb_path,
        "provenance": {"osm": osm_metadata, "copernicus_dem": dem_metadata, "sentinel": rgb_metadata},
    }
