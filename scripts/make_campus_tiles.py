#!/usr/bin/env python3
"""Render locally from an Overpass `out geom` JSON export. Never scrapes map tiles.

Requires Pillow only for regeneration, not for running the Qt application.
Data: OpenStreetMap contributors, ODbL. Output: WGS84 Web Mercator XYZ PNG.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

BBOX = (119.620, 29.124, 119.654, 29.151)  # west, south, east, north
ATTRIBUTION = "© OpenStreetMap contributors · ODbL"


def world(lon, lat, z):
    size = 256 * 2**z
    lat = max(-85.05112878, min(85.05112878, lat))
    return ((lon + 180) / 360 * size,
            (1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2 * size)


def load_font(size, override=None):
    candidates = [override, "/System/Library/Fonts/STHeiti Medium.ttc",
                  "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return ImageFont.truetype(candidate, size)
    raise RuntimeError("Chinese font missing: pass --font=/path/to/font.ttf")


def render(source, destination, font_path=None):
    raw = source.read_bytes()
    data = json.loads(raw)
    if data.get("remark"):
        raise ValueError("Incomplete/error Overpass response: " + data["remark"])
    elements = data["elements"]
    # Validate the actual campus feature before producing any tiles.
    campus = next((e for e in elements if e.get("type") == "way" and e.get("id") == 297356404), None)
    if not campus or campus.get("tags", {}).get("amenity") != "university":
        raise ValueError("Export must include OSM campus way 297356404; cannot substitute invented geography")
    features = [e for e in elements if e.get("geometry") and len(e["geometry"]) >= 2]
    count = 0
    for z in range(14, 20):
        nw = world(BBOX[0], BBOX[3], z)
        se = world(BBOX[2], BBOX[1], z)
        x0, y0, x1, y1 = (math.floor(nw[0]/256), math.floor(nw[1]/256),
                          math.floor(se[0]/256), math.floor(se[1]/256))
        prepared, labels = [], []
        font = load_font(12 if z >= 17 else 11, font_path)
        large = load_font(15, font_path)
        for e in features:
            pts = [world(p["lon"], p["lat"], z) for p in e["geometry"]]
            bounds = (min(p[0] for p in pts), min(p[1] for p in pts),
                      max(p[0] for p in pts), max(p[1] for p in pts))
            tags = e.get("tags", {})
            closed = e["geometry"][0] == e["geometry"][-1]
            fill, line, width, layer = None, None, 1, 0
            if closed:
                if tags.get("amenity") == "university": fill, line, layer = "#e0ebdc", "#89a686", 0
                elif tags.get("natural") == "water" or tags.get("water"): fill, line, layer = "#93cfdf", "#79bacf", 2
                elif tags.get("landuse") in ("forest", "grass", "meadow", "orchard") or tags.get("leisure") in ("park", "garden", "pitch"):
                    fill, line, layer = "#c5dfb2", "#adc996", 1
                elif tags.get("building"): fill, line, layer = "#c8c5bd", "#9b9b91", 3
                elif tags.get("landuse") == "residential": fill, layer = "#e6e3dc", 0
            highway = tags.get("highway")
            if highway and not (closed and tags.get("area") == "yes"):
                base = {"motorway": 13, "trunk": 12, "primary": 10, "secondary": 8,
                        "tertiary": 7, "residential": 5, "service": 4,
                        "footway": 2, "path": 2, "cycleway": 2, "steps": 2}.get(highway, 4)
                width = max(1, round(base * 2 ** (z - 17)))
                line, layer = ("#f6d48b" if base >= 8 else "#faf9ee"), 5
            elif tags.get("waterway") and not closed:
                line, width, layer = "#93cfdf", max(2, 2**(z-16)), 2
            if fill or line:
                prepared.append((layer, pts, bounds, fill, line, int(width), bool(highway)))
            name = tags.get("name:zh") or tags.get("name")
            if name and (e is campus or z >= 17 or (highway and z >= 16)):
                # Geometric label positions derive solely from source geometry.
                px, py = ((bounds[0] + bounds[2])/2, (bounds[1] + bounds[3])/2)
                labels.append((0 if e is campus else 1 if tags.get("building") else 2,
                               px, py, name, large if e is campus else font))
        # Place labels once per zoom in global pixel coordinates: stable across tile boundaries.
        occupied, accepted = [], []
        for _, px, py, name, label_font in sorted(labels, key=lambda item: item[0]):
            text_box = label_font.getbbox(name)
            tw, th = text_box[2]-text_box[0], text_box[3]-text_box[1]
            box = (px-tw/2-5, py-th/2-4, px+tw/2+5, py+th/2+4)
            if any(box[0]<b[2] and box[2]>b[0] and box[1]<b[3] and box[3]>b[1] for b in occupied): continue
            occupied.append(box); accepted.append((box, px, py, name, label_font))
        prepared.sort(key=lambda p: p[0])
        for x in range(x0, x1+1):
            folder = destination / str(z) / str(x)
            folder.mkdir(parents=True, exist_ok=True)
            for y in range(y0, y1+1):
                # Render padded tiles so road outlines/labels do not create seam artifacts.
                pad = 64
                image = Image.new("RGB", (256+pad*2, 256+pad*2), "#edf0e6")
                draw = ImageDraw.Draw(image)
                ox, oy = x*256-pad, y*256-pad
                for layer, pts, bounds, fill, line, width, road in prepared:
                    if bounds[2]<ox or bounds[0]>ox+384 or bounds[3]<oy or bounds[1]>oy+384: continue
                    local = [(px-ox, py-oy) for px, py in pts]
                    if fill: draw.polygon(local, fill=fill)
                    if line:
                        if road: draw.line(local, fill="#c4c5b9", width=width+2, joint="curve")
                        draw.line(local, fill=line, width=width, joint="curve")
                for box, px, py, name, label_font in accepted:
                    if box[2]<ox or box[0]>ox+384 or box[3]<oy or box[1]>oy+384: continue
                    draw.text((px-ox, py-oy), name, fill="#364b3e", font=label_font,
                              anchor="mm", stroke_width=2, stroke_fill="#f8f9f2")
                image.crop((pad, pad, pad+256, pad+256)).save(folder / f"{y}.png", optimize=True)
                count += 1
        print(f"zoom {z}: {(x1-x0+1)*(y1-y0+1)} tiles", flush=True)
    metadata = {"name": "浙江师范大学金华校区及周边", "type": "locally rendered street/building map, NOT satellite imagery",
                "scheme": "xyz", "projection": "EPSG:3857", "coordinate_system": "WGS84",
                "bounds": BBOX, "center": [119.63764, 29.13678], "minzoom": 14, "maxzoom": 19,
                "tile_count": count, "source": "https://overpass.private.coffee/api/interpreter",
                "source_timestamp": data.get("osm3s", {}).get("timestamp_osm_base"),
                "source_sha256": hashlib.sha256(raw).hexdigest(), "feature_count": len(features),
                "attribution": ATTRIBUTION, "license": "https://opendatacommons.org/licenses/odbl/1-0/",
                "copyright": "https://www.openstreetmap.org/copyright",
                "warning": "Open map data may be incomplete/outdated. Not a surveyed navigation or flight safety map."}
    (destination / "metadata.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")
    # Keep the complete data snapshot with its license attribution for reproducibility.
    (destination / "source-osm.json").write_bytes(raw)
    print(f"Created {count} tiles; {len(features)} source ways", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Complete Overpass out-geom JSON file")
    parser.add_argument("destination", type=Path)
    parser.add_argument("--font")
    args = parser.parse_args()
    render(args.source, args.destination, args.font)
