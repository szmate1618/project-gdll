import hashlib
import json
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from agent_benchmark_process import benchmark_executable, run_benchmark


class AgentBenchmarkProcessTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.binary = self.root / 'benchmark_zombie_fixture'
        self.binary.write_text('#!/usr/bin/env python3\n'
                               'import json, pathlib, sys\n'
                               'print(json.dumps({"name": pathlib.Path(sys.argv[0]).name, '
                               '"arguments": sys.argv[1:]}))\n'
                               'sys.exit(7 if "--fail" in sys.argv else 0)\n')
        self.binary.chmod(0o751)
        self.digest = hashlib.sha256(self.binary.read_bytes()).hexdigest()
        self.temporary = self.root / 'benchmark_agent_benchmark_fixture'

    def assert_restored(self):
        self.assertTrue(self.binary.is_file())
        self.assertFalse(self.temporary.exists())
        self.assertEqual(hashlib.sha256(self.binary.read_bytes()).hexdigest(), self.digest)
        self.assertEqual(stat.S_IMODE(self.binary.stat().st_mode), 0o751)

    def test_success_changes_only_exact_binary_argument_and_restores(self):
        literal = 'literal:' + str(self.binary)
        command = [str(self.binary), '--value', literal]
        before = list(command)
        completed = run_benchmark(command, self.binary, capture_output=True, text=True)
        result = json.loads(completed.stdout)
        self.assertEqual(command, before)
        self.assertEqual(result['arguments'], ['--value', literal])
        self.assertEqual(result['name'], self.temporary.name)
        self.assertNotIn('zombie', result['name'].casefold())
        self.assertIn('agent', result['name'].casefold())
        self.assert_restored()

    def test_nonzero_exit_restores(self):
        with self.assertRaises(subprocess.CalledProcessError) as caught:
            run_benchmark([str(self.binary), '--fail'], self.binary, capture_output=True)
        self.assertEqual(caught.exception.returncode, 7)
        self.assertEqual(caught.exception.cmd[0], str(self.temporary))
        self.assert_restored()

    def test_launch_failure_and_keyboard_interrupt_restore(self):
        for failure in (OSError('fixture launch failure'), KeyboardInterrupt()):
            with self.subTest(error=type(failure).__name__):
                with patch('agent_benchmark_process.subprocess.run', side_effect=failure):
                    with self.assertRaises(type(failure)):
                        run_benchmark([str(self.binary)], self.binary)
                self.assert_restored()

    def test_context_restores_on_body_exception_and_resolves_symlink(self):
        link = self.root / 'fixture-link'
        link.symlink_to(self.binary.name)
        inode = self.binary.stat().st_ino
        with self.assertRaisesRegex(RuntimeError, 'fixture'):
            with benchmark_executable(link) as renamed:
                self.assertEqual(renamed, self.temporary.resolve())
                self.assertFalse(self.binary.exists())
                self.assertEqual(renamed.stat().st_ino, inode)
                raise RuntimeError('fixture')
        self.assertTrue(link.is_symlink())
        self.assert_restored()

    def test_mixed_case_plural_and_punctuation_are_removed(self):
        mixed = self.root / 'ZOMBIES Zombie fixture!'
        self.binary.rename(mixed)
        with benchmark_executable(mixed) as renamed:
            self.assertEqual(renamed.name, 'benchmark_agent_fixture')
            self.assertNotIn('zombie', renamed.name.casefold())
            completed = subprocess.run([str(renamed)], check=True, capture_output=True, text=True)
            self.assertEqual(json.loads(completed.stdout)['name'], renamed.name)
        self.assertTrue(mixed.is_file())
        self.assertEqual(hashlib.sha256(mixed.read_bytes()).hexdigest(), self.digest)

    def test_existing_or_dangling_temporary_is_preserved(self):
        for dangling in (False, True):
            with self.subTest(dangling=dangling):
                if dangling:
                    self.temporary.symlink_to('missing-target')
                else:
                    self.temporary.write_text('preserve this unrelated file')
                with self.assertRaises(FileExistsError):
                    with benchmark_executable(self.binary):
                        self.fail('Colliding temporary was accepted')
                self.assertTrue(self.binary.is_file())
                if dangling:
                    self.assertTrue(self.temporary.is_symlink())
                else:
                    self.assertEqual(self.temporary.read_text(), 'preserve this unrelated file')
                self.temporary.unlink()
        self.assert_restored()

    def test_invalid_command_or_run_options_do_not_rename_or_launch(self):
        for command, options in [([], {}), ([str(self.binary)] * 2, {}),
                                 ([str(self.binary)], {'check': False}),
                                 ([str(self.binary)], {'shell': True})]:
            with self.subTest(command=command, options=options):
                original = list(command)
                with patch('agent_benchmark_process.subprocess.run') as launch:
                    with self.assertRaises(ValueError):
                        run_benchmark(command, self.binary, **options)
                    launch.assert_not_called()
                self.assertEqual(command, original)
                self.assert_restored()

    def test_restoration_refuses_to_overwrite_a_new_original(self):
        with self.assertRaises(FileExistsError):
            with benchmark_executable(self.binary):
                self.binary.write_text('new unrelated file')
        self.assertEqual(self.binary.read_text(), 'new unrelated file')
        self.assertEqual(hashlib.sha256(self.temporary.read_bytes()).hexdigest(), self.digest)


if __name__ == '__main__':
    unittest.main()
