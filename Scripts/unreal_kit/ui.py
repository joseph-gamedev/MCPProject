"""Game UI as layout trees (panel / row / column / grid / text / button / bar), like small HTML pages.

    from unreal_kit import ui
    ui.define([
        ui.hud("hud", [("GOLD", "{gold}"), ("HP", "{health.player}")]),
        ui.panel("shop", [ui.title("Shop"), ui.row([ui.button("buy_sword", "Sword - 50g", disabled_if="gold < 50")])],
                 anchor="center", visible_if="shop_open"),
    ], theme=ui.THEMES["fantasy"])

Text can interpolate expressions: "Score {score}". visible_if / disabled_if / style_if take expressions.
Buttons fire rules {"on": "button", "filter": <button name>}. Anchors: top-left, top, top-right, left, center, right,
bottom-left, bottom, bottom-right. Colour tokens: text, muted, subtle, accent, panel, surface, border, success, warning, danger, info, gold, white, black
(or "#rrggbb").
"""

from . import tools

THEMES = {
    "arcade": {"colors": {"accent": "#FFD23F", "success": "#3BCEAC", "danger": "#EE4266", "panel": "#0E0E1A", "surface": "#1C1C2E", "text": "#F4F4F8"},
               "radius": 4, "font_size": 18, "text_shadow": True},
    "fantasy": {"colors": {"accent": "#E8B04B", "success": "#7FB069", "danger": "#C8553D", "panel": "#2B2118", "surface": "#3A2D21", "text": "#F3E9D2"},
                "radius": 10, "font_size": 16, "text_shadow": True},
    "scifi": {"colors": {"accent": "#38BDF8", "info": "#A78BFA", "danger": "#F43F5E", "panel": "#0B1220", "surface": "#13203A", "text": "#E2E8F0"},
              "radius": 2, "font_size": 15},
    "clean": {"colors": {"accent": "#2563EB", "success": "#10B981", "danger": "#DC2626", "panel": "#FFFFFF", "surface": "#F3F4F6", "text": "#111827",
                         "muted": "#6B7280"}, "radius": 12, "font_size": 15},
}


def _item(kind, name=None, **fields):
    d = {"kind": kind}
    if name:
        d["name"] = name
    d.update({k: v for k, v in fields.items() if v is not None})
    return d


def text(value, size=None, weight=None, color=None, align=None, name=None, **kw):
    return _item("text", name, text=value, size=size, weight=weight, color=color, align=align, **kw)


def title(value, size="xl", **kw):
    return text(value, size=size, weight="bold", **kw)


def button(name, label, variant="primary", disabled_if=None, tooltip=None, **kw):
    return _item("button", name, text=label, variant=variant, disabled_if=disabled_if, tooltip=tooltip, **kw)


def bar(value, max_, fill="success", width=220, height=14, label=None, **kw):
    return _item("bar", None, value=str(value), max=str(max_), fill=fill, size=[width, height], text=label, **kw)


def row(children, gap=8, align=None, justify=None, **kw):
    return _item("row", None, children=list(children), gap=gap, align=align, justify=justify, **kw)


def column(children, gap=6, align=None, **kw):
    return _item("column", None, children=list(children), gap=gap, align=align, **kw)


def grid(children, columns=3, gap=8, cell=None, **kw):
    """cell = [width, height] gives every cell the same size."""
    return _item("grid", None, children=list(children), columns=columns, gap=gap, cell=cell, **kw)


def repeat(item, count, as_="i"):
    """Copies a child item count times (a number or an expression like "len(bag)"); inside it the index is a
    local named as_ ("{bag[i].name}"). A repeated button reports it in event.data (event.data.i)."""
    d = dict(item)
    d["repeat"] = count
    d["as"] = as_
    return d


def spacer(size=8):
    return _item("spacer", None, height=size, width=size)


def divider():
    return _item("divider")


def panel(name, children, anchor="center", visible_if=None, width=None, padding=16, gap=10, **kw):
    """A top-level window/card. Every screen is a panel with a name."""
    return _item("panel", name, anchor=anchor, children=list(children), visible_if=visible_if, min_width=width, padding=padding, gap=gap, **kw)


def hud(name, stats, corner="top-left", title=None, bars=()):
    """Corner HUD: stats = [(label, "{expr}")], bars = [(label, value_expr, max_expr, fill)]."""
    kids = [text(title, weight="bold", color="accent")] if title else []
    kids += [row([text(label, size="sm", color="muted"), text(value, weight="bold")], gap=6) for label, value in stats]
    kids += [column([text(label, size="xs", color="muted"), bar(v, m, fill)], gap=2) for label, v, m, fill in bars]
    return panel(name, kids, anchor=corner, padding=10, gap=4)


def banner(name, text_value, visible_if=None, anchor="top"):
    """Big centred message (READY, WAVE 2, OUCH!)."""
    return _item("panel", name, anchor=anchor, visible_if=visible_if, padding=14, offset=[0, 80],
                 children=[text(text_value, size="xl", weight="black", color="accent")])


def panel_message(name, heading, body=None, button=None, visible_if=None):
    """Centred dialog: heading, optional body text and one button (name, label)."""
    kids = [title(heading, align="center")]
    if body:
        kids.append(text(body, color="muted", align="center"))
    if button:
        kids += [spacer(6), _item("button", button[0], text=button[1], variant="primary")]
    return panel(name, kids, anchor="center", visible_if=visible_if, width=360, padding=24, align="center")



def menu(name, heading, buttons, visible_if=None, subtitle=None):
    """Title screen / pause menu: buttons = [(name, label)] (first is primary)."""
    kids = [title(heading, size="xl", align="center")]
    if subtitle:
        kids.append(text(subtitle, color="muted", align="center"))
    kids.append(spacer(10))
    kids += [_item("button", n, text=l, variant="primary" if i == 0 else "ghost") for i, (n, l) in enumerate(buttons)]
    return panel(name, kids, anchor="center", visible_if=visible_if, width=320, padding=28, gap=10, align="stretch")


def inventory(name, list_var, columns=5, visible_if=None, heading="Inventory", slot_text=None):
    """Grid of slots, one per element of a list variable; clicking fires button '<name>_slot' with event.data.i."""
    slot = _item("button", f"{name}_slot", text=slot_text or f"{{{list_var}[i]}}", variant="ghost")
    return panel(name, [title(heading, size="lg"), grid([repeat(slot, f"len({list_var})")], columns=columns, cell=[88, 88])],
                 visible_if=visible_if)


def define(items, theme=None, replace=False):
    """Creates or updates UI items (merged by name). theme: dict or a THEMES key."""
    args = {"items": list(items), "replace": replace}
    if theme:
        args["theme"] = THEMES.get(theme, theme) if isinstance(theme, str) else theme
    return tools.logic.define_ui(**args)


def remove(names):
    return tools.logic.remove_ui(names=list(names))
