"""Turns the last proof run (Saved/ToolsetProof) into Docs/Proof: compressed JPEG screenshots and a README with the results.

    python Scripts/proof/toolset_proof.py      # every toolset, in a running game (port 8028 by default)
    python Scripts/proof/building_proof.py     # every building type of unreal_kit.modern
    python Scripts/proof/publish_proof.py      # -> Docs/Proof (commit this folder)
"""
import glob
import json
import os
import shutil
import sys

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "Saved", "ToolsetProof")
DST = os.path.join(ROOT, "Docs", "Proof")
WIDTH, QUALITY = 960, 80


def jpeg(src, dst):
    im = Image.open(src).convert("RGB")
    if im.width > WIDTH:
        im = im.resize((WIDTH, round(im.height * WIDTH / im.width)), Image.LANCZOS)
    im.save(dst, "JPEG", quality=QUALITY, optimize=True, progressive=True)


def main():
    if not os.path.exists(os.path.join(SRC, "results.json")):
        sys.exit("no proof run in Saved/ToolsetProof: run toolset_proof.py first")
    shutil.rmtree(DST, ignore_errors=True)
    os.makedirs(os.path.join(DST, "toolsets"))
    os.makedirs(os.path.join(DST, "buildings"))
    results = json.load(open(os.path.join(SRC, "results.json"), encoding="utf-8"))
    shots = {}
    for f in sorted(glob.glob(os.path.join(SRC, "*.png"))):
        name = os.path.basename(f)[:-4]
        jpeg(f, os.path.join(DST, "toolsets", name + ".jpg"))
        shots.setdefault(name.split("_")[0], []).append(name)
    buildings = {}
    bres = os.path.join(SRC, "buildings", "results.json")
    if os.path.exists(bres):
        buildings = json.load(open(bres, encoding="utf-8"))
        for f in sorted(glob.glob(os.path.join(SRC, "buildings", "*.png"))):
            jpeg(f, os.path.join(DST, "buildings", os.path.basename(f)[:-4] + ".jpg"))
    for k in results:   # the calls each toolset made (no screenshots for the non-visual ones), local paths removed
        text = open(os.path.join(SRC, k + ".json"), encoding="utf-8").read()
        for variant in (ROOT, ROOT.replace("\\", "/"), ROOT.replace("\\", "\\\\")):
            text = text.replace(variant, "<project>")
        open(os.path.join(DST, "toolsets", k + ".json"), "w", encoding="utf-8").write(text)
    passed, total = sum(v["passed"] for v in results.values()), sum(v["total"] for v in results.values())
    lines = ["# Toolset proof", "",
             f"Every Runtime Toolsets toolset exercised in a running game: **{len(results)} toolsets, {passed}/{total} tool calls passed**, "
             "each on a fresh scene with the default look. Regenerate with the scripts in `Scripts/proof` (see `publish_proof.py`).", "",
             "| Toolset | Calls | Proof | Notes |", "|---|---|---|---|"]
    for k, v in results.items():
        proof = ", ".join(f"[{s}](toolsets/{s}.jpg)" for s in shots.get(k, [])) or f"[responses](toolsets/{k}.json)"
        notes = "; ".join(f["error"].split("\n")[0][:110] for f in v["failures"]).replace("|", "/")
        lines.append(f"| {k} | {v['passed']}/{v['total']} | {proof} | {notes} |")
    lines += ["", "The one call that does not pass is expected: `session.call_tool` refuses to call itself, and the Python kit already "
              "reaches the game through it.", ""]
    for k, names in shots.items():
        lines += [f"### {k}", "", " ".join(f'<img src="toolsets/{n}.jpg" width="300" alt="{n}">' for n in names), ""]
    if buildings:
        ok = sum(1 for v in buildings.values() if v.get("ok"))
        lines += ["## Building types", "", f"Every building type of `unreal_kit.modern` ({ok}/{len(buildings)} built), one call each: "
                  "`modern.place(kind)`.", "", "| Type | Parts | Size (m) | Image |", "|---|---|---|---|"]
        for k, v in buildings.items():
            lines.append(f"| {k} | {v.get('parts', '')} | {' x '.join(map(str, v.get('size_m', [])))} | [{k}](buildings/{k}.jpg) |")
        lines += [""] + [f'<img src="buildings/{k}.jpg" width="300" alt="{k}">' for k in buildings] + [""]
    open(os.path.join(DST, "README.md"), "w", encoding="utf-8").write("\n".join(lines))
    size = sum(os.path.getsize(f) for f in glob.glob(os.path.join(DST, "**", "*"), recursive=True) if os.path.isfile(f))
    print(f"Docs/Proof: {len(shots)} toolsets with screenshots, {len(buildings)} building types, {size / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
