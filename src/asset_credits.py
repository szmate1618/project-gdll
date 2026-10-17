"""Build a map's distribution notice from its published GLB metadata."""
from __future__ import annotations

import re

ASSET_CREDIT_FILES = ("README.txt", "OPENSTREETMAP.txt", "SENTINEL.txt", "COPERNICUS_DEM.txt",
                      "WEAPONS.txt", "ZOMBIES.txt", "AUDIO.txt",
                      "VEGETATION_SOURCES.txt", "GENERATED_ARTWORK.txt")
SOFTWARE_CREDIT_FILES = ("README.txt", "nlohmann-json.txt", "stb.txt", "meshoptimizer.txt",
                         "PREPARATION_TOOLS.txt")


def map_credits(document: dict, name: str, digest: str) -> bytes:
    """Use the selected scene, never an adjacent report or today's config/year."""
    scenes = document.get("scenes", [])
    metadata = scenes[document.get("scene", 0)].get("extras", {}) if scenes else {}
    sources = metadata.get("sources", {})
    lines = ["PACKAGED MAP — DATA ATTRIBUTION", "", f"Map: {name}", f"SHA-256: {digest}", ""]
    if "osm" in sources or "fences" in sources:
        lines.extend(["Map data © OpenStreetMap contributors (ODbL 1.0).",
                      "https://www.openstreetmap.org/copyright", "See OPENSTREETMAP.txt.", ""])
    if "sentinel" in sources:
        sentinel = sources["sentinel"]
        acquisition = sentinel.get("datetime") or sentinel.get("acquisition_datetime")
        if not isinstance(acquisition, str) or not re.match(r"^[12]\d{3}-\d{2}-\d{2}(?:T|$)", acquisition):
            raise ValueError("Sentinel source is missing its acquisition date; restore map provenance before packaging")
        lines.extend([f"Contains modified Copernicus Sentinel data {acquisition[:4]}",
                      f"Acquisition: {acquisition}", "See SENTINEL.txt for terms.", ""])
        if sentinel.get("item_id"):
            lines.extend([f"Sentinel item: {sentinel['item_id']}", ""])
    if "copernicus_dem" in sources:
        lines.extend(["Terrain elevation: Copernicus DEM GLO-30 / WorldDEM-30.",
                      "See COPERNICUS_DEM.txt for the required acknowledgment, disclaimer and terms.", ""])
    # Retain any additional source notices already published in the scene.
    if metadata.get("attribution"):
        lines.extend(["Original attribution embedded in the published map:", str(metadata["attribution"]), ""])
    for key, source in sources.items():
        if key not in {"osm", "fences", "sentinel", "copernicus_dem"}:
            lines.extend([f"Additional dataset: {key}",
                          str(source.get("attribution", "No attribution recorded; its license requires review."))
                          if isinstance(source, dict) else str(source), ""])
    if not sources and not metadata.get("attribution"):
        lines.extend(["No geographic source attribution is recorded in this map.",
                      "The project's generated sample needs no geographic data credit.",
                      "For other maps, supply the original providers' notices and permissions.", ""])
    return ("\n".join(lines).rstrip() + "\n").encode("utf-8")


def vegetation_credits(preset: str, layers: list) -> bytes:
    """Credit the actual packaged vegetation, including nested combined-layer sources.

    Layer tuples contain (archive name, parsed published document, SHA-256).
    Image years come from those documents, never the town map or today's config.
    """
    lines = ['PACKAGED VEGETATION — DATA ATTRIBUTION', '', f'Preset: {preset}', '']
    years, notices = set(), set()
    orthophoto = False

    def visit(value):
        nonlocal orthophoto
        if isinstance(value, list):
            for child in value:
                visit(child)
        elif isinstance(value, dict):
            method = value.get('method', '')
            if value.get('orthophotos') or 'orthophoto' in method:
                orthophoto = True
            satellite = value.get('satellite', {})
            acquisition = satellite.get('acquisition_datetime') or value.get('imagery_date')
            needs_date = method in ('astra_recall_with_negative_colour_context_filter',
                                    'independent_manual_satellite_only_visual_tracing')
            if needs_date and (not isinstance(acquisition, str)
                               or not re.match(r'^[12]\d{3}-\d{2}-\d{2}(?:T|$)', acquisition)):
                raise ValueError('Vegetation Sentinel acquisition date is missing; regenerate trees or restore forest provenance')
            if acquisition:
                if not isinstance(acquisition, str) or not re.match(r'^[12]\d{3}-\d{2}-\d{2}(?:T|$)', acquisition):
                    raise ValueError('Vegetation has an invalid Sentinel acquisition date')
                years.add((acquisition[:4], acquisition))
            if isinstance(value.get('attribution'), str):
                notices.add(value['attribution'])
            for child in value.values():
                if isinstance(child, (dict, list)):
                    visit(child)

    for name, document, digest in layers:
        lines.extend([f'Layer: {name}', f'SHA-256: {digest}'])
        if isinstance(document.get('instances'), list):
            lines.append(f'Individual trees (including row samples): {len(document["instances"])}')
        if isinstance(document.get('patches'), list):
            lines.append(f'Forest patches: {len(document["patches"])}')
        lines.append('')
        visit(document.get('source', {}))
    if preset in ('osm-tuned2', 'osm') and layers:
        lines.extend(['Vegetation map data © OpenStreetMap contributors (ODbL 1.0).',
                      'Includes mapped tree nodes, sampled tree rows and forest polygons.',
                      'https://www.openstreetmap.org/copyright', 'See OPENSTREETMAP.txt.', ''])
    for year, acquisition in sorted(years):
        lines.extend([f'Contains modified Copernicus Sentinel data {year}',
                      f'Vegetation reference acquisition: {acquisition}', 'See SENTINEL.txt.', ''])
    for notice in sorted(notices):
        lines.extend(['Recorded layer attribution:', notice, ''])
    if orthophoto:
        lines.extend(['Includes orthophoto-derived data. Provider/license verification remains unresolved;',
                      'see VEGETATION_SOURCES.txt. This notice does not establish redistribution rights.', ''])
    if not layers:
        lines.extend(['No vegetation layer is included in this pack.', ''])
    return ('\n'.join(lines).rstrip()+'\n').encode('utf-8')
