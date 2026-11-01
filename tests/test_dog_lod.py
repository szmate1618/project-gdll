"""Dog LoD publication is restricted to approved names and remains atomic."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from src.glb import read_glb
from tools.prepare_dog_lods import prepare
from test_zombie_lod import fixture, HELPER


@unittest.skipUnless(HELPER.is_file(), 'Build zombie_mesh_simplify first')
class DogLodTests(unittest.TestCase):
    def test_three_appearances_keep_rigs_and_ignore_comparison_models(self):
        with tempfile.TemporaryDirectory() as directory:
            models = Path(directory)
            source = fixture()
            original, original_binary = read_glb(source)
            for variant in range(1, 4):
                (models / f'undead_dog_variant_{variant}.glb').write_bytes(source)
            (models / 'undead_dog_resolution_comparison.glb').write_bytes(b'not a runtime variant')
            with contextlib.redirect_stdout(io.StringIO()):
                prepare(models, HELPER)
            manifest = json.loads((models / 'lods.json').read_text())
            self.assertEqual(set(manifest['models']), {f'undead_dog_variant_{i}.glb' for i in range(1, 4)})
            for entry in manifest['models'].values():
                self.assertEqual(entry['source_sha256'], hashlib.sha256(source).hexdigest())
                self.assertEqual(len(entry['levels']), 3)
                for level in entry['levels']:
                    output = (models / level['path']).read_bytes()
                    self.assertEqual(hashlib.sha256(output).hexdigest(), level['sha256'])
                    document, binary = read_glb(output)
                    for name in ('nodes', 'skins', 'animations'):
                        self.assertEqual(document[name], original[name])
                    self.assertEqual(binary[:len(original_binary)], original_binary)

    def test_failed_rebuild_retains_last_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            models = Path(directory)
            previous = b'{"version":1,"models":{"last-good":{}}}\n'
            (models / 'lods.json').write_bytes(previous)
            (models / 'undead_dog_variant_1.glb').write_bytes(fixture())
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(FileNotFoundError):
                prepare(models, HELPER)
            self.assertEqual((models / 'lods.json').read_bytes(), previous)


if __name__ == '__main__':
    unittest.main()
