"""Publish a validated map and its generated companions with rollback on error."""
from __future__ import annotations

from pathlib import Path
import shutil
import tempfile


def publish_files(pending: dict[Path, Path]) -> None:
    """Replace staged files, restoring previous outputs if replacement fails.

    Callers finish geometry and companion validation before entering here.
    Staging and backups share the output filesystem so each replace is atomic;
    the set of replacements is not a crash-safe filesystem transaction.
    """
    directory = next(iter(pending)).parent
    scratch = Path(tempfile.mkdtemp(prefix=".map-backup-", dir=directory))
    retain_backups = False
    try:
        backups = {}
        for path in pending:
            backup = scratch / path.name if path.exists() else None
            if backup is not None:
                shutil.copy2(path, backup)
            backups[path] = backup
        published = []
        try:
            for path, staged in pending.items():
                staged.replace(path)
                published.append(path)
        except Exception as error:
            for path in reversed(published):
                backup = backups[path]
                try:
                    if backup is None:
                        path.unlink(missing_ok=True)
                    else:
                        backup.replace(path)
                except OSError:
                    retain_backups = True
            if retain_backups:
                raise RuntimeError(f"Map publication failed and rollback was incomplete; previous files retained in {scratch}") from error
            raise
    finally:
        if not retain_backups:
            shutil.rmtree(scratch)
