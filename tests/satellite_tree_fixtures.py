"""Small published-map/forest/native-image fixture for satellite tree preparation."""
import hashlib
import json
from pathlib import Path
import tempfile

import numpy as np
from PIL import Image
import rasterio
from rasterio.features import rasterize
from rasterio.transform import from_bounds
from shapely.affinity import translate
from shapely.geometry import box, mapping
from shapely.geometry.polygon import orient

from tests import test_tree_map
from tools.create_tree_impostors import build_impostor


class SatelliteTreeFixture:
    def setUp(self):
        scratch = Path(__file__).resolve().parents[1]/'build'
        scratch.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        fixture = test_tree_map.TreeMapTests()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        fixture.write_map()
        self.map_path = self.root/'map.glb'
        self.map_path.write_bytes(fixture.path.read_bytes())
        self.config = self.root/'config.json'
        self.config.write_text(json.dumps(fixture.config))
        self.frame = fixture.frame
        self.bounds = tuple(np.asarray(fixture.frame['bounds'], dtype=np.float32).astype(float))
        x0, y0, x1, y1 = self.bounds
        midpoint = (x0+x1)/2
        # A visible clearing in the west forest must remain searchable.
        self.clearing = box(x0+18, y0+55, midpoint-15, y0+95)
        self.forest = orient(box(x0+3, y0+3, midpoint, y1-3).difference(self.clearing))
        origin = self.frame['origin']
        self.transform = from_bounds(x0+origin[0], y0+origin[1], x1+origin[0], y1+origin[1], 40, 50)
        rgb = np.full((50, 40, 3), [160, 160, 150], np.uint8)
        self.forest_mask = rasterize([(mapping(translate(self.forest, *origin)), 1)],
                                     out_shape=rgb.shape[:2], transform=self.transform).astype(bool)
        rgb[self.forest_mask] = [40, 70, 35]
        # A tree-coloured pixel east of the forest and pixels inside its hole.
        rgb[25, 30] = [40, 70, 35]
        clearing = rasterize([(mapping(translate(self.clearing.buffer(-6), *origin)), 1)],
                              out_shape=rgb.shape[:2], transform=self.transform).astype(bool)
        rgb[clearing] = [40, 70, 35]
        self.image = self.root/'sentinel.tif'
        with rasterio.open(self.image, 'w', driver='GTiff', width=40, height=50, count=3,
                           dtype='uint8', crs=self.frame['crs'], transform=self.transform) as target:
            target.write(np.moveaxis(rgb, -1, 0))
        self.asset = self.root/'tree.glb'
        for suffix in ('', '-side', '-top-mid', '-top-two-thirds'):
            Image.new('RGBA', (16, 16), (40, 70, 35, 255)).save(self.root/f'tree{suffix}.png')
        self.asset.write_bytes(build_impostor('tree', 10., self.root))
        self.forest_path = self.root/'forest.json'
        self.document = {'schema': 'godollo.forest_patches', 'version': 1,
            'coordinate_frame': {'units': 'meters', 'coordinate_order': ['east', 'north'],
                'crs': self.frame['crs'], 'origin_projected_m': list(origin), 'bounds_m': list(self.bounds)},
            'source': {'map_sha256': hashlib.sha256(self.map_path.read_bytes()).hexdigest()},
            'runtime': {'tree_asset': 'tree.glb'},
            'patches': [{'id': 'forest', 'polygon': list(self.forest.exterior.coords),
                         'holes': [list(h.coords) for h in self.forest.interiors]}]}
        self.forest_path.write_text(json.dumps(self.document))
        self.output = self.root/'detections'
