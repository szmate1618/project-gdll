"""Native raster coordinates retain rotation and separate horizontal scales."""
import unittest
import numpy as np
import rasterio
from src.satellite_tree_data import read_satellite
from src.satellite_tree_detection import DetectionSettings
from tests.satellite_tree_fixtures import SatelliteTreeFixture


class SatelliteRasterTests(SatelliteTreeFixture,unittest.TestCase):
    def test_rotated_anisotropic_pixels_keep_their_native_centres(self):
        from affine import Affine
        origin = self.frame['origin']
        transform = Affine.translation(origin[0]-500, origin[1]+500)*Affine.rotation(12)*Affine.scale(5, -8)
        image = self.root/'rotated.tif'
        pixels = np.full((3, 200, 200), 80, np.uint8)
        with rasterio.open(image, 'w', driver='GTiff', width=200, height=200, count=3,
                           dtype='uint8', crs=self.frame['crs'], transform=transform) as target:
            target.write(pixels)
        rgb, valid, centres, excluded, training, coordinates, metadata = read_satellite(
            image, self.frame, self.bounds, self.forest, DetectionSettings())
        east, north = coordinates(np.array([2, 5]), np.array([3, 7]))
        for i, (row, column) in enumerate([(2, 3), (5, 7)]):
            x, y = transform*(column+metadata['window']['column']+.5, row+metadata['window']['row']+.5)
            self.assertAlmostEqual(east[i], x-origin[0])
            self.assertAlmostEqual(north[i], y-origin[1])
        np.testing.assert_allclose(metadata['pixel_axis_lengths_in_map_m'], [5, 8], atol=1e-8)
        self.assertGreater(valid.sum(), 0)
        self.assertTrue(np.all(training <= excluded))
