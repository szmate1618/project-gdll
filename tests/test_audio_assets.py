"""Check shipped recordings against their credit/provenance manifest, offline."""

import array
import hashlib
import json
from pathlib import Path
import sys
import unittest
import wave


DIRECTORY = Path(__file__).resolve().parents[1] / "assets/audio"


class AudioAssetsTests(unittest.TestCase):
    def test_shipped_files_are_credited_and_match_manifest(self):
        manifest = json.loads((DIRECTORY / "sources.json").read_text())
        self.assertEqual(manifest["version"], 1)
        assets = manifest["assets"]
        names = [asset["file"] for asset in assets]
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(set(names), {path.name for path in DIRECTORY.glob("*.wav")})
        self.assertIn("huh.wav", names)
        self.assertIn("intrigued.wav", names)
        credits = (DIRECTORY / "CREDITS.md").read_text()
        for asset in assets:
            with self.subTest(file=asset["file"]):
                self.assertEqual(Path(asset["file"]).name, asset["file"])
                source = manifest["sources"][asset["source"]]
                self.assertTrue(source["used"])
                self.assertIn(source["license"], ("CC0-1.0", "CC-BY-3.0"))
                self.assertIn(source["page"], credits)
                self.assertIn(source["license_url"], credits)
                self.assertEqual(len(source["sha256"]), 64)
                path = DIRECTORY / asset["file"]
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), asset["sha256"])
                with wave.open(str(path), "rb") as wav:
                    self.assertEqual(wav.getnchannels(), 1)
                    self.assertEqual(wav.getsampwidth(), 2)
                    self.assertEqual(wav.getframerate(), 48000)
                    self.assertEqual(wav.getnframes(), asset["frames"])
                    self.assertGreater(wav.getnframes(), 0)
                    self.assertLessEqual(wav.getnframes(), 480000)
                    samples = array.array("h", wav.readframes(wav.getnframes()))
                if sys.byteorder != "little":
                    samples.byteswap()
                self.assertEqual(samples[0], 0)
                self.assertEqual(samples[-1], 0)
                self.assertGreater(max(abs(value) for value in samples), 1000)
                self.assertLess(max(abs(value) for value in samples), 32767)


if __name__ == "__main__":
    unittest.main()
