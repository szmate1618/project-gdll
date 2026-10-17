"""The production detector uses a caller-selected mask and a fixed colour reference."""
import hashlib
import json
from pathlib import Path
from unittest.mock import patch
import unittest

import numpy as np
from scipy.spatial import cKDTree
from shapely.geometry import Point,box
from shapely.geometry.polygon import orient

from src.forest_comparison import load_tree_points
from src.satellite_tree_data import read_satellite
from src.satellite_tree_detection import DetectionSettings,learn_forest_color
from src.satellite_tree_precision import FEATURE_NAMES
from src.satellite_tree_recall import fit_recall_color
from src.satellite_tree_tuned2 import prepare_tuned2_trees
from tests.satellite_tree_fixtures import SatelliteTreeFixture
from tools.detect_tuned2_trees import main


class Tuned2PreparationTests(SatelliteTreeFixture,unittest.TestCase):
    def setUp(self):
        super().setUp()
        rgb,_,_,_,training,_,_=read_satellite(self.image,self.frame,self.bounds,self.forest,DetectionSettings())
        self.reference_model=learn_forest_color(rgb,training)
        sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
        self.recall_path=self.root/'recall.json'
        recall=dict(schema='godollo.satellite_tree_recall_model',version=1,map_sha256=sha(self.map_path),
                    forest_sha256=sha(self.forest_path),satellite_sha256=sha(self.image),
                    baseline_color_model=self.reference_model,
                    model=fit_recall_color(np.tile([110,105,75],(30,1)),np.full(30,.5),
                                           self.reference_model['minimum_greenness']),calibration={})
        self.recall_path.write_text(json.dumps(recall))
        self.precision_path=self.root/'precision.json'
        precision=dict(schema='godollo.satellite_tree_precision_model',version=1,map_sha256=sha(self.map_path),
                       forest_sha256=sha(self.forest_path),satellite_sha256=sha(self.image),
                       recall_profile_sha256=sha(self.recall_path),calibration={},
                       model=dict(feature_names=FEATURE_NAMES,feature_mean=[0.]*len(FEATURE_NAMES),
                                  feature_scale=[1.]*len(FEATURE_NAMES),coefficients=[0.]*(2*len(FEATURE_NAMES)),
                                  intercept=10.,minimum_score=.4,feature_expansion='standardized_linear_and_squares_clipped_8'))
        self.precision_path.write_text(json.dumps(precision))
        self.arguments=dict(config_path=self.config,map_path=self.map_path,training_forest_path=self.forest_path,
                            image_path=self.image,output_dir=self.output,recall_profile_path=self.recall_path,
                            precision_profile_path=self.precision_path,root=self.root)

    def alternate_exclusions(self):
        x,y=self.transform*(30.5,25.5)
        east,north=x-self.frame['origin'][0],y-self.frame['origin'][1]
        geometry=orient(box(east-15,north-15,east+15,north+15))
        document=json.loads(json.dumps(self.document))
        document['patches']=[{'id':'alternate','polygon':list(geometry.exterior.coords),'holes':[]}]
        path=self.root/'alternate.json';path.write_text(json.dumps(document))
        return path,document,geometry

    def test_cli_mask_is_independent_from_calibration_reference(self):
        path,document,geometry=self.alternate_exclusions()
        self.assertEqual(main(['--forest-exclusions',str(path),'--config',str(self.config),'--map',str(self.map_path),
                              '--colour-reference-forests',str(self.forest_path),'--image',str(self.image),
                              '--recall-profile',str(self.recall_path),'--precision-profile',str(self.precision_path),
                              '--output-dir',str(self.output)]),0)
        result=json.loads((self.output/'trees.instances.json').read_text())
        self.assertEqual(result['source']['color_model'],self.reference_model)
        self.assertNotEqual(result['source']['forest']['sha256'],result['source']['training_forest']['sha256'])
        points=load_tree_points(self.output/'trees.instances.json',document)
        self.assertTrue(any(self.forest.contains(Point(p)) for p in points))
        self.assertFalse(any(geometry.intersects(Point(p)) for p in points))
        self.assertFalse(cKDTree(points).query_pairs(12-1e-7))
        for instance in result['instances']:
            x,y=self.transform*(instance['source']['pixel_column']+.5,instance['source']['pixel_row']+.5)
            self.assertAlmostEqual(instance['matrix'][12],x-self.frame['origin'][0])
            self.assertAlmostEqual(instance['matrix'][14],-(y-self.frame['origin'][1]))

    def test_wrong_exclusion_frame_and_training_calibration_preserve_outputs(self):
        path,document,_=self.alternate_exclusions()
        prepare_tuned2_trees(path,**self.arguments)
        previous={p.name:p.read_bytes() for p in self.output.iterdir()}
        document['source']['map_sha256']='stale';path.write_text(json.dumps(document))
        with self.assertRaisesRegex(ValueError,'exclusion data does not match'):
            prepare_tuned2_trees(path,**self.arguments)
        self.assertEqual(previous,{p.name:p.read_bytes() for p in self.output.iterdir()})
        path,_,_=self.alternate_exclusions()
        profile=json.loads(self.precision_path.read_text())
        profile['forest_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
        self.precision_path.write_text(json.dumps(profile))
        with self.assertRaisesRegex(ValueError,'Precision profile does not match'):
            prepare_tuned2_trees(path,**self.arguments)
        self.assertEqual(previous,{p.name:p.read_bytes() for p in self.output.iterdir()})

    def test_publication_failure_restores_the_last_good_dataset(self):
        prepare_tuned2_trees(self.forest_path,**self.arguments)
        previous={p.name:p.read_bytes() for p in self.output.iterdir()}
        import os
        replace=os.replace
        def fail_report(source,target):
            if Path(target)==self.output/'report.json':raise OSError('simulated publication failure')
            return replace(source,target)
        with patch('src.satellite_tree_data.os.replace',side_effect=fail_report):
            with self.assertRaisesRegex(OSError,'simulated publication'):
                prepare_tuned2_trees(self.forest_path,**self.arguments)
        self.assertEqual(previous,{p.name:p.read_bytes() for p in self.output.iterdir()})

    def test_exclusions_argument_required_and_original_outputs_protected(self):
        with self.assertRaises(SystemExit) as error:
            main([])
        self.assertEqual(error.exception.code,2)
        with self.assertRaisesRegex(ValueError,'separate output'):
            prepare_tuned2_trees(self.forest_path,**{**self.arguments,'output_dir':self.map_path.parent})


if __name__=='__main__':
    unittest.main()
