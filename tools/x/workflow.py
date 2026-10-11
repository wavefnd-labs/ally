"""Development orchestration; all compilation and packaging belongs to CMake."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
SKIA_REV = "03c4671cffded59a3bb00e18bfae3c2763424fc6"


def run(args, cwd=ROOT):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def deps():
    base = ROOT / "third_party"
    base.mkdir(exist_ok=True)
    for repo, tag, name, digest in [
        (
            "libsdl-org/SDL",
            "release-3.2.24",
            "SDL-release-3.2.24",
            "66c374ee7f786abcd8592d18fc5b18b1e3f1c67e923867eee5f9c503cb253afd",
        ),
        (
            "chirlu/soxr",
            "0.1.3",
            "soxr-0.1.3",
            "db6ca1b1e8405c6ef92f8294fc123d910abf0a114003b3f0f13fa57a95fd62d0",
        ),
        (
            "celtera/libremidi",
            "v4.5.0",
            "libremidi-4.5.0",
            "2e884a4c826dd87157ee4fab8cd8c7b9dbbc1ddb804cb10ef0852094200724db",
        ),
    ]:
        if not (base / name / "CMakeLists.txt").exists():
            archive = base / (name + ".tar.gz")
            urllib.request.urlretrieve(
                f"https://github.com/{repo}/archive/refs/tags/{tag}.tar.gz", archive
            )
            if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
                raise RuntimeError(f"Dependency checksum mismatch: {archive}")
            with tarfile.open(archive) as t:
                t.extractall(base, filter="data")
            archive.unlink()
    skia = base / "skia"
    if not (skia / "include/core/SkCanvas.h").exists():
        run(["git", "init", skia])
        run(["git", "remote", "add", "origin", "https://skia.googlesource.com/skia.git"], cwd=skia)
        run(["git", "fetch", "--depth", "1", "origin", SKIA_REV], cwd=skia)
        run(["git", "checkout", "--detach", "FETCH_HEAD"], cwd=skia)
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=skia, text=True).strip()
    if actual != SKIA_REV:
        raise RuntimeError(f"Skia revision mismatch: {actual}; expected {SKIA_REV}")
    if not (skia / "bin" / ("gn.exe" if os.name == "nt" else "gn")).exists():
        run([sys.executable, skia / "bin/fetch-gn"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "command", choices=["doctor", "deps", "build", "run", "test", "fmt", "clean", "package"]
    )
    parser.add_argument("--release", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--preset", choices=["debug", "release", "asan"])
    options, extra = parser.parse_known_args()
    if extra and extra[0] == "--":
        extra = extra[1:]
    if extra and options.command != "run":
        parser.error("Additional arguments are supported only for run")
    preset = options.preset or ("release" if options.release else "debug")
    folder = ROOT / "build" / preset
    try:
        if options.command == "doctor":
            missing = False
            print(f"Python: {sys.version.split()[0]} | platform: {sys.platform}")
            for tool in ["cmake", "ninja", "clang", "clang++", "clang-format", "git"]:
                path = shutil.which(tool)
                print(f"{tool}: {path or 'MISSING'}")
                missing |= path is None
            print(
                "Skia:",
                "source ready"
                if (ROOT / "third_party/skia/include/core/SkCanvas.h").exists()
                else "run x.py deps",
            )
            if sys.platform.startswith("linux"):
                run(["pkg-config", "--modversion", "alsa", "fontconfig", "freetype2"])
            return int(missing)
        if options.command == "deps":
            deps()
        elif options.command == "fmt":
            files = sorted(
                p
                for base in ["src", "include", "libs", "tests"]
                for p in (ROOT / base).rglob("*")
                if p.suffix in {".cpp", ".hpp", ".h", ".c"} and "build" not in p.parts
            )
            run(["clang-format", *(["--dry-run", "--Werror"] if options.check else ["-i"]), *files])
        elif options.command == "clean":
            if folder.parent != ROOT / "build":
                raise RuntimeError("Refusing unexpected clean path")
            if folder.exists():
                shutil.rmtree(folder)
        else:
            if preset != "asan":
                deps()
            run(["cmake", "--preset", preset])
            run(["cmake", "--build", "--preset", preset, "--parallel", "8"])
            if options.command == "test":
                run(["ctest", "--preset", preset])
            elif options.command == "run":
                if preset == "asan":
                    raise RuntimeError("asan preset builds core tests only")
                run([folder / ("ally.exe" if os.name == "nt" else "ally"), *extra])
            elif options.command == "package":
                run(["ctest", "--preset", preset])
                (ROOT / "dist").mkdir(exist_ok=True)
                run(["cpack", "--config", folder / "CPackConfig.cmake", "-B", ROOT / "dist"])
        return 0
    except (subprocess.CalledProcessError, OSError, RuntimeError) as error:
        print(f"x.py: {error}", file=sys.stderr)
        return 1
