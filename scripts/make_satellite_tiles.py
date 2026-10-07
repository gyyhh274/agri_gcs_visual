#!/usr/bin/env python3
"""Build disk-only XYZ tiles from the documented Maxar RGB GeoTIFF.

Preparation only: pip install rasterio Pillow. Qt runtime needs neither.
Never fabricates imagery or treats re-sampled pixels as source resolution.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import tempfile

import numpy as np
from PIL import Image
import rasterio
from rasterio.enums import Resampling
from rasterio.transform import from_origin
from rasterio.vrt import WarpedVRT
from rasterio.warp import transform_bounds
from rasterio.windows import Window

BASE = "https://maxar-opendata.s3.us-west-2.amazonaws.com/events/New-Zealand-Flooding23/ard/60/213311212312/2022-12-11/10300100DE4D9300"
SOURCE_URL = BASE + "-visual.tif"
SOURCE_SIZE = 61216613
SOURCE_SHA256 = "1094eef0572e6324cdd3653d888c76c5d1e12ae8fc0577a612fd80588f14b248"
LICENSE = "https://creativecommons.org/licenses/by-nc/4.0/"
ORIGIN = math.pi * 6378137


def download(destination, resume_parts=None):
    if destination.exists():
        raise ValueError("Download target already exists; supply the existing file without --download")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="maxar-parts-", dir=destination.parent) as temporary:
        part_dir = resume_parts or Path(temporary)
        if resume_parts and not resume_parts.is_dir():
            raise ValueError("Resume directory does not exist")
        chunks = 16
        size = math.ceil(SOURCE_SIZE / chunks)

        def fetch(index):
            start, end = index * size, min((index + 1) * size, SOURCE_SIZE) - 1
            target = part_dir / str(index)
            for attempt in range(20):
                received = target.stat().st_size if target.exists() else 0
                if received == end - start + 1:
                    break
                if received > end - start + 1:
                    raise ValueError("Invalid saved download part")
                tail = Path(temporary) / f"tail-{index}"
                result = subprocess.run(["curl", "-sSL", "--fail", "--connect-timeout", "15",
                                         "--max-time", "60", "--range", f"{start+received}-{end}",
                                         "-o", str(tail), SOURCE_URL])
                # Preserve contiguous received bytes on curl timeouts; retry the remainder.
                if result.returncode not in (0, 28):
                    continue
                if not tail.exists() or tail.stat().st_size == 0:
                    continue
                if tail.stat().st_size > end - start + 1 - received:
                    raise ValueError("Server ignored byte range")
                if (target.stat().st_size if target.exists() else 0) != received:
                    raise ValueError("Download part changed concurrently; do not run two downloaders")
                with target.open("ab") as output, tail.open("rb") as stream:
                    shutil.copyfileobj(stream, output)
                tail.unlink()
            if target.stat().st_size != end - start + 1:
                raise ValueError("Server did not return the requested range")
            print(f"Downloaded part {index + 1}/{chunks}", flush=True)
            return target

        with ThreadPoolExecutor(max_workers=8) as workers:
            parts = list(workers.map(fetch, range(chunks)))
        complete = part_dir / "complete.tif"
        with complete.open("wb") as output:
            for part in parts:
                with part.open("rb") as stream:
                    shutil.copyfileobj(stream, output)
        with rasterio.open(complete) as source:
            if source.count != 3 or source.crs.to_epsg() != 32760:
                raise ValueError("Unexpected source format or projection")
        complete.rename(destination)


def build(source_path, output, minimum, maximum):
    if output.exists() and any(output.iterdir()):
        raise ValueError("Output must be empty; existing maps are never overwritten")
    output.mkdir(parents=True, exist_ok=True)
    checksum = hashlib.sha256()
    with source_path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
    digest = checksum.hexdigest()
    if digest != SOURCE_SHA256:
        raise ValueError("Source SHA-256 mismatch: incomplete, corrupted or different satellite scene")
    counts = {}
    with rasterio.open(source_path) as source:
        if source.count != 3 or source.dtypes != ("uint8",) * 3 or not source.crs:
            raise ValueError("Expected a georeferenced 8-bit RGB image")
        if source.crs.to_epsg() != 32760 or (source.width, source.height) != (17408, 17408):
            raise ValueError("This source-specific generator accepts only the documented Maxar scene")
        bounds = transform_bounds(source.crs, "EPSG:3857", *source.bounds, densify_pts=41)
        geographic = transform_bounds(source.crs, "EPSG:4326", *source.bounds, densify_pts=41)
        for zoom in range(minimum, maximum + 1):
            n = 1 << zoom
            span = 2 * ORIGIN / n
            left = max(0, math.floor((bounds[0] + ORIGIN) / span))
            right = min(n - 1, math.floor((bounds[2] + ORIGIN) / span))
            top = max(0, math.floor((ORIGIN - bounds[3]) / span))
            bottom = min(n - 1, math.floor((ORIGIN - bounds[1]) / span))
            transform = from_origin(-ORIGIN + left * span, ORIGIN - top * span, span / 256, span / 256)
            count = 0
            # Tile-aligned virtual raster: GDAL warps only the requested 256px window.
            with WarpedVRT(source, crs="EPSG:3857", transform=transform,
                           width=(right - left + 1) * 256, height=(bottom - top + 1) * 256,
                           add_alpha=True, resampling=Resampling.bilinear) as warped:
                for x in range(left, right + 1):
                    folder = output / str(zoom) / str(x)
                    folder.mkdir(parents=True, exist_ok=True)
                    for y in range(top, bottom + 1):
                        rgba = warped.read(window=Window((x-left)*256, (y-top)*256, 256, 256))
                        if not rgba[3].any():
                            continue
                        if np.all(rgba[3] == 255):
                            Image.fromarray(np.moveaxis(rgba[:3], 0, -1)).save(
                                folder / f"{y}.jpg", quality=90, subsampling=0)
                        else:
                            Image.fromarray(np.moveaxis(rgba, 0, -1)).save(folder / f"{y}.png")
                        count += 1
            counts[str(zoom)] = count
            print(f"Zoom {zoom}: {count} tiles", flush=True)
    west, south, east, north = geographic
    metadata = {
        "name": "奥克兰周边离线卫星影像（新西兰）",
        "type": "real satellite RGB imagery; NOT AI-generated",
        "scheme": "xyz", "projection": "EPSG:3857", "coordinate_system": "WGS84",
        "bounds": list(geographic), "center": [(west+east)/2, (south+north)/2],
        "minzoom": minimum, "maxzoom": maximum, "tile_size": 256,
        "tile_format": "JPEG quality 90 (full coverage); RGBA PNG (transparent edges)",
        "tile_count": sum(counts.values()), "tiles_per_zoom": counts,
        "source": SOURCE_URL, "source_metadata": BASE + ".json",
        "source_sha256": digest, "source_bytes": source_path.stat().st_size,
        "acquired": "2022-12-11T22:13:03Z", "platform": "WorldView-2",
        "source_gsd_m": 0.56, "visual_pixel_spacing_m": 0.30517578125,
        "processing": "Reprojected UTM EPSG:32760 to Web Mercator EPSG:3857; bilinear resampling; JPEG quality 90 and transparent-edge PNG XYZ tiles.",
        "attribution": "© Maxar · CC BY-NC 4.0 · 非商业 · 2022-12-11",
        "license": LICENSE,
        "warning": "Historical imagery for non-commercial demonstrations only. Not the actual flight site; not a flight-safety or obstacle survey."
    }
    (output / "metadata.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Local downloaded RGB GeoTIFF")
    parser.add_argument("output", type=Path, help="New empty XYZ output directory")
    parser.add_argument("--download", action="store_true", help="Download the documented 61 MB source first")
    parser.add_argument("--resume-parts", type=Path, help="Existing numbered partial downloads for this source")
    parser.add_argument("--minzoom", type=int, default=13)
    parser.add_argument("--maxzoom", type=int, default=18)
    args = parser.parse_args()
    if not 0 <= args.minzoom <= args.maxzoom <= 19:
        parser.error("Require 0 <= minzoom <= maxzoom <= 19")
    if args.download:
        download(args.source, args.resume_parts)
    build(args.source, args.output, args.minzoom, args.maxzoom)


if __name__ == "__main__":
    main()
