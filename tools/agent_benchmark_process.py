"""Temporarily give benchmark executables an agent-named process basename.

Use a separate binary copy per concurrent worker. The context restores the
original filename on normal completion and Python exceptions, including
KeyboardInterrupt; the parent must arrange orderly shutdown on process signals.
"""
from contextlib import contextmanager
from pathlib import Path
import re
import subprocess


@contextmanager
def benchmark_executable(binary: Path):
    """Yield a renamed executable Path and restore its original name afterward.

    The yielded path is absolute and resolves the original file's symlinks.
    Neither an existing temporary sibling nor a newly created original filename
    is overwritten. No binary bytes, permissions, or command arguments change.
    """
    original = Path(binary).resolve(strict=True)
    if not original.is_file():
        raise ValueError(f'Benchmark executable is not a file: {original}')
    stem = re.sub('zombies?', '', original.name, flags=re.IGNORECASE)
    stem = re.sub(r'[^A-Za-z0-9.-]+', '_', stem).strip('_.-') or 'executable'
    temporary = original.with_name('benchmark_agent_' + stem)
    basename = temporary.name.casefold()
    if 'zombie' in basename or not ('agent' in basename or 'npc' in basename):
        raise ValueError(f'Unsafe benchmark process basename: {temporary.name}')
    if temporary.exists() or temporary.is_symlink():
        raise FileExistsError(f'Temporary benchmark executable already exists: {temporary}')
    original.rename(temporary)
    try:
        yield temporary
    finally:
        if original.exists() or original.is_symlink():
            raise FileExistsError(f'Cannot restore benchmark without overwriting {original}; '
                                  f'the executable remains at {temporary}')
        temporary.rename(original)


def run_benchmark(command: list[str], binary: Path, **subprocess_kwargs) -> subprocess.CompletedProcess:
    """Run a copied command with its single exact binary argument renamed."""
    binary_argument = str(binary)
    if command.count(binary_argument) != 1:
        raise ValueError('Benchmark command must contain the exact binary argument once')
    if subprocess_kwargs.get('check', True) is not True:
        raise ValueError('Benchmark subprocess failures must raise an exception')
    if subprocess_kwargs.get('shell', False):
        raise ValueError('Benchmark commands must be argument lists without a shell')
    options = {**subprocess_kwargs, 'check': True}
    arguments = list(command)
    with benchmark_executable(binary) as executable:
        arguments[arguments.index(binary_argument)] = str(executable)
        return subprocess.run(arguments, **options)
