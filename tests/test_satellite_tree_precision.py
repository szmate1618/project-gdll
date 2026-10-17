"""Global negative-example scoring must not depend on annotation locations."""
import unittest
from copy import deepcopy
import numpy as np

from src.satellite_tree_precision import (FEATURE_NAMES,pixel_features,sample_features,fit_precision_model,
                                          precision_scores,validate_precision_profile)


class SatellitePrecisionTests(unittest.TestCase):
    def model(self):
        x=np.zeros((60,len(FEATURE_NAMES)))
        x[:30,:3]=[40,70,35];x[30:,:3]=[90,90,55]
        y=np.r_[np.ones(30),np.zeros(30)]
        return fit_precision_model(x,y,np.ones(60)),x

    def test_balanced_score_separates_examples_and_rejects_bad_inputs(self):
        model,x=self.model();scores=precision_scores(x,model)
        self.assertGreater(scores[:30].min(),.7)
        self.assertLess(scores[30:].max(),.3)
        with self.assertRaisesRegex(ValueError,'both labels'):
            fit_precision_model(x,np.ones(60),np.ones(60))

    def test_texture_ignores_invalid_pixels_and_sampling_is_bilinear(self):
        rgb=np.full((9,9,3),[40,70,35],np.uint8);valid=np.ones((9,9),bool)
        rgb[4,5]=[250,250,250];valid[4,5]=False
        image=pixel_features(rgb,valid)
        np.testing.assert_allclose(image[4,4,4:7],[40,70,35],atol=1e-9)
        self.assertLess(image[4,4,7],1e-5)
        rgb=np.array([[[10,20,30],[110,20,30]],[[10,220,30],[110,220,230]]],np.uint8)
        valid=np.ones((2,2),bool);image=pixel_features(rgb,valid)
        samples,good=sample_features(rgb,valid,image,[.25],[.75])
        self.assertTrue(good[0])
        np.testing.assert_allclose(samples[0,:3],[35,170,67.5])

    def test_equal_pixel_neighbourhoods_score_identically_at_different_locations(self):
        rgb=np.full((30,30,3),[90,90,55],np.uint8)
        patch=np.full((5,5,3),[40,70,35],np.uint8);patch[2,2]=[60,80,40]
        rgb[3:8,3:8]=patch;rgb[18:23,18:23]=patch
        image=pixel_features(rgb,np.ones((30,30),bool))
        np.testing.assert_allclose(image[5,5],image[20,20],atol=1e-9)
        model,_=self.model()
        np.testing.assert_allclose(precision_scores(image[[5,20],[5,20]],model)[0],
                                   precision_scores(image[[5,20],[5,20]],model)[1],atol=1e-9)

    def test_scene_and_model_validation(self):
        model,_=self.model()
        p=dict(schema='godollo.satellite_tree_precision_model',version=1,map_sha256='map',forest_sha256='forest',
               satellite_sha256='image',recall_profile_sha256='recall',model=model)
        validate_precision_profile(p,'map','forest','image','recall')
        with self.assertRaisesRegex(ValueError,'does not match'):
            validate_precision_profile(p,'map','forest','image','other')
        p=deepcopy(p);p['model']['minimum_score']=float('nan')
        with self.assertRaisesRegex(ValueError,'Invalid precision'):
            validate_precision_profile(p,'map','forest','image','recall')


if __name__=='__main__':
    unittest.main()
