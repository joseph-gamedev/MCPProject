"""
Inventory and player stats, built entirely from tool calls (what an AI agent does from a prompt):

  * character window (I / Tab): equipment, stats with level-up points, 12-slot inventory grid with tooltips and rarities
  * hotbar (keys 1-4) and a HUD card with level, health, XP and gold
  * item database in a variable: weapons, armor, helmets, rings, potions, gems; stacking, use/equip/discard
  * pickups that bob in the world, chests (E), slime drops, and a merchant shop (E) with buy buttons and gem selling
  * stats STR / DEF / VIT / AGI -> attack, block, max HP, move speed; armor absorbs damage; XP and level-ups
  * slimes are real Unreal AI agents (navmesh + perception); F5 / F9 save and load

The UI uses logic.define_ui layout items (panel / row / column / grid, like HTML + flexbox) with a theme.

    python Scripts/examples/inventory_rpg.py [--url http://127.0.0.1:8018/mcp] [--clear] [--out shot.jpg] [--open]
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from rtbuild import *  # noqa: E402,F403

HIDE_Z = -60000  # templates live far below the world

# ---- item database -------------------------------------------------------------------------------------------------
ITEMS = {
    "wood_sword":    {"name": "Wooden Sword",  "type": "weapon", "rarity": "common",    "atk": 3,  "def": 0, "hp": 0,  "spd": 0,   "heal": 0,   "stack": 1,  "value": 5,  "color": [0.55, 0.35, 0.15]},
    "iron_sword":    {"name": "Iron Sword",    "type": "weapon", "rarity": "rare",      "atk": 7,  "def": 0, "hp": 0,  "spd": 0,   "heal": 0,   "stack": 1,  "value": 20, "color": [0.75, 0.78, 0.82]},
    "fire_staff":    {"name": "Fire Staff",    "type": "weapon", "rarity": "legendary", "atk": 12, "def": 0, "hp": 0,  "spd": 0,   "heal": 0,   "stack": 1,  "value": 45, "color": [1.0, 0.35, 0.1]},
    "leather_armor": {"name": "Leather Armor", "type": "armor",  "rarity": "common",    "atk": 0,  "def": 3, "hp": 10, "spd": 0,   "heal": 0,   "stack": 1,  "value": 10, "color": [0.45, 0.28, 0.15]},
    "iron_armor":    {"name": "Iron Armor",    "type": "armor",  "rarity": "rare",      "atk": 0,  "def": 8, "hp": 25, "spd": -40, "heal": 0,   "stack": 1,  "value": 35, "color": [0.6, 0.62, 0.66]},
    "cap":           {"name": "Leather Cap",   "type": "helmet", "rarity": "common",    "atk": 0,  "def": 1, "hp": 5,  "spd": 0,   "heal": 0,   "stack": 1,  "value": 4,  "color": [0.5, 0.32, 0.18]},
    "iron_helm":     {"name": "Iron Helm",     "type": "helmet", "rarity": "rare",      "atk": 0,  "def": 4, "hp": 10, "spd": 0,   "heal": 0,   "stack": 1,  "value": 18, "color": [0.7, 0.72, 0.76]},
    "ring_vigor":    {"name": "Ring of Vigor", "type": "ring",   "rarity": "epic",      "atk": 0,  "def": 0, "hp": 40, "spd": 0,   "heal": 0,   "stack": 1,  "value": 30, "color": [0.2, 0.9, 0.3]},
    "ring_swift":    {"name": "Ring of Haste", "type": "ring",   "rarity": "epic",      "atk": 1,  "def": 0, "hp": 0,  "spd": 150, "heal": 0,   "stack": 1,  "value": 30, "color": [0.2, 0.7, 1.0]},
    "potion":        {"name": "Health Potion", "type": "potion", "rarity": "common",    "atk": 0,  "def": 0, "hp": 0,  "spd": 0,   "heal": 40,  "stack": 9,  "value": 6,  "color": [0.95, 0.15, 0.2]},
    "elixir":        {"name": "Elixir",        "type": "potion", "rarity": "epic",      "atk": 0,  "def": 0, "hp": 0,  "spd": 0,   "heal": 999, "stack": 5,  "value": 25, "color": [0.9, 0.5, 1.0]},
    "gem":           {"name": "Sapphire",      "type": "gem",    "rarity": "rare",      "atk": 0,  "def": 0, "hp": 0,  "spd": 0,   "heal": 0,   "stack": 20, "value": 15, "color": [0.3, 0.95, 0.95]},
}
SLOTS = ["weapon", "armor", "helmet", "ring"]
BAG = 12
CHEST_LOOT = ["iron_sword", "iron_armor", "iron_helm", "ring_vigor", "ring_swift", "elixir", "fire_staff", "potion", "potion", "gem"]
SHOP = ["potion", "elixir", "iron_sword", "iron_helm", "iron_armor"]

THEME = {
    "colors": {
        "accent": "#E8B04B", "accent_text": "#1A1408", "panel": "#11141CF0", "surface": "#1B202D", "surface_hover": "#262D3F",
        "text": "#EEF0F5", "muted": "#9AA3B5", "border": "#FFFFFF1C",
        "common": "#8E97A8", "rare": "#4EA8FF", "epic": "#B57CFF", "legendary": "#FFB547",
        "t_weapon": "#AEB9C7", "t_armor": "#A27752", "t_helmet": "#8A94A3", "t_ring": "#4FD1C5", "t_potion": "#E5546A", "t_gem": "#3CE0E0",
    },
    "radius": 14, "padding": 20, "gap": 10, "font_size": 15,
}


def tooltip(it):
    """Plain text shown on hover; precomputed so the UI expression is just tips[id]."""
    lines = [it["name"], f"{it['rarity'].title()} {it['type']}"]
    for key, label in [("atk", "Attack"), ("def", "Defense"), ("hp", "Max HP"), ("spd", "Speed")]:
        if it[key]:
            lines.append(f"{'+' if it[key] > 0 else ''}{it[key]} {label}")
    if it["heal"]:
        lines.append("Restores all health" if it["heal"] >= 999 else f"Restores {it['heal']} health")
    if it["type"] == "gem":
        lines.append(f"Sell to the merchant for {it['value']} gold")
    lines.append("Click to " + ("drink" if it["type"] == "potion" else "inspect" if it["type"] == "gem" else "equip"))
    return "\n".join(lines)


def eq_sum(stat):
    return " + ".join(f"(eq_{s} == '' ? 0 : db[eq_{s}].{stat})" for s in SLOTS)


def msg(text):
    return [{"set": "msg", "value": text}, {"set": "msg_until", "value": "time + 2.5"}]


# ---- world ---------------------------------------------------------------------------------------------------------

def build_world(rt):
    grass = mat("grass", color=[0.2, 0.4, 0.13])
    stone = mat("stone", color=[0.52, 0.5, 0.48])
    wood = mat("wood", color=[0.4, 0.26, 0.14])
    P = Parts()
    P += box([7000, 7000, 40], at=[0, 0, -40], **grass)
    P += box([5200, 260, 4], at=[0, 0, -3.5], **mat("sand", color=[0.42, 0.33, 0.22]))
    P += box([260, 5200, 4], at=[0, 0, -3.5], **mat("sand", color=[0.42, 0.33, 0.22]))
    for x, y, w, d in [(0, 3500, 7100, 120), (0, -3500, 7100, 120), (3500, 0, 120, 7100), (-3500, 0, 120, 7100),
                       (-1200, 1200, 1600, 120), (1300, -900, 120, 1800), (1500, 1700, 1200, 120), (-1700, -1500, 120, 1400)]:
        P += box([w, d, 220], at=[x, y, 0], **stone)
    for x, y, s in [(-2500, 2500, 1), (2600, 2500, 1.2), (-2600, -2600, 0.9), (2500, -2600, 1.1), (300, 2200, 1), (-300, -2300, 1.3),
                    (2600, 300, 0.9), (-2700, 200, 1.1), (-900, -2800, 1), (1000, 2900, 0.8), (-2900, -1200, 1.2), (2900, -1300, 1)]:
        P += cyl(60 * s, 260 * s, at=[x, y, 0], detail="low", **wood)
        P += cone(400 * s, 380 * s, at=[x, y, 200 * s], detail="low", **mat("grass", color=[0.12, 0.33, 0.12]))
        P += cone(300 * s, 300 * s, at=[x, y, 420 * s], detail="low", **mat("grass", color=[0.15, 0.4, 0.14]))
    for x, y, s in [(-500, 1500, 120), (800, -1600, 90), (1900, 900, 140), (-2000, -600, 110), (600, 3000, 100), (-3000, 2900, 130)]:
        P += sphere(s, at=[x, y, 0], scale=[1.3, 1, 0.6], **stone)
    rt.build("World", P)
    for i, (x, y) in enumerate([(-2200, 1800), (2300, -1700), (2400, 2800)]):
        rt.build(f"chest_{i}", [box([140, 90, 80], at=[x, y, 0], **wood),
                                box([150, 100, 22], at=[x, y, 80], **mat("painted_iron", color=[0.75, 0.55, 0.18])),
                                box([24, 10, 30], at=[x + 0, y - 48, 55], **mat("gold"))])
    rt.build("merchant", [cyl(90, 150, at=[-600, -600, 0], **mat("plaster", color=[0.3, 0.2, 0.6])),
                          sphere(70, at=[-600, -600, 185], **mat("plaster", color=[0.95, 0.8, 0.65])),
                          cone(110, 90, at=[-600, -600, 215], **mat("plaster", color=[0.25, 0.15, 0.5])),
                          box([420, 170, 90], at=[-600, -430, 0], **wood),
                          box([460, 200, 12], at=[-600, -430, 240], **mat("plaster", color=[0.8, 0.2, 0.2]))])


def build_templates(rt):
    """One pickup template per item and the slime; pools hand out copies."""
    for i, (iid, it) in enumerate(ITEMS.items()):
        shape = {"weapon": box([26, 120, 16], at=[0, 0, 50]), "armor": box([80, 40, 90], at=[0, 0, 25]), "helmet": dome(70, 45, at=[0, 0, 35]),
                 "ring": cyl(50, 12, at=[0, 0, 50]), "potion": cyl(40, 60, at=[0, 0, 25]), "gem": sphere(50, at=[0, 0, 45])}[it["type"]]
        shape.update(mat("plaster", color=it["color"], emissive=it["color"], emissive_strength=2.0))
        glow = cyl(110, 3, at=[0, 0, 2], **mat("plaster", color=it["color"], emissive=it["color"], emissive_strength=1.0, opacity=0.35))
        rt.call("build.create_assembly", name=f"tpl_{iid}", parts=[shape, glow], location=[i * 300, 0, HIDE_Z], tags=["pickup", f"item_{iid}"])
        rt.call("logic.define_pool", name=f"p_{iid}", template=f"tpl_{iid}", size=8)
    rt.call("build.create_assembly", name="tpl_gold", parts=[cyl(60, 12, at=[0, 0, 20], **mat("gold")), cyl(60, 12, at=[25, 15, 32], **mat("gold"))],
            location=[0, 600, HIDE_Z], tags=["pickup", "item_gold"])
    rt.call("logic.define_pool", name="p_gold", template="tpl_gold", size=10)
    slime = [sphere(150, at=[0, 0, 60], scale=[1, 1, 0.75], **mat("plaster", color=[0.3, 0.85, 0.35], roughness=0.15)),
             sphere(32, at=[58, -28, 95], **mat("plaster", color=[0.03, 0.03, 0.03])), sphere(32, at=[58, 28, 95], **mat("plaster", color=[0.03, 0.03, 0.03]))]
    rt.call("build.create_assembly", name="tpl_slime", parts=slime, location=[0, 1200, HIDE_Z], tags=["slime"])
    rt.call("logic.define_pool", name="slimes", template="tpl_slime", size=6)


# ---- UI ------------------------------------------------------------------------------------------------------------

def to_hex(c):
    """Linear [r,g,b] -> "#rrggbb" (sRGB), for UI colors bound to item data."""
    srgb = [min(255, round(255 * (12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055))) for v in c]
    return "#" + "".join(f"{v:02X}" for v in srgb)


def item_icon(item, scale=1.0):
    """An icon drawn from UI shapes, one variant per item type, colored live from the item's data. item = expression of the id."""
    col = f"{{db[{item}].hex}}"
    px = lambda v: round(v * scale)
    shape = lambda w, h, r, bg, **kw: {"kind": "column", "width": px(w), "height": px(h), "radius": px(r), "background": bg, **kw}
    is_type = lambda t: f"{item} != '' && db[{item}].type == '{t}'"
    variants = [
        ("weapon", {"kind": "column", "align": "center", "gap": 0, "rotate": 45, "children": [
            shape(6, 30, 3, col), shape(18, 5, 2, "#D9B45A"), shape(5, 8, 1, "#6B4426"), shape(8, 5, 2.5, "#D9B45A")]}),
        ("armor", {"kind": "column", "align": "center", "gap": px(2), "children": [
            {"kind": "row", "gap": px(12), "children": [shape(9, 8, 3, col), shape(9, 8, 3, col)]},
            shape(24, 22, 6, col), shape(24, 4, 2, "#00000055")]}),
        ("helmet", {"kind": "column", "align": "center", "gap": 0, "children": [
            shape(28, 20, 10, col), shape(32, 6, 3, col), shape(18, 4, 2, "#00000066")]}),
        ("ring", {"kind": "stack", "children": [
            {"kind": "stack", "h_align": "center", "v_align": "center", "children": [
                shape(28, 28, 14, col), shape(16, 16, 8, "surface", h_align="center", v_align="center")]},
            shape(10, 10, 5, "#FFFFFF", h_align="center", v_align="start", margin=[px(-4), 0, 0, 0])]}),
        ("potion", {"kind": "column", "align": "center", "gap": 0, "children": [
            shape(10, 5, 2, "#8B5A2B"), shape(8, 6, 1, "#FFFFFF66"), shape(26, 24, 12, col)]}),
        ("gem", {"kind": "stack", "children": [
            shape(22, 22, 4, col, rotate=45, h_align="center", v_align="center"),
            shape(7, 7, 3.5, "#FFFFFFB0", h_align="center", v_align="center", margin=[0, 0, px(6), px(6)])]}),
    ]
    return {"kind": "stack", "width": px(40), "height": px(40), "h_align": "center",
            "children": [dict(v, visible_if=is_type(t), h_align="center", v_align="center") for t, v in variants]}


def item_slot(name, size, index="i", key_label=None, names=True):
    """A bag slot button: drawn icon, name, stack count, rarity border, tooltip. Rules get event.i."""
    item = f"bag[{index}]"
    has = f"{item} != ''"
    content = {"kind": "stack", "grow": 1, "children": [
        {"kind": "column", "gap": 4, "align": "center", "v_align": "center" if names else "start", "children": [
            item_icon(item, 1.0 if names else 0.85),
            {"kind": "text", "text": f"{{{has} ? db[{item}].name : ''}}", "size": "xs", "align": "center", "wrap": True, "color": "muted",
             "visible_if": "true" if names else "false"}]},
        {"kind": "text", "text": f"{{bag_n[{index}]}}", "size": "sm", "weight": "bold", "h_align": "end", "v_align": "start" if names else "end",
         "visible_if": f"bag_n[{index}] > 1", "shadow": True},
    ]}
    if key_label:
        content["children"].append({"kind": "text", "text": key_label, "size": "xs", "weight": "bold", "color": "accent", "h_align": "start", "v_align": "end"})
    return {"name": name, "kind": "button", "width": size, "height": size, "padding": 6,
            "tooltip": f"{{{has} ? tips[{item}] : ''}}",
            "style_if": [{"if": f"{has} && db[{item}].rarity == '{r}'", "border_color": r} for r in ["legendary", "epic", "rare"]]
                        + [{"if": f"mode == 'discard' && {has}", "border_color": "danger"}],
            "children": [content]}


def section(title, extra=None):
    row = {"kind": "row", "children": [{"kind": "text", "text": title, "size": "xs", "weight": "bold", "color": "muted"}]}
    if extra:
        row["children"] += [{"kind": "spacer", "grow": 1}, extra]
    return row


def build_ui():
    hud = {"name": "hud", "kind": "panel", "anchor": "top-left", "width": 320, "padding": 14, "gap": 10, "children": [
        {"kind": "row", "gap": 12, "children": [
            {"kind": "column", "width": 46, "height": 46, "radius": 23, "background": "accent", "align": "center", "justify": "center", "gap": 0, "children": [
                {"kind": "text", "text": "LV", "size": 9, "weight": "bold", "color": "accent_text", "shadow": False},
                {"kind": "text", "text": "{level}", "size": "lg", "weight": "black", "color": "accent_text", "shadow": False}]},
            {"kind": "column", "grow": 1, "gap": 6, "children": [
                {"kind": "row", "children": [{"kind": "text", "text": "Adventurer", "weight": "bold"}, {"kind": "spacer", "grow": 1},
                                             {"kind": "text", "text": "{gold} g", "weight": "bold", "color": "gold"}]},
                {"kind": "bar", "value": "health.player", "max": "max_hp", "fill": "#E5484D", "height": 18, "text": "{round(health.player)} / {max_hp}"},
                {"kind": "bar", "value": "xp", "max": "xp_next", "fill": "#4EA8FF", "height": 6, "tooltip": "XP {xp} / {xp_next}"}]}]},
        {"kind": "text", "text": "{points} stat points to spend - press I", "size": "sm", "weight": "medium", "color": "accent", "visible_if": "points > 0"},
    ]}
    hotbar = {"name": "hotbar", "kind": "panel", "anchor": "bottom", "padding": [10, 14], "gap": 8, "align": "center", "children": [
        {"kind": "row", "gap": 8, "children": [item_slot("slot", 70, key_label="{i + 1}", names=False) | {"repeat": 4, "as": "i"}]},
        {"kind": "text", "text": "WASD move  ·  LMB shoot  ·  Space dash  ·  E interact  ·  I character  ·  F5/F9 save/load", "size": "xs",
         "color": "muted", "align": "center", "h_align": "center"}]}
    toast = {"name": "toast", "kind": "row", "anchor": "top", "offset": [0, 10], "padding": [10, 18], "radius": 20, "background": "panel",
             "border_color": "border", "visible_if": "time < msg_until", "children": [{"kind": "text", "text": "{msg}", "weight": "medium"}]}
    prompt = {"name": "deadmsg", "kind": "panel", "anchor": "center", "visible_if": "!alive('player')", "children": [
        {"kind": "text", "text": "You fainted...", "size": "xl", "weight": "bold", "align": "center", "h_align": "center"}]}

    equipment = {"kind": "grid", "columns": 2, "gap": 8, "children": [
        {"name": f"eq_{s}", "kind": "button", "width": 150, "height": 66, "padding": [8, 10], "tooltip": f"{{eq_{s} == '' ? '' : tips[eq_{s}]}}",
         "style_if": [{"if": f"eq_{s} != '' && db[eq_{s}].rarity == '{r}'", "border_color": r} for r in ["legendary", "epic", "rare"]],
         "children": [{"kind": "row", "gap": 8, "children": [
             item_icon(f"eq_{s}", 0.8) | {"h_align": "start"},
             {"kind": "column", "gap": 2, "grow": 1, "children": [
                 {"kind": "text", "text": s.upper(), "size": 10, "weight": "bold", "color": "muted"},
                 {"kind": "text", "text": f"{{eq_{s} == '' ? 'Empty' : db[eq_{s}].name}}", "size": "sm", "weight": "medium", "wrap": True,
                  "style_if": [{"if": f"eq_{s} == ''", "color": "subtle"}]}]}]}]}
        for s in SLOTS]}

    def stat_row(key, label, desc):
        return {"kind": "row", "gap": 8, "tooltip": desc, "children": [
            {"kind": "text", "text": label, "weight": "bold", "color": "muted", "width": 44},
            {"kind": "text", "text": f"{{{key}}}", "weight": "bold", "grow": 1},
            {"name": f"plus_{key}", "kind": "button", "variant": "primary", "text": "+", "width": 28, "height": 26, "padding": 0, "weight": "black",
             "visible_if": "points > 0"}]}

    derived = {"kind": "column", "gap": 4, "padding": 12, "radius": 10, "background": "surface", "children": [
        {"kind": "row", "children": [{"kind": "text", "text": "Attack", "color": "muted", "size": "sm"}, {"kind": "spacer", "grow": 1},
                                     {"kind": "text", "text": "{atk}", "weight": "bold", "size": "sm"}]},
        {"kind": "row", "children": [{"kind": "text", "text": "Block", "color": "muted", "size": "sm"}, {"kind": "spacer", "grow": 1},
                                     {"kind": "text", "text": "{round(defense * 100 / (defense + 20))}%", "weight": "bold", "size": "sm"}]},
        {"kind": "row", "children": [{"kind": "text", "text": "Max HP", "color": "muted", "size": "sm"}, {"kind": "spacer", "grow": 1},
                                     {"kind": "text", "text": "{max_hp}", "weight": "bold", "size": "sm"}]},
        {"kind": "row", "children": [{"kind": "text", "text": "Speed", "color": "muted", "size": "sm"}, {"kind": "spacer", "grow": 1},
                                     {"kind": "text", "text": "{speed}", "weight": "bold", "size": "sm"}]}]}

    left = {"kind": "column", "width": 308, "gap": 12, "children": [
        section("EQUIPMENT"), equipment,
        section("STATS", {"kind": "text", "text": "{points} points", "size": "xs", "weight": "bold", "color": "accent", "visible_if": "points > 0"}),
        stat_row("str", "STR", "Strength: +2 attack per point"),
        stat_row("def", "DEF", "Defense: blocks more damage"),
        stat_row("vit", "VIT", "Vitality: +12 max HP per point"),
        stat_row("agi", "AGI", "Agility: +15 move speed per point"),
        derived]}

    right = {"kind": "column", "gap": 12, "children": [
        section("INVENTORY", {"kind": "text", "text": "{12 - count(bag, '')} / 12", "size": "xs", "weight": "bold", "color": "muted"}),
        {"kind": "grid", "columns": 4, "gap": 8, "children": [item_slot("slot", 94) | {"repeat": BAG, "as": "i"}]},
        {"kind": "row", "gap": 8, "children": [
            {"name": "mode_use", "kind": "button", "text": "Use / Equip", "size": "sm", "variant": "ghost",
             "style_if": [{"if": "mode == 'use'", "background": "accent", "color": "accent_text"}]},
            {"name": "mode_discard", "kind": "button", "text": "Discard", "size": "sm", "variant": "ghost",
             "style_if": [{"if": "mode == 'discard'", "background": "danger", "color": "white"}]},
            {"kind": "spacer", "grow": 1},
            {"kind": "text", "text": "{mode == 'use' ? 'Click an item to use it' : 'Click an item to throw it away'}", "size": "xs", "color": "muted"}]}]}

    window = {"name": "character", "kind": "panel", "anchor": "center", "offset": [0, -20], "padding": 22, "gap": 16, "visible_if": "show_inv", "children": [
        {"kind": "row", "gap": 10, "children": [
            {"kind": "text", "text": "Character", "size": "xl", "weight": "bold"},
            {"kind": "text", "text": "Level {level}", "size": "sm", "color": "muted", "v_align": "end", "margin": [0, 0, 3, 0]},
            {"kind": "spacer", "grow": 1},
            {"kind": "text", "text": "{gold} gold", "weight": "bold", "color": "gold"},
            {"name": "close_inv", "kind": "button", "text": "Close", "size": "sm", "variant": "ghost"}]},
        {"kind": "divider"},
        {"kind": "row", "gap": 22, "align": "start", "children": [left, {"kind": "divider"}, right]}]}

    shop = {"name": "shop", "kind": "panel", "anchor": "center", "width": 460, "gap": 14, "visible_if": "show_shop", "children": [
        {"kind": "row", "children": [
            {"kind": "column", "gap": 2, "children": [{"kind": "text", "text": "Merchant", "size": "xl", "weight": "bold"},
                                                      {"kind": "text", "text": "Potions, steel and fair prices.", "size": "sm", "color": "muted"}]},
            {"kind": "spacer", "grow": 1},
            {"kind": "text", "text": "{gold} gold", "weight": "bold", "color": "gold"}]},
        {"kind": "divider"},
        {"kind": "column", "gap": 6, "children": [
            {"kind": "row", "gap": 12, "padding": [8, 12], "radius": 10, "background": "surface", "repeat": len(SHOP), "as": "k",
             "tooltip": "{tips[shop[k]]}", "children": [
                item_icon("shop[k]", 0.85),
                {"kind": "column", "grow": 1, "gap": 1, "children": [
                    {"kind": "text", "text": "{db[shop[k]].name}", "weight": "medium",
                     "style_if": [{"if": f"db[shop[k]].rarity == '{r}'", "color": r} for r in ["legendary", "epic", "rare"]]},
                    {"kind": "text", "text": "{db[shop[k]].rarity}", "size": "xs", "color": "muted"}]},
                {"name": "buy", "kind": "button", "variant": "primary", "text": "{db[shop[k]].value * 2} g", "size": "sm", "width": 76,
                 "disabled_if": "gold < db[shop[k]].value * 2"}]}]},
        {"kind": "row", "gap": 8, "children": [
            {"name": "sell_gems", "kind": "button", "text": "Sell gems", "variant": "success", "disabled_if": "!contains(bag, 'gem')"},
            {"kind": "spacer", "grow": 1},
            {"name": "close_shop", "kind": "button", "text": "Leave", "variant": "ghost"}]}]}
    return [hud, hotbar, toast, prompt, window, shop]


# ---- rules ---------------------------------------------------------------------------------------------------------

def build_rules():
    rules = []
    rules.append({"name": "add_item", "on": "event", "filter": "add_item", "actions": [
        {"let": "id", "value": "event.id"},
        {"let": "s", "value": "index_of(bag, id)"},
        {"if": "s >= 0 && bag_n[s] < db[id].stack", "then": [{"set_item": "bag_n", "index": "s", "value": "bag_n[s] + 1"}] + msg("'+ ' + db[id].name"),
         "else": [{"let": "e", "value": "index_of(bag, '')"},
                  {"if": "e < 0", "then": msg("'Your bag is full'"),
                   "else": [{"set_item": "bag", "index": "e", "value": "id"}, {"set_item": "bag_n", "index": "e", "value": "1"}] + msg("'+ ' + db[id].name")}]}]})
    for iid in ITEMS:
        rules.append({"name": f"pick_{iid}", "on": "touch", "target": f"tag:item_{iid}", "actions": [
            {"release": "event.target"}, {"feedback": "pickup"}, {"emit": "add_item", "data": {"id": f"='{iid}'"}}]})
    rules.append({"name": "pick_gold", "on": "touch", "target": "tag:item_gold", "actions": [
        {"release": "event.target"}, {"feedback": "pickup"}, {"let": "g", "value": "rand_int(5, 15)"}, {"set": "gold", "value": "gold + g"}]
        + msg("'+ ' + str(g) + ' gold'")})

    def consume():
        return [{"set_item": "bag_n", "index": "i", "value": "bag_n[i] - 1"}, {"if": "bag_n[i] <= 0", "then": [{"set_item": "bag", "index": "i", "value": "''"}]}]
    use = [{"let": "i", "value": "event.i"}, {"let": "id", "value": "bag[i]"},
           {"if": "id != '' && mode == 'discard'", "then": consume() + msg("'Discarded ' + db[id].name")},
           {"if": "id != '' && mode == 'use' && db[id].type == 'potion'", "then": [{"heal": "'player'", "amount": "db[id].heal"}, {"feedback": "drink"}]
            + consume() + msg("'Drank ' + db[id].name")},
           {"if": "id != '' && mode == 'use' && db[id].type == 'gem'", "then": msg("'Sell gems to the merchant'")}]
    for s in SLOTS:
        use.append({"if": f"id != '' && mode == 'use' && db[id].type == '{s}'", "then": [
            {"let": "old", "value": f"eq_{s}"}, {"set": f"eq_{s}", "value": "id"},
            {"set_item": "bag", "index": "i", "value": "old"}, {"set_item": "bag_n", "index": "i", "value": "old == '' ? 0 : 1"},
            {"emit": "recalc"}] + msg("'Equipped ' + db[id].name")})
    rules.append({"name": "use_slot", "on": "event", "filter": "use_slot", "actions": use})
    rules.append({"name": "slot", "on": "button", "filter": "slot", "actions": [{"emit": "use_slot", "data": {"i": "=event.i"}}]})
    for k, key in enumerate(["One", "Two", "Three", "Four"]):
        rules.append({"name": f"hot{k}", "on": "key", "key": key, "actions": [{"emit": "use_slot", "data": {"i": f"={k}"}}]})
    for s in SLOTS:
        rules.append({"name": f"unequip_{s}", "on": "button", "filter": f"eq_{s}", "condition": f"eq_{s} != ''", "actions": [
            {"if": "index_of(bag, '') < 0", "then": msg("'No room in your bag'"),
             "else": [{"emit": "add_item", "data": {"id": f"=eq_{s}"}}, {"set": f"eq_{s}", "value": "''"}, {"emit": "recalc"}]}]})
    rules.append({"name": "recalc", "on": "event", "filter": "recalc", "actions": [
        {"set": "atk", "value": f"2 + str * 2 + {eq_sum('atk')}"},
        {"set": "defense", "value": f"def + {eq_sum('def')}"},
        {"set": "max_hp", "value": f"60 + vit * 12 + {eq_sum('hp')}"},
        {"set": "speed", "value": f"max(250, 460 + agi * 15 + {eq_sum('spd')})"},
        {"health": "'player'", "max": "max_hp", "value": "min(health.player, max_hp)"},
        {"weapon": "bolt", "set": {"damage": "=atk"}},
        {"player": "set", "fields": {"speed": "=speed"}}]})
    start = [{"emit": "add_item", "data": {"id": f"='{i}'"}} for i in ["wood_sword", "cap", "potion", "potion"]]
    start += [{"emit": "recalc"}, {"health": "'player'", "max": "max_hp", "value": "max_hp"}]
    for iid, x, y in [("potion", 700, 300), ("leather_armor", -900, 2000), ("gem", 1800, 400), ("ring_swift", -2900, -2900), ("potion", -1600, 600),
                      ("gem", 2900, -400), ("iron_sword", 2900, 2900), ("gem", -400, -1200)]:
        start += [{"spawn": f"p_{iid}", "at": f"[{x}, {y}, 0]", "as": "p"},
                  {"tween": "p.id", "move": [0, 0, 35], "duration": 0.9, "loop": "pingpong", "ease": "in_out_sine"}]
    for x, y in [(400, -600), (-400, 900), (1000, 2600), (-2400, -1000)]:
        start += [{"spawn": "p_gold", "at": f"[{x}, {y}, 0]", "as": "p"},
                  {"tween": "p.id", "move": [0, 0, 25], "duration": 0.8, "loop": "pingpong", "ease": "in_out_sine"}]
    rules.append({"name": "start", "on": "start", "actions": start + msg("'Explore, loot chests and level up  -  I opens your character'")})
    rules.append({"name": "armor", "on": "damaged", "target": "player", "condition": "defense > 0", "actions": [
        {"heal": "'player'", "amount": "round(event.amount * defense / (defense + 20))"}]})
    rules.append({"name": "bite", "on": "timer", "filter": "bite", "actions": [
        {"for": "e", "in": "nodes('tag:slime')", "do": [
            {"if": "alive(e) && dist(pos(e), pos('player')) < 190", "then": [{"damage": "'player'", "amount": "8 + level * 2", "type": "bite"}]}]}]})
    rules.append({"name": "bite_timer", "on": "start", "actions": [{"timer": "bite", "delay": "0.8", "repeat": True}]})
    rules.append({"name": "level_up", "on": "var_changed", "filter": "xp", "condition": "xp >= xp_next", "actions": [
        {"set": "xp", "value": "xp - xp_next"}, {"set": "level", "value": "level + 1"}, {"set": "points", "value": "points + 3"},
        {"set": "xp_next", "value": "floor(xp_next * 1.5)"}, {"emit": "recalc"}, {"heal": "'player'", "amount": "9999"}, {"feedback": "levelup"}]
        + msg("'Level up! You are now level ' + str(level)")})
    for st in ["str", "def", "vit", "agi"]:
        rules.append({"name": f"plus_{st}", "on": "button", "filter": f"plus_{st}", "condition": "points > 0", "actions": [
            {"set": st, "value": f"{st} + 1"}, {"set": "points", "value": "points - 1"}, {"emit": "recalc"}]})
    for c in ["chest_0", "chest_1", "chest_2"]:
        rules.append({"name": f"open_{c}", "on": "interact", "target": c, "actions": [
            {"tween": "event.target", "scale": [1.12, 1.12, 1.3], "duration": 0.15, "loop": "yoyo"}, {"feedback": "chest"},
            {"emit": "add_item", "data": {"id": f"=pick({json.dumps(CHEST_LOOT)})"}},
            {"emit": "add_item", "data": {"id": f"=pick({json.dumps(CHEST_LOOT)})"}},
            {"set": "gold", "value": "gold + rand_int(20, 40)"}]})
    rules.append({"name": "shop_open", "on": "interact", "target": "merchant", "actions": [{"set": "show_shop", "value": "true"}, {"set": "show_inv", "value": "false"}]})
    rules.append({"name": "buy", "on": "button", "filter": "buy", "actions": [
        {"let": "id", "value": "shop[event.k]"}, {"let": "price", "value": "db[id].value * 2"},
        {"if": "gold >= price && (index_of(bag, '') >= 0 || (index_of(bag, id) >= 0 && bag_n[index_of(bag, id)] < db[id].stack))",
         "then": [{"set": "gold", "value": "gold - price"}, {"emit": "add_item", "data": {"id": "=id"}}],
         "else": msg("gold < price ? 'Not enough gold' : 'Your bag is full'")}]})
    rules.append({"name": "sell_gems", "on": "button", "filter": "sell_gems", "actions": [
        {"let": "earned", "value": "0"},
        {"for": "i", "in": f"range({BAG})", "do": [{"if": "bag[i] == 'gem'", "then": [
            {"let": "earned", "value": "earned + bag_n[i] * db.gem.value"}, {"set_item": "bag", "index": "i", "value": "''"}, {"set_item": "bag_n", "index": "i", "value": "0"}]}]},
        {"set": "gold", "value": "gold + earned"}] + msg("'Sold gems for ' + str(earned) + ' gold'")})
    rules.append({"name": "close_shop", "on": "button", "filter": "close_shop", "actions": [{"set": "show_shop", "value": "false"}]})
    rules.append({"name": "close_inv", "on": "button", "filter": "close_inv", "actions": [{"set": "show_inv", "value": "false"}]})
    for key in ["I", "Tab"]:
        rules.append({"name": f"toggle_{key}", "on": "key", "key": key, "actions": [{"set": "show_inv", "value": "!show_inv"}, {"set": "show_shop", "value": "false"}]})
    rules.append({"name": "escape", "on": "key", "key": "Escape", "actions": [{"set": "show_inv", "value": "false"}, {"set": "show_shop", "value": "false"}]})
    rules.append({"name": "mode_use", "on": "button", "filter": "mode_use", "actions": [{"set": "mode", "value": "'use'"}]})
    rules.append({"name": "mode_discard", "on": "button", "filter": "mode_discard", "actions": [{"set": "mode", "value": "'discard'"}]})
    saved = ["bag", "bag_n"] + [f"eq_{s}" for s in SLOTS] + ["str", "def", "vit", "agi", "points", "level", "xp", "xp_next", "gold"]
    rules.append({"name": "save", "on": "key", "key": "F5", "actions": [{"save": "rpg", "vars": saved}] + msg("'Game saved'")})
    rules.append({"name": "load", "on": "key", "key": "F9", "actions": [{"load": "rpg"}, {"emit": "recalc"}] + msg("'Game loaded'")})
    return rules


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8018/mcp")
    ap.add_argument("--clear", action="store_true")
    ap.add_argument("--out", default=None)
    ap.add_argument("--open", action="store_true", help="start with the character window open")
    args = ap.parse_args()
    rt = Runtime(args.url)
    if args.clear:
        rt.call("logic.stop")
        rt.call("logic.remove_player")
        rt.call("scene.clear_all", confirm=True)

    build_world(rt)
    build_templates(rt)
    rt.call("logic.set_solid", targets=["tag:pickup"], solid=False)

    rt.call("logic.define_vars", vars={
        "db": {k: dict(v, hex=to_hex(v["color"])) for k, v in ITEMS.items()}, "tips": {k: tooltip(v) for k, v in ITEMS.items()}, "shop": SHOP,
        "bag": [""] * BAG, "bag_n": [0] * BAG,
        **{f"eq_{s}": "" for s in SLOTS},
        "str": 3, "def": 2, "vit": 3, "agi": 2, "points": 0,
        "level": 1, "xp": 0, "xp_next": 100, "gold": 25,
        "atk": 0, "defense": 0, "max_hp": 100, "speed": 500,
        "show_inv": args.open, "show_shop": False, "mode": "use", "msg": "", "msg_until": 0,
    })

    rt.call("logic.define_feedback", name="hurt", steps=[{"type": "shake", "duration": 0.18, "intensity": 0.6}, {"type": "flash", "intensity": 0.35}])
    rt.call("logic.define_feedback", name="pickup", steps=[{"type": "shake", "duration": 0.06, "intensity": 0.15}])
    rt.call("logic.define_feedback", name="drink", steps=[{"type": "flash", "intensity": 0.2, "color": [0.3, 1, 0.4]}])
    rt.call("logic.define_feedback", name="chest", steps=[{"type": "shake", "duration": 0.12, "intensity": 0.3}])
    rt.call("logic.define_feedback", name="levelup", steps=[{"type": "flash", "intensity": 0.45, "color": [1, 0.8, 0.3]}, {"type": "slowmo", "duration": 0.35, "scale": 0.4},
                                                            {"type": "text", "text": "LEVEL UP!", "color": [1, 0.8, 0.3]}])

    rt.call("logic.spawn_player", mode="topdown", location=[0, 0, 40], facing_controls=True, speed=500, camera_distance=1900, camera_pitch=58,
            dash={"speed": 1800, "duration": 0.18, "cooldown": 0.8, "invulnerable": True})
    rt.call("logic.define_health", targets=["player"], max=100, invulnerable=0.6, hit_feedback="hurt")
    rt.call("logic.define_weapon", name="bolt", damage=5, rate=3, speed=2600, lifetime=1.2, spread=2, hits="tag:slime",
            bullet={"size": 22, "color": [1.0, 0.85, 0.3]})
    rt.call("logic.equip", owner="player", weapon="bolt")
    rt.call("logic.define_loot", name="slime_drop", entries=[{"pool": "p_gold", "weight": 4}, {"pool": "p_potion", "weight": 3},
                                                              {"pool": "p_gem", "weight": 2}, {"pool": "none", "weight": 2}], count=[1, 2])
    rt.call("logic.define_health", targets=["tag:slime"], max=20, on_death="Release", loot="slime_drop", points_var="xp", points=35)
    rt.call("logic.define_brain", name="slime", backend="ai", vision={"radius": 1100, "angle": 140}, hearing={"radius": 1400}, start="wander", states=[
        {"name": "wander", "move": "wander", "radius": 500, "speed": 140, "transitions": [{"when": "sees", "to": "chase"}, {"when": "heard", "to": "hunt"}]},
        {"name": "hunt", "move": "to_last_known", "speed": 260, "icon": "?", "transitions": [{"when": "sees", "to": "chase"}, {"when": "at_goal || time_in_state > 6", "to": "wander"}]},
        {"name": "chase", "move": "chase", "speed": 330, "keep_distance": 90, "face": "target", "icon": "!",
         "transitions": [{"when": "!player_alive", "to": "wander"}, {"when": "!sees && since_seen > 3", "to": "hunt"}]},
    ])
    rt.call("logic.assign_brain", targets=["tag:slime"], brain="slime")
    rt.call("logic.define_spawner", name="slimes", pool="slimes", points=[[-2500, -500, 0], [2500, 900, 0], [-400, 2700, 0], [800, -2800, 0], [2700, -2700, 0]],
            interval=[3, 6], max_alive=5, start_delay=2)
    rt.call("logic.set_hud", enemy_bars=True, damage_numbers=True, hit_flash=True, low_health=True, alert_icons=True)
    rt.call("logic.define_interactable", targets=["chest_0", "chest_1", "chest_2"], prompt="Open chest", once=True)
    rt.call("logic.define_interactable", targets=["merchant"], prompt="Trade", radius=320)

    rt.call("logic.define_rules", rules=build_rules(), replace=True)
    rt.call("logic.define_ui", items=build_ui(), theme=THEME, replace=True)

    rt.call("lighting.apply_mood", mood="morning")
    nav = rt.call("ai.build_navigation")
    print("navigation:", nav.get("has_nav_mesh"))
    rt.call("logic.start")
    st = rt.call("logic.get_state", log_limit=20)
    errors = [e for e in st.get("log", []) if e.get("errors")]
    print("vars:", {k: st["vars"][k] for k in ("bag", "bag_n", "atk", "defense", "max_hp", "speed", "gold")})
    print("errors:", json.dumps(errors)[:1500])
    if args.out:
        rt.call("ops.wait", seconds=1.5)
        rt.screenshot(args.out, show_ui=True)
        print("saved", args.out)


if __name__ == "__main__":
    main()
