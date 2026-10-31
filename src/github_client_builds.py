"""Read and download the packaged clients from one GitHub Actions build."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import HTTPRedirectHandler, Request, build_opener, urlopen


ARTIFACTS = {
    "windows": "godollo-client-windows-x86_64",
    "linux": "godollo-client-linux-x86_64",
}


def repository_from_origin(root):
    result = subprocess.run(["git", "-C", str(root), "remote", "get-url", "origin"],
                            check=True, capture_output=True, text=True)
    match = re.fullmatch(r"(?:https://github\.com/|git@github\.com:)([^/]+/[^/]+?)(?:\.git)?", result.stdout.strip())
    if not match:
        raise ValueError("Cannot infer a GitHub repository from origin; use --repo OWNER/REPO")
    return match[1]


class _ArtifactRedirect(HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        # Artifact storage uses a signed URL; never forward the GitHub token.
        return None


class GitHubClientBuilds:
    def __init__(self, repository, token):
        if not re.fullmatch(r"[\w.-]+/[\w.-]+", repository):
            raise ValueError("Repository must be OWNER/REPO")
        if not token:
            raise ValueError("Set GH_TOKEN or GITHUB_TOKEN to a token with Actions read access")
        self.repository = repository
        self.token = token

    def request(self, endpoint):
        url = f"https://api.github.com/repos/{self.repository}" + ("/" + endpoint if endpoint else "")
        return Request(url, headers={
            "Accept": "application/vnd.github+json", "Authorization": f"Bearer {self.token}",
            "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "godollo-client-packager",
        })

    def get(self, endpoint):
        try:
            with urlopen(self.request(endpoint), timeout=60) as response:
                return json.load(response)
        except HTTPError as error:
            raise ValueError(f"GitHub API returned HTTP {error.code}; check repository access and Actions read permission") from error

    def pages(self, endpoint, key, **query):
        page = 1
        while True:
            data = self.get(endpoint + "?" + urlencode({**query, "per_page": 100, "page": page}))[key]
            yield from data
            if len(data) < 100:
                return
            page += 1

    def latest(self, platforms, branch=None):
        if branch is None:
            branch = self.get("")["default_branch"]
        wanted = {ARTIFACTS[platform] for platform in platforms}
        for run in self.pages("actions/workflows/build-apps.yml/runs", "workflow_runs",
                              branch=branch, status="success"):
            if run["event"] not in ("push", "workflow_dispatch"):
                continue
            artifacts = {item["name"]: item for item in self.pages(
                f"actions/runs/{run['id']}/artifacts", "artifacts") if not item["expired"]}
            if wanted <= artifacts.keys():
                return run, {platform: artifacts[ARTIFACTS[platform]] for platform in platforms}
        raise ValueError(f"No successful Build apps run on {branch!r} has all requested unexpired client artifacts")

    def download(self, artifact, destination):
        request = self.request(f"actions/artifacts/{artifact['id']}/zip")
        try:
            response = build_opener(_ArtifactRedirect()).open(request, timeout=60)
        except HTTPError as error:
            if error.code != 302:
                raise ValueError(f"Artifact download returned HTTP {error.code}; check token permissions or artifact expiry") from error
            location = error.headers.get("Location", "")
            if not location.startswith("https://"):
                raise ValueError("GitHub returned an invalid artifact download URL") from error
            response = urlopen(location, timeout=60)
        destination = Path(destination)
        with response, destination.open("wb") as target:
            shutil.copyfileobj(response, target, length=1024 * 1024)
        digest = artifact.get("digest")
        if digest:
            with destination.open("rb") as stream:
                actual = "sha256:" + hashlib.file_digest(stream, "sha256").hexdigest()
            if actual != digest:
                raise ValueError(f"Downloaded artifact checksum mismatch: {artifact['name']}")
