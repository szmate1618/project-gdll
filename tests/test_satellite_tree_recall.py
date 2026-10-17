"""Calibration interpolation, contextual proposals, and baseline preservation."""
from copy import deepcopy
import unittest

import numpy as np

from src.satellite_tree_detection import select_candidates
from src.satellite_tree_recall import (bilinear_rgb, classify_recall_color,
                                        fit_recall_color, validate_recall_profile)


class SatelliteRecallTests(unittest.TestCase):
    def test_bilinear_weights_pixel_centres_and_invalid_stencils(self):
        rgb=np.array([[[10,20,30],[110,20,30]],[[10,220,30],[110,220,230]]],dtype=np.uint8)
        valid=np.ones((2,2),bool)
        samples,good=bilinear_rgb(rgb,valid,[.25,0,-.1],[.75,0,0])
        np.testing.assert_array_equal(good,[True,True,False])
        np.testing.assert_allclose(samples[:2],[[35,170,67.5],[10,20,30]])
        self.assertTrue(np.isnan(samples[2]).all())
        valid[1,1]=False
        _,good=bilinear_rgb(rgb,valid,[.25],[.75])
        self.assertFalse(good[0])

    def test_broader_colour_needs_nearby_valid_green_and_obeys_exclusion(self):
        model=fit_recall_color(np.tile([110,105,75],(30,1)),np.full(30,.5),.4)
        rgb=np.full((10,10,3),180,np.uint8)
        rgb[4,4]=[110,105,75]
        valid=np.ones((10,10),bool)
        excluded=np.zeros((10,10),bool)
        self.assertFalse(classify_recall_color(rgb,valid,excluded,model)[1][4,4])
        rgb[4,5]=[60,120,35]
        self.assertTrue(classify_recall_color(rgb,valid,excluded,model)[1][4,4])
        excluded[4,4]=True
        raw,outside,_=classify_recall_color(rgb,valid,excluded,model)
        self.assertTrue(raw[4,4]); self.assertFalse(outside[4,4])
        valid[4,5]=False
        self.assertFalse(classify_recall_color(rgb,valid,excluded,model)[0][4,4])

    def test_existing_candidates_win_even_against_higher_recall_scores(self):
        mask=np.ones((1,5),bool)
        scores=np.array([[1.,1.,100.,1.,1.]])
        coordinates=lambda rows,cols:(cols*10.,rows*10.)
        baseline=[dict(row=0,column=1,east_m=10.,north_m=0.,score=.01)]
        result=select_candidates(mask,scores,coordinates,12.,existing=baseline)
        self.assertEqual([p['column'] for p in result],[1,3])
        self.assertEqual(result[0],baseline[0])
        self.assertEqual(select_candidates(mask&False,scores,coordinates,12.,existing=baseline),baseline)

    def test_profile_rejects_wrong_scene_and_invalid_covariance(self):
        model=fit_recall_color(np.tile([110,105,75],(30,1)),np.full(30,.5),.4)
        profile=dict(schema='godollo.satellite_tree_recall_model',version=1,map_sha256='map',
                     forest_sha256='forest',satellite_sha256='image',baseline_color_model={},model=model)
        validate_recall_profile(profile,'map','forest','image',{})
        for key in ('map_sha256','forest_sha256','satellite_sha256','baseline_color_model'):
            altered=deepcopy(profile);altered[key]='stale'
            with self.assertRaisesRegex(ValueError,'does not match'):
                validate_recall_profile(altered,'map','forest','image',{})
        altered=deepcopy(profile);altered['model']['inverse_covariance_rgb'][0][0]=-1
        with self.assertRaisesRegex(ValueError,'invalid colour'):
            validate_recall_profile(altered,'map','forest','image',{})


if __name__=='__main__':
    unittest.main()
