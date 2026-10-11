#!/usr/bin/env python3
"""Supervise a real-time native recording soak and retain auditable evidence."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def write_status(path, state):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, required=True)
    parser.add_argument("--tracks", type=int, choices=(3, 32), required=True)
    parser.add_argument("--device", required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--binary", type=Path, default=Path("build/release/stability_tests"))
    args = parser.parse_args()
    if not 1 <= args.seconds <= 21600:
        parser.error("--seconds must be 1..21600")
    directory = args.directory.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    binary = args.binary.resolve(strict=True)
    status_path = directory / "status.json"
    log_path = directory / "native.log"
    state = {
        "state": "starting",
        "requested_seconds": args.seconds,
        "tracks": args.tracks,
        "device": args.device,
        "directory": str(directory),
        "supervisor_pid": os.getpid(),
        "started_utc": utc_now(),
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
        "peak_rss_kib": 0,
    }
    # Snapshot the exact tested sources; a Git HEAD alone cannot identify uncommitted fixes.
    diff = subprocess.check_output(["git", "diff", "--binary"])
    (directory / "tested-source.patch").write_bytes(diff)
    state["source_patch_sha256"] = hashlib.sha256(diff).hexdigest()
    untracked = subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard", "-z"])
    state["untracked_sources"] = {}
    for name in untracked.decode().split("\0"):
        if not name:
            continue
        source = Path(name)
        if source.is_file() and not source.is_symlink():
            content = source.read_bytes()
            snapshot = directory / "untracked-source" / name
            snapshot.parent.mkdir(parents=True, exist_ok=True)
            snapshot.write_bytes(content)
            state["untracked_sources"][name] = hashlib.sha256(content).hexdigest()
    write_status(status_path, state)
    command = [str(binary), "--native", str(args.seconds), args.device, str(directory), str(args.tracks)]
    process = None
    started = time.monotonic()
    try:
        with log_path.open("wb") as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            state.update(state="running", test_pid=process.pid, command=command)
            write_status(status_path, state)
            while process.poll() is None:
                contents = log_path.read_text(encoding="utf-8", errors="replace")
                progress = [line for line in contents.splitlines() if line.startswith("Native soak progress:")]
                if progress:
                    state["last_progress"] = progress[-1]
                    state["progress"] = {
                        key: float(value) if "." in value or "e" in value.lower() else int(value)
                        for key, value in re.findall(r"([a-z_]+)=([0-9.eE+-]+)", progress[-1])
                    }
                fixture = re.search(r'fixture="([^"\n]+)"', contents)
                if fixture:
                    state["fixture"] = fixture.group(1)
                try:
                    proc_status = Path(f"/proc/{process.pid}/status").read_text()
                    rss = re.search(r"VmRSS:\s+(\d+)", proc_status)
                    if rss:
                        state["peak_rss_kib"] = max(state["peak_rss_kib"], int(rss.group(1)))
                except OSError:
                    pass
                state["updated_utc"] = utc_now()
                state["supervisor_elapsed_seconds"] = time.monotonic() - started
                state["disk_available_bytes"] = shutil.disk_usage(directory).free
                write_status(status_path, state)
                if state["disk_available_bytes"] < 1024**3:
                    raise RuntimeError("Stopped own test to preserve at least 1 GiB of disk headroom")
                # Includes source generation and full-file readback; hung tests must not run forever.
                if time.monotonic() - started > args.seconds + 3600:
                    raise RuntimeError("Native soak exceeded capture and readback timeout")
                time.sleep(5)
        contents = log_path.read_text(encoding="utf-8", errors="replace")
        state["exit_code"] = process.returncode
        state["state"] = "passed" if process.returncode == 0 and "Native soak passed:" in contents else "failed"
        if process.returncode == -15 and (directory / "stop-reason.txt").exists():
            state["state"] = "interrupted"
            state["stop_reason"] = (directory / "stop-reason.txt").read_text(encoding="utf-8")
        state["finished_utc"] = utc_now()
        state["supervisor_elapsed_seconds"] = time.monotonic() - started
        state["log_sha256"] = hashlib.sha256(log_path.read_bytes()).hexdigest()
        state["last_log_lines"] = contents.splitlines()[-4:]
        write_status(status_path, state)
        return 0 if state["state"] == "passed" else 1
    except BaseException as error:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        state.update(state="failed", error=str(error), finished_utc=utc_now())
        write_status(status_path, state)
        raise


if __name__ == "__main__":
    sys.exit(main())
