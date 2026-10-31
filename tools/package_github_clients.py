#!/usr/bin/env python3
"""Download the latest successful GitHub clients and ZIP them with prepared town assets."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
from urllib.error import URLError
import zipfile

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from src.client_bundle import bundle_client, check_local_configuration, unpack_client
from src.github_client_builds import ARTIFACTS, GitHubClientBuilds, repository_from_origin
from src.vegetation_selection import VEGETATION_PRESETS
from tools.package_multiplayer_assets import package_assets


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", help="OWNER/REPO; defaults to this checkout's origin")
    parser.add_argument("--branch", help="Defaults to the GitHub repository's default branch")
    parser.add_argument("--platform", choices=("both", *ARTIFACTS), default="both")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/distribution")
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--assets", type=Path, help="Reuse an existing asset ZIP instead of preparing a fresh one")
    source.add_argument("--map", type=Path, default=None, help="Prepared map; defaults to output/godollo.glb")
    parser.add_argument("--zombies", type=Path, default=None)
    parser.add_argument("--vegetation", choices=VEGETATION_PRESETS, default=None)
    parser.add_argument("--no-zombies", action="store_true")
    parser.add_argument("--no-trees", action="store_true")
    parser.add_argument("--no-forests", action="store_true")
    args = parser.parse_args(argv)
    if args.assets and (args.zombies or args.vegetation or args.no_zombies or args.no_trees or args.no_forests):
        parser.error("--assets already fixes the map and layer settings; omit preparation options")
    try:
        github = GitHubClientBuilds(args.repo or repository_from_origin(ROOT),
                                   os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN"))
        platforms = tuple(ARTIFACTS) if args.platform == "both" else (args.platform,)
        run, artifacts = github.latest(platforms, args.branch)
        print(f"Build {run['id']} at {run['head_sha']}: {run['html_url']}", flush=True)
        args.output_dir.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="github-clients-", dir=args.output_dir) as temporary:
            scratch = Path(temporary)
            clients = {}
            for platform, artifact in artifacts.items():
                print(f"Downloading {artifact['name']}…", flush=True)
                download = scratch / (platform + ".zip")
                github.download(artifact, download)
                clients[platform] = unpack_client(download, platform, scratch / platform)
                if args.assets is None:
                    check_local_configuration(clients[platform], ROOT)
            asset_pack = args.assets
            if asset_pack is None:
                print("Packaging prepared local assets…", flush=True)
                asset_pack = scratch / "assets.zip"
                package_assets(ROOT, args.map or ROOT / "output/godollo.glb", asset_pack,
                               None if args.no_zombies else args.zombies or ROOT / "assets/zombies/models",
                               vegetation=args.vegetation or "osm-tuned2",
                               trees=not args.no_trees, forests=not args.no_forests)
            staged = []
            for platform, client in clients.items():
                name = f"{ARTIFACTS[platform]}-town-{run['head_sha'][:12]}.zip"
                provenance = {"repository": github.repository, "run_id": run["id"],
                              "commit": run["head_sha"], "url": run["html_url"],
                              "platform": platform, "artifact_id": artifacts[platform]["id"]}
                print(f"Combining {platform} client and assets…", flush=True)
                staged.append(bundle_client(client, asset_pack, scratch / name, provenance))
            # Finish both platforms before publishing either completed archive.
            for path in staged:
                output = args.output_dir / path.name
                os.replace(path, output)
                print(f"Saved {output.resolve()}", flush=True)
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError,
            URLError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(f"Client packaging failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
