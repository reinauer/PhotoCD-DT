#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


SECTOR_SIZE = 2352
USER_DATA_OFFSET = 24
USER_DATA_SIZE = 2048


def parse_cue(cue_path: Path) -> Path:
    for line in cue_path.read_text().splitlines():
        line = line.strip()
        if line.upper().startswith("FILE "):
            parts = line.split('"')
            if len(parts) >= 3:
                return (cue_path.parent / parts[1]).resolve()
    raise ValueError(f"could not find FILE entry in {cue_path}")


def convert_raw_mode2_to_iso(bin_path: Path, iso_path: Path) -> None:
    if iso_path.exists() and iso_path.stat().st_mtime >= bin_path.stat().st_mtime:
        return

    iso_path.parent.mkdir(parents=True, exist_ok=True)
    with bin_path.open("rb") as src, iso_path.open("wb") as dst:
        while True:
            sector = src.read(SECTOR_SIZE)
            if len(sector) < SECTOR_SIZE:
                break
            dst.write(sector[USER_DATA_OFFSET:USER_DATA_OFFSET + USER_DATA_SIZE])


def extract_members(iso_path: Path, output_dir: Path, members: list[str]) -> None:
    if shutil.which("bsdtar") is None:
        raise RuntimeError("bsdtar is required to extract sample files")

    output_dir.mkdir(parents=True, exist_ok=True)
    cmd = ["bsdtar", "-xf", str(iso_path), "-C", str(output_dir), *members]
    subprocess.run(cmd, check=True)


def default_members() -> list[str]:
    return [
        "PHOTO_CD/IMAGES/IMG0001.PCD",
        "PHOTO_CD/IMAGES/IMG0002.PCD",
    ]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract representative Photo CD samples from a raw BIN/CUE image."
    )
    parser.add_argument("--cue", required=True, type=Path, help="path to the .cue file")
    parser.add_argument(
        "--iso",
        type=Path,
        help="optional path for the reconstructed ISO image "
        "(default: build/fixtures/<cue-stem>.iso)",
    )
    parser.add_argument("--out-dir", required=True, type=Path, help="directory to extract into")
    parser.add_argument("members", nargs="*", help="specific members to extract")
    args = parser.parse_args()

    cue_path = args.cue.resolve()
    bin_path = parse_cue(cue_path)
    iso_path = args.iso or Path("build/fixtures") / f"{cue_path.stem}.iso"
    members = args.members or default_members()

    convert_raw_mode2_to_iso(bin_path, iso_path)
    extract_members(iso_path, args.out_dir.resolve(), members)

    for member in members:
        print(member)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
