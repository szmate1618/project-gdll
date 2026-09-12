"""Read detector CSVs and merge them without discarding detections or metadata."""

from __future__ import annotations

import csv
import glob
import math
import os
from pathlib import Path
import tempfile
from typing import Iterable, Iterator


REQUIRED_COLUMNS = ("xmin", "ymin", "xmax", "ymax", "world_x", "world_y", "image_path")
PROVENANCE_COLUMNS = ("source_csv", "source_row")
ALIASES = {"x_min": "xmin", "y_min": "ymin", "x_max": "xmax", "y_max": "ymax"}
NUMERIC_COLUMNS = ("xmin", "ymin", "xmax", "ymax", "world_x", "world_y", "pixel_x", "pixel_y", "score")


def _header(reader: csv.reader, path: Path) -> list[str]:
    try:
        original = next(reader)
    except StopIteration:
        raise ValueError(f"{path}: CSV is empty; expected a header.") from None
    names = [ALIASES.get(name.strip(), name.strip()) for name in original]
    if any(not name for name in names):
        raise ValueError(f"{path}: CSV header contains an unnamed column.")
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        raise ValueError(f"{path}: duplicate columns after normalizing aliases: {', '.join(duplicates)}.")
    missing = [name for name in REQUIRED_COLUMNS if name not in names]
    if missing:
        raise ValueError(f"{path}: missing required CSV columns: {', '.join(missing)}.")
    return names


def _fieldnames(path: Path) -> list[str]:
    try:
        with path.open("r", encoding="utf-8-sig", newline="") as stream:
            return _header(csv.reader(stream, strict=True), path)
    except (OSError, UnicodeError, csv.Error) as error:
        raise ValueError(f"Cannot read CSV header {path}: {error}") from error


def _normalize(row: dict[str, str], path: Path, line: int) -> dict[str, str]:
    context = f"{path}:{line}"
    numbers = {}
    for field in NUMERIC_COLUMNS:
        value = row.get(field, "").strip()
        if not value and field not in REQUIRED_COLUMNS:
            continue
        try:
            number = float(value)
        except ValueError:
            raise ValueError(f"{context}: {field} must be a finite number; received {value!r}.") from None
        if not math.isfinite(number):
            raise ValueError(f"{context}: {field} must be finite; received {value!r}.")
        numbers[field] = number
        row[field] = value
    if numbers["xmax"] <= numbers["xmin"] or numbers["ymax"] <= numbers["ymin"]:
        raise ValueError(f"{context}: detection bbox must have positive width and height (xmax > xmin, ymax > ymin).")
    image = row["image_path"].strip()
    if not image:
        raise ValueError(f"{context}: image_path must not be empty.")

    # A merged CSV remains traceable to its original detector output. Blank
    # provenance cells in a heterogeneous union simply denote a new input row.
    provenance = row.get("source_csv", "").strip()
    provenance_row = row.get("source_row", "").strip()
    if bool(provenance) != bool(provenance_row):
        raise ValueError(f"{context}: source_csv and source_row must be supplied together.")
    if provenance:
        origin = Path(provenance).expanduser()
        if not origin.is_absolute():
            origin = path.parent / origin
        origin = origin.resolve()
        try:
            original_line = int(provenance_row)
        except ValueError:
            raise ValueError(f"{context}: source_row must be a positive integer.") from None
        if original_line < 1:
            raise ValueError(f"{context}: source_row must be a positive integer.")
    else:
        origin, original_line = path, line
    row["source_csv"] = str(origin)
    row["source_row"] = str(original_line)

    candidate = Path(image).expanduser()
    if not candidate.is_absolute():
        candidate = origin.parent / candidate
    # Preserve unresolved detector paths (including paths from another OS).
    # The source_csv field records the directory against which they were meant.
    row["image_path"] = str(candidate.resolve()) if candidate.is_file() else image
    return row


def iter_detections(path: Path) -> Iterator[dict[str, str]]:
    """Yield validated detector rows, retaining extra fields and original provenance.

    ``x_min/y_min/x_max/y_max`` headers normalize to ``xmin/ymin/xmax/ymax``.
    Required coordinates must be finite and the pixel bbox must have positive
    dimensions. Optional nonempty pixel coordinates and scores are also checked.
    Existing images resolve relative to their source CSV; missing image paths
    remain unchanged. ``source_row`` is a physical record-start line, normally
    2 for the first detection. All yielded values are strings.
    """
    path = Path(path).expanduser().resolve()
    reader = None
    try:
        with path.open("r", encoding="utf-8-sig", newline="") as stream:
            reader = csv.reader(stream, strict=True)
            fields = _header(reader, path)
            while True:
                line = reader.line_num + 1
                try:
                    values = next(reader)
                except StopIteration:
                    break
                if not values:
                    continue
                if len(values) != len(fields):
                    raise ValueError(f"{path}:{line}: expected {len(fields)} CSV fields, received {len(values)}.")
                yield _normalize(dict(zip(fields, values)), path, line)
    except (OSError, UnicodeError, csv.Error) as error:
        line = reader.line_num if reader is not None else 0
        raise ValueError(f"Cannot read detection CSV {path}:{line}: {error}") from error


def _input_files(inputs: Iterable[str | Path], output: Path) -> list[Path]:
    if isinstance(inputs, (str, Path)):
        inputs = [inputs]
    found = set()
    for item in inputs:
        expression = os.path.expanduser(os.fspath(item))
        literal = Path(expression)
        if literal.exists():
            matches = [literal]
        else:
            matches = [Path(match) for match in glob.glob(expression, recursive=True)]
        if not matches:
            raise ValueError(f"No input files match {expression!r}.")
        for match in matches:
            if match.is_dir():
                candidates = sorted(path for path in match.rglob("*") if path.is_file() and path.suffix.lower() == ".csv")
            elif match.is_file():
                candidates = [match]
            else:
                continue
            for candidate in candidates:
                resolved = candidate.resolve()
                if resolved != output:
                    found.add(resolved)
    paths = sorted(found, key=str)
    if not paths:
        raise ValueError("No input CSV files remain after excluding the output path.")
    return paths


def merge_detections(inputs: Iterable[str | Path], output: Path) -> dict:
    """Merge paths/globs/directories atomically; deduplicate files, never rows.

    Directories are searched recursively for CSV files. Input paths are sorted,
    repeated paths are read once, and the destination is always excluded. Rows
    keep their order within each source. Extra columns form a sorted union.
    The output is replaced only after every input row passes validation.

    Return ``output: Path``, ``inputs: list[Path]``, ``rows: int``, and
    ``columns: list[str]``. Invalid or unavailable inputs raise ``ValueError``.
    """
    output = Path(output).expanduser().resolve()
    paths = _input_files(inputs, output)
    extra = set()
    for path in paths:
        extra.update(_fieldnames(path))
    extra.difference_update(REQUIRED_COLUMNS + PROVENANCE_COLUMNS)
    columns = list(REQUIRED_COLUMNS) + sorted(extra) + list(PROVENANCE_COLUMNS)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    count = 0
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", newline="", prefix=f".{output.name}.",
            suffix=".part", dir=output.parent, delete=False,
        ) as stream:
            temporary = Path(stream.name)
            writer = csv.DictWriter(stream, fieldnames=columns, lineterminator="\n")
            writer.writeheader()
            for path in paths:
                for row in iter_detections(path):
                    writer.writerow(row)
                    count += 1
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return {"output": output, "inputs": paths, "rows": count, "columns": columns}
