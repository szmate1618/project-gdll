"""Ready-to-run archives retain client libraries, launchers and matching content."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import shutil
import stat
import tarfile
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.parse import parse_qs, urlsplit
import zipfile

from src.client_bundle import bundle_client, check_local_configuration, unpack_client
from src.github_client_builds import ARTIFACTS, GitHubClientBuilds, repository_from_origin
from tools.package_github_clients import main


class BuildSelectionTests(unittest.TestCase):
    def test_latest_complete_run_includes_manual_builds_and_skips_prs(self):
        github = GitHubClientBuilds("owner/game", "secret")
        runs = [{"id": 4, "event": "pull_request"}, {"id": 3, "event": "push"},
                {"id": 2, "event": "workflow_dispatch"}]
        calls = []

        def get(endpoint):
            calls.append(endpoint)
            if not endpoint:
                return {"default_branch": "master"}
            if endpoint.startswith("actions/workflows/"):
                query = parse_qs(urlsplit(endpoint).query)
                self.assertEqual(query["branch"], ["master"])
                self.assertEqual(query["status"], ["success"])
                return {"workflow_runs": runs}
            expired = endpoint.startswith("actions/runs/3/")
            return {"artifacts": [{"id": index, "name": name, "expired": expired and platform == "linux"}
                                  for index, (platform, name) in enumerate(ARTIFACTS.items())]}

        with patch.object(github, "get", side_effect=get):
            run, artifacts = github.latest(("windows", "linux"))
        self.assertEqual(run["id"], 2)
        self.assertEqual(set(artifacts), {"windows", "linux"})
        self.assertFalse(any("runs/4/" in call for call in calls))

    def test_pages_continue_and_missing_artifacts_fail(self):
        github = GitHubClientBuilds("owner/game", "secret")
        with patch.object(github, "get", side_effect=[{"artifacts": list(range(100))}, {"artifacts": [100]}]):
            self.assertEqual(list(github.pages("artifacts", "artifacts")), list(range(101)))
        with patch.object(github, "get", return_value={"workflow_runs": []}):
            with self.assertRaisesRegex(ValueError, "No successful"):
                github.latest(("windows",), "master")

    def test_origin_and_token_validation(self):
        for origin in ("https://github.com/owner/game.git", "git@github.com:owner/game.git"):
            with patch("src.github_client_builds.subprocess.run") as run:
                run.return_value.stdout = origin + "\n"
                self.assertEqual(repository_from_origin(Path.cwd()), "owner/game")
        with self.assertRaisesRegex(ValueError, "GH_TOKEN"):
            GitHubClientBuilds("owner/game", "")

    def test_download_verifies_digest_and_does_not_forward_token(self):
        github = GitHubClientBuilds("owner/game", "secret")
        content = b"downloaded artifact bytes"
        artifact = {"id": 7, "name": ARTIFACTS["windows"],
                    "digest": "sha256:" + hashlib.sha256(content).hexdigest()}
        redirect = HTTPError("https://api.github.com/artifact", 302, "Found",
                             {"Location": "https://storage.example/signed"}, None)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "artifact.zip"
            with patch("src.github_client_builds.build_opener") as opener, \
                 patch("src.github_client_builds.urlopen", return_value=io.BytesIO(content)) as download:
                opener.return_value.open.side_effect = redirect
                github.download(artifact, output)
                self.assertEqual(output.read_bytes(), content)
                download.assert_called_once_with("https://storage.example/signed", timeout=60)
                self.assertEqual(opener.return_value.open.call_args.args[0].get_header("Authorization"), "Bearer secret")
            with patch("src.github_client_builds.build_opener") as opener:
                opener.return_value.open.return_value = io.BytesIO(b"corrupt")
                with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                    github.download(artifact, output)


class ClientBundleTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.configs = {"storage/equipment.toml": b"tactical vest\n", "inventory.toml": b"starting items\n",
                        "doors.toml": b"max_closed_flex_m = 0.10\n"}
        self.assets = {**self.configs, "output/town.glb": b"town geometry", "play-town.sh": b"#!/bin/sh\n",
                       "play-town.cmd": b"@echo off\r\n", "credits/assets/MAP.txt": b"Map attribution",
                       "credits/assets/UNDEAD_DOGS.txt": (Path(__file__).resolve().parents[1] /
                                                          "credits/assets/UNDEAD_DOGS.txt").read_bytes(),
                       "credits/assets/PIGEONS.txt": (Path(__file__).resolve().parents[1] /
                                                      "credits/assets/PIGEONS.txt").read_bytes(),
                       "assets/pigeon/flock.json": b'{"schema":"godollo.pigeon-flock","version":1}\n',
                       "assets/undead_dog/models/undead_dog_variant_1.glb": b"prepared dog model"}
        self.asset_pack = self.root / "assets.zip"
        self.write_assets()

    def write_assets(self, bad_digest=False):
        hashes = {name: hashlib.sha256(data).hexdigest() for name, data in self.assets.items()}
        if bad_digest:
            hashes["output/town.glb"] = "0" * 64
        with zipfile.ZipFile(self.asset_pack, "w") as archive:
            for name, data in self.assets.items():
                archive.writestr(name, data)
            archive.writestr("asset-pack.json", json.dumps({"version": 1, "files": hashes}))

    def artifact(self, platform):
        package = io.BytesIO()
        executable = "godollo_client.exe" if platform == "windows" else "godollo_client"
        files = {**self.configs, executable: b"game binary", "licenses/license.txt": b"License notice"}
        if platform == "windows":
            files["OpenAL32.dll"] = b"audio library"
            with zipfile.ZipFile(package, "w") as archive:
                for name, data in files.items():
                    archive.writestr(name, data)
            suffix = ".zip"
        else:
            files["run-client.sh"] = b"#!/bin/sh\n"
            files["lib/libaudio.so.1.2"] = b"audio library"
            prefix = ARTIFACTS[platform] + "/"
            with tarfile.open(fileobj=package, mode="w:gz") as archive:
                for name, data in files.items():
                    entry = tarfile.TarInfo(prefix + name)
                    entry.size = len(data)
                    entry.mode = 0o755 if name in (executable, "run-client.sh") else 0o644
                    archive.addfile(entry, io.BytesIO(data))
                link = tarfile.TarInfo(prefix + "lib/libaudio.so.1")
                link.type = tarfile.SYMTYPE
                link.linkname = "libaudio.so.1.2"
                archive.addfile(link)
            suffix = ".tar.gz"
        artifact = self.root / (platform + "-artifact.zip")
        with zipfile.ZipFile(artifact, "w") as archive:
            archive.writestr(ARTIFACTS[platform] + suffix, package.getvalue())
        return artifact

    def provenance(self, platform):
        return {"platform": platform, "commit": "abcd1234", "run_id": 42}

    def test_both_platforms_unwrap_merge_and_keep_runtime_files(self):
        for platform in ARTIFACTS:
            with self.subTest(platform=platform):
                client = unpack_client(self.artifact(platform), platform, self.root / platform)
                output = self.root / (platform + "-town.zip")
                bundle_client(client, self.asset_pack, output, self.provenance(platform))
                prefix = ARTIFACTS[platform] + "/"
                with zipfile.ZipFile(output) as archive:
                    self.assertIsNone(archive.testzip())
                    for name, data in self.assets.items():
                        self.assertEqual(archive.read(prefix + name), data)
                    self.assertEqual(archive.read(prefix + "licenses/license.txt"), b"License notice")
                    self.assertEqual(json.loads(archive.read(prefix + "client-build.json"))["run_id"], 42)
                    self.assertTrue(archive.getinfo(prefix + "play-town.sh").external_attr >> 16 & stat.S_IXUSR)
                    if platform == "linux":
                        self.assertEqual(archive.read(prefix + "lib/libaudio.so.1"), b"audio library")
                        self.assertTrue(archive.getinfo(prefix + "godollo_client").external_attr >> 16 & stat.S_IXUSR)
                        self.assertTrue(archive.getinfo(prefix + "run-client.sh").external_attr >> 16 & stat.S_IXUSR)
                    else:
                        self.assertEqual(archive.read(prefix + "OpenAL32.dll"), b"audio library")

    def test_config_mismatch_and_corrupt_assets_preserve_previous_output(self):
        client = unpack_client(self.artifact("windows"), "windows", self.root / "client")
        output = self.root / "town.zip"
        output.write_bytes(b"previous good package")
        for failure in ("changed_config", "extra_config", "corrupt_asset"):
            with self.subTest(failure=failure):
                if failure == "changed_config":
                    self.assets["inventory.toml"] = b"incompatible inventory"
                elif failure == "extra_config":
                    self.assets["skills/new.skill.toml"] = b"new skill"
                self.write_assets(bad_digest=failure == "corrupt_asset")
                with self.assertRaisesRegex(ValueError, "differ|checksum mismatch"):
                    bundle_client(client, self.asset_pack, output, self.provenance("windows"))
                self.assertEqual(output.read_bytes(), b"previous good package")
                self.assertEqual(list(self.root.glob("town-*.zip")), [])
                self.assets["inventory.toml"] = self.configs["inventory.toml"]
                self.assets.pop("skills/new.skill.toml", None)

    def test_local_preflight_checks_checkout_without_requiring_game_assets(self):
        client = unpack_client(self.artifact("windows"), "windows", self.root / "client")
        check_local_configuration(client, client)
        with self.assertRaisesRegex(ValueError, "local checkout differ"):
            check_local_configuration(client, self.root / "missing")

    def test_door_configuration_mismatch_is_rejected(self):
        client = unpack_client(self.artifact("windows"), "windows", self.root / "client")
        self.assets["doors.toml"] = b"max_closed_flex_m = 0.03\n"
        self.write_assets()
        with self.assertRaisesRegex(ValueError, "differ at doors.toml"):
            bundle_client(client, self.asset_pack, self.root / "town.zip", self.provenance("windows"))

    def test_notice_line_endings_are_portable_but_configuration_bytes_stay_exact(self):
        client = unpack_client(self.artifact("windows"), "windows", self.root / "client")
        notice = "credits/assets/NOTICE.txt"
        self.assets[notice] = b"Shared attribution\nSecond line\n"
        self.write_assets()
        target = client / notice
        target.parent.mkdir(parents=True)
        target.write_bytes(b"Shared attribution\r\nSecond line\r\n")
        output = self.root / "town.zip"
        bundle_client(client, self.asset_pack, output, self.provenance("windows"))
        with zipfile.ZipFile(output) as archive:
            prefix = ARTIFACTS["windows"] + "/"
            data = archive.read(prefix + notice)
            manifest = json.loads(archive.read(prefix + "asset-pack.json"))
            self.assertEqual(data, self.assets[notice])
            self.assertEqual(manifest["files"][notice], hashlib.sha256(data).hexdigest())
        (client / "inventory.toml").write_bytes(self.configs["inventory.toml"].replace(b"\n", b"\r\n"))
        with self.assertRaisesRegex(ValueError, "differ at inventory.toml"):
            bundle_client(client, self.asset_pack, output, self.provenance("windows"))
        (client / "inventory.toml").write_bytes(self.configs["inventory.toml"])
        target.write_bytes(b"Different attribution\r\n")
        with self.assertRaisesRegex(ValueError, "differ at credits/assets/NOTICE.txt"):
            bundle_client(client, self.asset_pack, output, self.provenance("windows"))

    def test_zip_traversal_and_tar_escaping_links_are_rejected(self):
        malicious = self.root / "malicious.zip"
        nested = io.BytesIO()
        with zipfile.ZipFile(nested, "w") as archive:
            archive.writestr("../outside.txt", b"escaped")
        with zipfile.ZipFile(malicious, "w") as archive:
            archive.writestr(ARTIFACTS["windows"] + ".zip", nested.getvalue())
        with self.assertRaisesRegex(ValueError, "Unsafe archive path"):
            unpack_client(malicious, "windows", self.root / "windows")
        nested = io.BytesIO()
        with tarfile.open(fileobj=nested, mode="w:gz") as archive:
            link = tarfile.TarInfo(ARTIFACTS["linux"] + "/escape")
            link.type = tarfile.SYMTYPE
            link.linkname = "../../outside"
            archive.addfile(link)
        with zipfile.ZipFile(malicious, "w") as archive:
            archive.writestr(ARTIFACTS["linux"] + ".tar.gz", nested.getvalue())
        with self.assertRaises(tarfile.FilterError):
            unpack_client(malicious, "linux", self.root / "linux")
        self.assertFalse((self.root / "outside.txt").exists())

    def test_cli_downloads_both_from_one_build_and_publishes_zips(self):
        artifacts = {platform: self.artifact(platform) for platform in ARTIFACTS}
        output = self.root / "distribution"
        run = {"id": 42, "head_sha": "a" * 40, "html_url": "https://github.com/owner/game/actions/runs/42"}
        with patch("tools.package_github_clients.GitHubClientBuilds") as constructor, \
             contextlib.redirect_stdout(io.StringIO()):
            github = constructor.return_value
            github.repository = "owner/game"
            github.latest.return_value = (run, {platform: {"id": index, "name": platform}
                                                for index, platform in enumerate(ARTIFACTS)})
            github.download.side_effect = lambda artifact, destination: shutil.copyfile(artifacts[artifact["name"]], destination)
            self.assertEqual(main(["--repo", "owner/game", "--assets", str(self.asset_pack), "--output-dir", str(output)]), 0)
        self.assertEqual(github.download.call_count, 2)
        self.assertEqual(len(list(output.glob("*-town-aaaaaaaaaaaa.zip"))), 2)
        self.assertEqual(len(list(output.iterdir())), 2)

    def test_cli_failure_on_second_platform_keeps_previous_archives(self):
        windows = self.artifact("windows")
        self.configs["inventory.toml"] = b"different Linux configuration"
        linux = self.artifact("linux")
        artifacts = {"windows": windows, "linux": linux}
        output = self.root / "distribution"
        output.mkdir()
        for name in ARTIFACTS.values():
            (output / (name + "-town-aaaaaaaaaaaa.zip")).write_bytes(b"previous package")
        run = {"id": 42, "head_sha": "a" * 40, "html_url": "https://github.com/owner/game/actions/runs/42"}
        with patch("tools.package_github_clients.GitHubClientBuilds") as constructor, \
             contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()) as errors:
            github = constructor.return_value
            github.repository = "owner/game"
            github.latest.return_value = (run, {platform: {"id": index, "name": platform}
                                                for index, platform in enumerate(ARTIFACTS)})
            github.download.side_effect = lambda artifact, destination: shutil.copyfile(artifacts[artifact["name"]], destination)
            self.assertEqual(main(["--repo", "owner/game", "--assets", str(self.asset_pack), "--output-dir", str(output)]), 1)
        self.assertIn("inventory.toml", errors.getvalue())
        self.assertEqual(len(list(output.iterdir())), 2)
        self.assertTrue(all(path.read_bytes() == b"previous package" for path in output.iterdir()))

    def test_cli_fresh_assets_use_preparation_options_and_check_local_config_first(self):
        artifact = self.artifact("windows")
        for name, data in self.configs.items():
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        run = {"id": 42, "head_sha": "a" * 40, "html_url": "https://github.com/owner/game/actions/runs/42"}
        for dirty, dog_options in ((False, []), (False, ["--no-dogs"]),
                                   (False, ["--dogs", "assets/custom/models"]), (True, [])):
            with self.subTest(dirty=dirty, dog_options=dog_options):
                if dirty:
                    (self.root / "inventory.toml").write_bytes(b"unpublished inventory")
                with patch("tools.package_github_clients.ROOT", self.root), \
                     patch("tools.package_github_clients.GitHubClientBuilds") as constructor, \
                     patch("tools.package_github_clients.package_assets") as prepare, \
                     contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    github = constructor.return_value
                    github.repository = "owner/game"
                    github.latest.return_value = (run, {"windows": {"id": 7, "name": "windows"}})
                    github.download.side_effect = lambda _, destination: shutil.copyfile(artifact, destination)
                    prepare.side_effect = lambda root, map_path, destination, zombies, **options: shutil.copyfile(self.asset_pack, destination)
                    result = main(["--repo", "owner/game", "--platform", "windows", "--no-zombies", "--no-trees",
                                   "--vegetation", "none", *dog_options])
                    self.assertEqual(result, 1 if dirty else 0)
                    if dirty:
                        prepare.assert_not_called()
                    else:
                        self.assertEqual(prepare.call_args.args[1], self.root / "output/godollo.glb")
                        self.assertIsNone(prepare.call_args.args[3])
                        expected_dogs = (None if dog_options == ["--no-dogs"] else
                                         Path("assets/custom/models") if dog_options else
                                         self.root / "assets/undead_dog/models")
                        self.assertEqual(prepare.call_args.kwargs, {"vegetation": "none", "trees": False, "forests": True,
                                                                  "dogs": expected_dogs})

    def test_prebuilt_asset_pack_rejects_dog_preparation_options(self):
        for options in (["--no-dogs"], ["--dogs", "custom/models"]):
            with self.subTest(options=options), contextlib.redirect_stderr(io.StringIO()), \
                 self.assertRaises(SystemExit) as error:
                main(["--assets", str(self.asset_pack), *options])
            self.assertEqual(error.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
