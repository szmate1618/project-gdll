"""Runtime file selection shared by vegetation preparation and asset packaging."""
from pathlib import Path

VEGETATION_PRESETS = ('osm-tuned2', 'orthophoto', 'osm', 'sentinel-astra', 'none')


def vegetation_paths(root, map_path, preset='osm-tuned2'):
    root, adjacent = Path(root), Path(map_path).parent
    if preset == 'osm-tuned2':
        return adjacent/'trees.instances.json', adjacent/'osm/forest_patches.json'
    if preset == 'orthophoto':
        return adjacent/'trees.original.instances.json', root/'forest_patches.json'
    if preset == 'osm':
        return adjacent/'osm/trees.instances.json', adjacent/'osm/forest_patches.json'
    if preset == 'sentinel-astra':
        return None, root/'forest_patches_astra.json'
    if preset == 'none':
        return None, None
    raise ValueError(f'Unknown vegetation preset: {preset}')
