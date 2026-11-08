"""Preserve recoverable originals even when a filesystem rejects rollback."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from src.map_publication import publish_files


class MapPublicationTests(unittest.TestCase):
    def test_incomplete_rollback_retains_backups_and_restores_other_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            staging = root / "staging"
            staging.mkdir()
            pending = {}
            for name in ("map.glb", "map.navmesh", "report.json"):
                target = root / name
                target.write_bytes(b"previous " + name.encode())
                source = staging / name
                source.write_bytes(b"new " + name.encode())
                pending[target] = source
            replace = Path.replace

            def fail_publication_and_one_restore(path, destination):
                if destination == root / "report.json":
                    raise OSError("publication failure")
                if path.parent.parent.name.startswith(".map-backup-") and destination == root / "map.navmesh":
                    raise OSError("rollback failure")
                return replace(path, destination)

            with patch.object(Path, "replace", fail_publication_and_one_restore):
                with self.assertRaisesRegex(RuntimeError, "previous files retained in"):
                    publish_files(pending)
            self.assertEqual((root / "map.glb").read_bytes(), b"previous map.glb")
            self.assertEqual((root / "report.json").read_bytes(), b"previous report.json")
            backups = list(root.glob(".map-backup-*"))
            self.assertEqual(len(backups), 1)
            self.assertEqual((backups[0] / "1/map.navmesh").read_bytes(), b"previous map.navmesh")

    def test_rollback_preserves_companions_with_same_basename(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "osm").mkdir()
            first, second, last = root / "trees.json", root / "osm/trees.json", root / "last.json"
            for path, content in [(first, b"first"), (second, b"second"), (last, b"last")]:
                path.write_bytes(content)
            pending = {}
            for i, path in enumerate([first, second, last]):
                source = root / f"pending-{i}"
                source.write_bytes(b"replacement")
                pending[path] = source
            replace = Path.replace

            def fail_last(path, destination):
                if destination == last:
                    raise OSError("publication failure")
                return replace(path, destination)

            with patch.object(Path, "replace", fail_last):
                with self.assertRaises(OSError):
                    publish_files(pending)
            self.assertEqual(first.read_bytes(), b"first")
            self.assertEqual(second.read_bytes(), b"second")
