#!/usr/bin/env python3
"""Bundle a relocatable CPython and PyArrow next to the staged match exporter."""

import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import tempfile
from urllib.request import urlopen
import zipfile


PYTHON_RELEASE = "20260901"
PYTHON_VERSION = "3.12.14"
ARROW_VERSION = "26.0.1"
RUNTIMES = {
    "mac-arm64": (
        "aarch64-apple-darwin",
        "81a359f1cfadd4da11766534c5913791cea55f26e1bb902cacd2a531bb1e4b2b",
        "macosx_12_0_arm64",
        "61/81/dc5814d61f8c956363e81bcaba61b4712a5d227324d40c766b3858160711",
        "217497a51fd41abb74502ba2f4900872e0944c226945c02ee8a1cb4f81df3392",
    ),
    "mac-x64": (
        "x86_64-apple-darwin",
        "65b195c9cedc1fef6767f044f9822069adbd1bd9204d424ece4628776fdc04bb",
        "macosx_12_0_x86_64",
        "f7/c8/dcc42b1cceb69ff032fa16430bd82c086b933e35ef05ab521e56313b76be",
        "a2c5e9cec45b318917b630dc040cf197aef624d94866ab2c8e709708057123d2",
    ),
    "windows-x64": (
        "x86_64-pc-windows-msvc",
        "7c45c9622400d578709a9b2cddbe8124cc21d382409d9f13406d706d28e31b14",
        "win_amd64",
        "30/61/d94decccacb4dd40a4ebeed0ff3b2cea8f0b680f50184ea7eb0264db779b",
        "9379f0d1056cad6866c4697aec83a15830202ec7e0bd52f8bd508b316260fa61",
    ),
    "linux-x64": (
        "x86_64-unknown-linux-gnu",
        "72748da13197c1fb161e3afeef20a6a385ff24f2165e6e2758e47008e7faba4c",
        "manylinux_2_28_x86_64",
        "d3/79/e2fc9b1317b7a21e7bcd57eb035cac3fe72f47af3efe2ec3b4dea9a08a04",
        "c718bf97a92a6dea97fd1062c3b9f9e2e83cb9cc926e752289fa4ffff41738f9",
    ),
}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(cache, filename, url, digest):
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / filename
    if archive.is_file() and sha256(archive) == digest:
        return archive
    with tempfile.NamedTemporaryFile(dir=cache, delete=False) as file:
        temporary = Path(file.name)
        try:
            with urlopen(url, timeout=90) as response:
                shutil.copyfileobj(response, file)
            file.close()
            if sha256(temporary) != digest:
                raise ValueError(f"Checksum mismatch for {filename}")
            os.replace(temporary, archive)
        finally:
            file.close()
            temporary.unlink(missing_ok=True)
    return archive


def relative_path(name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
        raise ValueError(f"Unsafe archive member: {name}")
    return path


def install_runtime(destination, target, cache):
    triple, python_hash, wheel_tag, wheel_prefix, wheel_hash = RUNTIMES[target]
    python_name = f"cpython-{PYTHON_VERSION}+{PYTHON_RELEASE}-{triple}-install_only_stripped.tar.gz"
    python_url = f"https://github.com/astral-sh/python-build-standalone/releases/download/{PYTHON_RELEASE}/"
    wheel_name = f"pyarrow-{ARROW_VERSION}-cp312-cp312-{wheel_tag}.whl"
    python = download(cache, python_name, python_url + python_name.replace("+", "%2B"), python_hash)
    wheel = download(cache, wheel_name, f"https://files.pythonhosted.org/packages/{wheel_prefix}/{wheel_name}", wheel_hash)
    windows = target == "windows-x64"
    executable = Path("python.exe" if windows else "bin/python3.12")
    site_packages = Path("Lib/site-packages" if windows else "lib/python3.12/site-packages")

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as work:
        staged = Path(work) / target
        with tarfile.open(python) as archive:
            for member in archive:
                if not member.isfile():
                    continue  # The versioned interpreter and its runtime libraries are regular files.
                path = relative_path(member.name).relative_to("python")
                if path.parts[0] in ("share", "include", "Scripts"):
                    continue
                output = staged / path
                output.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(member) as source, output.open("wb") as file:
                    shutil.copyfileobj(source, file)
                output.chmod(member.mode & 0o777)
        with zipfile.ZipFile(wheel) as archive:
            for member in archive.infolist():
                if member.is_dir():
                    continue
                path = relative_path(member.filename)
                output = staged / site_packages / path
                output.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(member) as source, output.open("wb") as file:
                    shutil.copyfileobj(source, file)
        if not (staged / executable).is_file() or not (staged / site_packages / "pyarrow/parquet").is_dir():
            raise ValueError(f"Incomplete match statistics runtime for {target}")
        if destination.exists():
            shutil.rmtree(destination)
        staged.rename(destination)
    print(f"Match statistics runtime staged for {target}: {destination}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True,
                        help="Staged game/archive directory, or source root for editor runs")
    parser.add_argument("--target", choices=RUNTIMES, action="append", required=True)
    parser.add_argument("--cache", type=Path,
                        default=Path(__file__).resolve().parents[1] / "Saved/MatchStatsDownloads")
    args = parser.parse_args()
    # In a Mac app the NonUFS path lives below Contents/UE/RespawnAndRage.
    direct = args.archive / "scripts/export-match-stats.py"
    exporters = [direct] if direct.is_file() else list(args.archive.rglob("scripts/export-match-stats.py"))
    if len(exporters) != 1:
        parser.error(f"Expected one staged scripts/export-match-stats.py in {args.archive}")
    for target in args.target:
        install_runtime(exporters[0].parent / "MatchStatsPython" / target, target, args.cache)


if __name__ == "__main__":
    main()
