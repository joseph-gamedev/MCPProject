"""Builds Docs/Building-Types.pdf: every building type of unreal_kit.modern, how to make one and how it is built at runtime.

    python Scripts/docs/make_buildings_pdf.py

The type data (floors, lot, forms, facade systems, roofs, crowns) is read from unreal_kit.modern.KINDS, so the PDF follows
the code. Pictures come from Docs/Proof/buildings (Scripts/proof/building_proof.py, built in the packaged game).
"""
import datetime
import json
import os
import re
import sys

from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.platypus import Image, KeepTogether, PageBreak, Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path[:0] = [HERE, os.path.join(ROOT, "Scripts")]
import make_overview_pdf as base  # styles and helpers shared with the overview
from unreal_kit import modern

P, H1, H2, H3, BODY, SMALL, CELL, LEAD, LINE, SOFT = (base.P, base.H1, base.H2, base.H3, base.BODY, base.SMALL, base.CELL, base.LEAD,
                                                     base.LINE, base.SOFT)
OUT = os.path.join(ROOT, "Docs", "Building-Types.pdf")
PICS = os.path.join(ROOT, "Docs", "Proof", "buildings")

GROUPS = [("Homes", ["apartment", "villa", "townhouse", "shophouse"]),
          ("Work", ["office", "office_tower", "loft", "business_park", "bank"]),
          ("Education, health and public service", ["school", "kindergarten", "university", "hospital", "clinic", "government", "museum",
                                                    "police_station", "fire_station"]),
          ("Faith", ["mosque", "church"]),
          ("Shops, hotels and fuel", ["hotel", "mall", "supermarket", "petrol_station"]),
          ("Industry and parking", ["warehouse", "factory", "parking_garage"])]

# what each type adds around itself (read from its x_* site function in unreal_kit/modern.py)
SITE = {
    "apartment": "Entrance canopy.",
    "villa": "Swimming pool on a stone, timber or white deck; garden trees; small entrance canopy.",
    "townhouse": "Built to the lot edges (party walls), so a row of them forms a terrace.",
    "shophouse": "Shopfront ground floor; party walls for a street of them.",
    "office": "Front plaza in granite or paving with trees (sometimes a fountain); entrance canopy.",
    "office_tower": "Front plaza; a crown on top: fins, a glowing top, a spire, a lantern or a sloped cap.",
    "loft": "Brick, dark brick, charcoal or concrete cladding; no site elements.",
    "business_park": "Plaza, a parking lot in front and flagpoles.",
    "bank": "Granite or paved plaza with trees; entrance canopy.",
    "school": "Entrance canopy, flagpoles, a sports field behind the building, a playground and trees.",
    "kindergarten": "Entrance canopy, a fenced playground and trees.",
    "university": "Paved forecourt, three flagpoles and four to eight trees.",
    "hospital": "Helipad on the tallest block, main entrance canopy, emergency canopy and red or green cross signs, parking.",
    "clinic": "Entrance canopy and a cross sign.",
    "government": "A big thin roof on slender columns, a stone forecourt, a reflecting pool and three tall flagpoles.",
    "museum": "Granite or paved plaza with trees.",
    "police_station": "Blue entrance canopy and fascia band; a flagpole.",
    "fire_station": "Engine bay doors (red, white or aluminium) with glazing, a concrete apron and a hose-drying tower.",
    "mosque": "A dome (hemisphere, pointed or onion; gold, white, verdigris, blue or copper), sometimes four small domes; "
              "one, two or four minarets; a marble or paved forecourt.",
    "church": "A bell tower with a cross beside the nave.",
    "hotel": "Entrance canopy over a granite drop-off, a rooftop pool on a timber deck, trees.",
    "mall": "Entry canopies on each entrance block and a large parking lot.",
    "supermarket": "Entry canopy and a parking lot.",
    "petrol_station": "Forecourt, a canopy over two or more pump islands and a price pylon, all in one brand colour.",
    "warehouse": "Loading dock doors with canopies, a dock apron, a truck yard and an office annex.",
    "factory": "Office annex, one to three chimneys and up to four silos.",
    "parking_garage": "Open parking decks, a blue P sign and a stair core.",
}

FORM_WORDS = {
    "block": "one box", "bar": "a long slab", "L": "L-shaped", "U": "U-shaped", "H": "H-shaped", "courtyard": "around a courtyard",
    "stepped": "terraced steps", "cantilever": "an upper part overhanging", "notched": "a box with a cut corner", "bridge": "two blocks joined by a bridge",
    "curve": "a curved bar", "round": "round or elliptical", "podium_tower": "a tower on a podium", "twin": "two towers", "setback": "upper floors set back",
    "tower": "a slim tower", "slab": "a thin slab", "campus": "several linked wings", "wedge": "a wedge", "mall": "anchor blocks around a concourse",
}
SKIN_WORDS = {
    "curtain": "glass curtain wall with mullions", "ribbon": "ribbon windows between solid bands", "fins": "vertical fins over glass",
    "louvres": "horizontal sun louvres", "balconies": "continuous balconies with railings", "grid": "a structural grid (exoskeleton) over glass",
    "diagrid": "a diagonal grid", "screen": "a timber or metal screen", "punched": "punched windows in solid walls", "ribbed": "ribbed metal cladding",
    "tiles": "tiled or panel cladding", "monolith": "mostly solid walls, few openings", "open_deck": "open car-park decks",
}


def results():
    """{kind: {parts, size_m}} from the last building proof run (Saved), else from Docs/Proof/README.md."""
    p = os.path.join(ROOT, "Saved", "ToolsetProof", "buildings", "results.json")
    if os.path.exists(p):
        return json.load(open(p, encoding="utf-8"))
    out = {}
    readme = os.path.join(ROOT, "Docs", "Proof", "README.md")
    if os.path.exists(readme):
        for k, parts, size in re.findall(r"^\| ([a-z_]+) \| (\d+) \| ([\d x]+) \|", open(readme, encoding="utf-8").read(), re.M):
            out[k] = {"parts": int(parts), "size_m": [int(v) for v in size.split(" x ")]}
    return out


def uniq(seq):
    out = []
    for x in seq or []:
        if x not in out:
            out.append(x)
    return out


def m(v):
    return f"{v / 100:g} m"


def card(kind, res):
    k = modern.KINDS[kind]
    f0, f1 = k["floors"]
    (w0, w1), (d0, d1) = k["lot"]
    forms = ", ".join(uniq(k.get("forms")))
    skins = ", ".join(uniq(list(k["skins"]) if isinstance(k.get("skins"), dict) else k.get("skins")))
    roofs = ", ".join(r for r in uniq(k.get("roofs")) if r != "none") or "flat"
    crowns = ", ".join(c for c in uniq(k.get("crowns")) if c != "none")
    r = res.get(kind, {})
    made = f"The pictured example: {r['parts']} parts, {' x '.join(map(str, r['size_m']))} m" if r.get("parts") else ""
    floors = f"{f0}" if f0 == f1 else f"{f0} to {f1}"
    rows = [f"<b>{kind.replace('_', ' ').title()}</b> &nbsp;<font size=7.5 color='#5f5f5a'>modern.place(\"{kind}\")</font>",
            f"<b>Floors</b> {floors} &nbsp;·&nbsp; <b>Lot</b> {m(w0)} to {m(w1)} wide, {m(d0)} to {m(d1)} deep",
            f"<b>Forms</b> {forms}", f"<b>Facades</b> {skins}", f"<b>Roofs</b> {roofs}" + (f" &nbsp;·&nbsp; <b>Crowns</b> {crowns}" if crowns else ""),
            f"<b>Around it</b> {SITE.get(kind, '')}"]
    if made:
        rows.append(f"<font color='#5f5f5a'>{made}</font>")
    text = [P(x, CELL) for x in rows]
    pic = os.path.join(PICS, kind + ".jpg")
    img = Image(pic, width=72 * mm, height=40.5 * mm) if os.path.exists(pic) else P("(no picture yet: run Scripts/proof/building_proof.py)", SMALL)
    t = Table([[img, text]], colWidths=[75 * mm, 105 * mm])
    t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.4, LINE),
                           ("BOTTOMPADDING", (0, 0), (-1, -1), 7), ("TOPPADDING", (0, 0), (-1, -1), 5), ("LEFTPADDING", (0, 0), (-1, -1), 0)]))
    return t


def story():
    res = results()
    s = [P("Building types", H1),
         P(f"unreal_kit.modern &nbsp;·&nbsp; {len(modern.KINDS)} types &nbsp;·&nbsp; Runtime Toolsets, Unreal Engine 5.8 &nbsp;·&nbsp; "
           f"{datetime.date.today().isoformat()}", SMALL), Spacer(1, 8),
         P(f"The Python kit builds <b>{len(modern.KINDS)} kinds of contemporary building</b> in a running game, each with one call: "
           "homes, offices and towers, schools, a hospital, places of worship, shops, a petrol station, a factory. Every choice "
           "comes from a seed (shape, facade, colours, proportions, roof and the type's own site elements), so a street of "
           "them never repeats, and the same seed always gives the same building. Storey heights and proportions follow "
           "real buildings: 3.8 m classroom storeys, dock doors at trailer height, car-park decks about 3 m apart.", LEAD),
         P("Make one", H2),
         P("<font face='Courier' size=8.5>from unreal_kit import modern<br/>"
           "modern.place(\"hospital\", at=(0, 0, 0), seed=7)&nbsp;&nbsp;&nbsp;# builds it in the game, returns its scene node<br/>"
           "modern.place(\"apartment\", at=(5000, 0, 0), yaw=90, floors=12, skin=\"balconies\")<br/>"
           "parts = modern.building(\"office_tower\", seed=4)&nbsp;&nbsp;# only the parts list, nothing built yet</font>", BODY),
         base.table([["Option", "What it does"],
                     ["kind", "One of the types in this document."],
                     ["seed", "Same seed, same building; a new seed gives a new design of the same type."],
                     ["floors / width / depth", "Exact storeys and lot size in cm (the type's range otherwise)."],
                     ["polygon", "Build on any footprint, e.g. a GIS plot: world cm, the parts come back in world coordinates."],
                     ["form / skin", "Force a composition (forms below) or a facade system (facades below)."],
                     ["detail", "high, medium or low: fewer parts for distant buildings."],
                     ["site", "False builds the building alone, without its plaza, parking, sports field or other site elements."],
                     ["at / yaw (place)", "Where the lot's front centre goes and which way the front faces (front faces -y at yaw 0)."]],
                    [38 * mm, 142 * mm]),
         P("How a building is made at runtime", H2),
         base.bullets(["<b>Design, on the agent's side.</b> modern.building runs in Python, outside the engine: from the seed it picks a "
                       "form, outlines each volume (rectangle, rounded, chamfered, ellipse, curved bar or the given polygon), wraps it in "
                       "a facade system made of whole-height or whole-perimeter pieces (no per-window modules), adds the ground floor, "
                       "roof, crown and the type's site elements, and returns a list of parts: shape, size, position, material.",
                       "<b>Check.</b> uk.place_many checks every part first, so a mistake fails before anything is built.",
                       "<b>Send.</b> The parts go to the game as build.create_assembly calls inside ops.run_batch: up to 12,000 parts per "
                       "request and three requests in flight. A typical building (22 to 816 parts here) is one request.",
                       "<b>Build, in the game.</b> The builder turns the parts into dynamic meshes with shared materials on the game "
                       "thread (about 4 ms per frame, so the game keeps rendering) and registers the building as one scene node: it "
                       "can be moved, styled, saved as a template and undone like anything else.",
                       "<b>Plant.</b> modern.place then adds the building's trees, shrubs and benches as GPU instances of the project's "
                       "real meshes (RuntimeFoliage and RuntimeArchitecture props)."], numbered=True),
         P("In the proof run every type built in the packaged game in about a second (6 s each including a 5 s wait for the "
           "screenshot). Districts (unreal_kit.district) and GIS plots (gis.set_plot building) use the same types.", BODY)]

    forms_used = uniq([f for k in modern.KINDS.values() for f in (k.get("forms") or [])])
    skins_used = uniq([x for k in modern.KINDS.values() for x in (list(k["skins"]) if isinstance(k.get("skins"), dict) else (k.get("skins") or []))])
    s += [KeepTogether([P("Forms and facades", H2),
          P("Each type draws from its own short list of these:"),
          Table([[base.table([["Form", "Shape"]] + [[f, FORM_WORDS.get(f, "")] for f in forms_used], [24 * mm, 62 * mm]),
                  base.table([["Facade", "What it looks like"]] + [[x, SKIN_WORDS.get(x, "")] for x in skins_used], [22 * mm, 66 * mm])]],
                colWidths=[90 * mm, 90 * mm], style=[("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0)])])]

    for title, kinds in GROUPS:
        s += [PageBreak(), P(title, H2)]
        for kind in kinds:
            s.append(KeepTogether(card(kind, res)))
    missing = [k for k in modern.KINDS if not any(k in g for _, g in GROUPS)]
    if missing:
        s += [PageBreak(), P("Other types", H2)] + [KeepTogether(card(k, res)) for k in missing]
    return s


def footer(canvas, doc):
    canvas.saveState()
    canvas.setFont("Helvetica", 7.5)
    canvas.setFillColor(base.MUTE)
    canvas.drawString(15 * mm, 10 * mm, "Runtime Toolsets - building types")
    canvas.drawRightString(A4[0] - 15 * mm, 10 * mm, f"page {doc.page}")
    canvas.restoreState()


def main():
    doc = SimpleDocTemplate(OUT, pagesize=A4, leftMargin=15 * mm, rightMargin=15 * mm, topMargin=15 * mm, bottomMargin=16 * mm,
                            title="Runtime Toolsets: building types", author="MCPProject")
    doc.build(story(), onFirstPage=footer, onLaterPages=footer)
    print("written", OUT, os.path.getsize(OUT) // 1024, "KB")


if __name__ == "__main__":
    main()
