"""Style recipes for facades.py: what a building of each place is made of and how it varies.

Each recipe answers: how wide and tall are buildings, which piece goes in each bay of each storey, what colours
vary between buildings, what the hidden shell (back, sides, roof fill) is, and how the street is dressed.
Copy one to make a new place; heights are cm above the pavement.
"""

A_PARIS_DOORS = [(0.03, 0.08, 0.05), (0.02, 0.03, 0.07), (0.14, 0.03, 0.03), (0.02, 0.02, 0.02), (0.16, 0.09, 0.05)]
A_PARIS_SHOPS = [(0.02, 0.05, 0.08), (0.12, 0.02, 0.03), (0.02, 0.06, 0.04), (0.02, 0.02, 0.02), (0.25, 0.2, 0.12), (0.08, 0.1, 0.12)]


def _paris_house(rng, bays=None):
    n = bays or rng.choice((3, 4, 4, 5, 5, 6))
    return {"bays": n, "door": rng.randrange(n) if n < 3 else rng.randrange(1, n - 1), "shops": rng.random() < 0.8,
            "pediments": rng.random() < 0.5, "dormers": rng.choice(("all", "alternate")), "paint_a": rng.choice(A_PARIS_SHOPS),
            "paint_b": (1, 1, 1), "plain_upper": rng.random() < 0.5}


def _paris_column(h, b, rng):
    if b == h["door"]:
        base = "Base_Door"
    elif h["shops"] and rng.random() < 0.85:
        base = "Base_Shop"
    else:
        base = "Base_Window"
    noble = "Noble_Pediment" if h["pediments"] and b % 2 == 0 else "Noble"
    dormer = h["dormers"] == "all" or b % 2 == 0
    return [(base, 0), (noble, 640), ("Upper", 980), ("Upper_Plain" if h["plain_upper"] else "Upper", 1290), ("Top", 1600), ("Cornice", 1890),
            ("Mansard_Dormer" if dormer else "Mansard", 1945)]


def _paris_shell(h):
    L, d = h["bays"] * 250.0, 1300.0
    out = [("wall", 2, L - 2, d - 40, d, 0, 1945), ("roof", 0, L, 600, d, 1945, 2315)]   # back wall, roof behind the mansards
    if h.get("first"):
        out.append(("wall", 2, 40, 50, d, 0, 2315))
    if h.get("last"):
        out.append(("wall", L - 40, L - 2, 50, d, 0, 2315))
    return out


def _paris_chimneys(h, rng):
    return [("Chimney", 0.0, 120.0, 2315), ("Chimney", h["bays"] * 250.0, 120.0, 2315)]


PARIS = {
    "name": "paris", "path": "/Game/RuntimeArchitecture/Paris/SM_PA_", "bay": 250.0, "wall": 50.0, "depth": 1300.0,
    "house": _paris_house, "column": _paris_column, "shell": _paris_shell, "chimneys": _paris_chimneys,
    "street": {"pavement": 450.0, "kerb": 25.0, "road": 1400.0, "lamp": "street_lamp", "lamp_spacing": 2200.0,
               "trees": ["maple"], "tree_spacing": 1100.0, "tree_scale": (0.5, 0.62)},
}

# ------------------------------------------------------------------------------------------------ Mediterranean

B_WALLS = [(0.78, 0.45, 0.16), (0.7, 0.3, 0.16), (0.88, 0.68, 0.26), (0.82, 0.46, 0.4), (0.92, 0.9, 0.84), (0.86, 0.52, 0.3), (0.62, 0.32, 0.18),
           (0.9, 0.78, 0.5), (0.5, 0.62, 0.72), (0.76, 0.6, 0.38)]   # ochre, terracotta, yellow, rose, white, salmon, sienna, cream, blue, sand
A_SHUTTERS = [(0.05, 0.2, 0.1), (0.03, 0.12, 0.06), (0.2, 0.1, 0.05), (0.15, 0.25, 0.32), (0.04, 0.18, 0.18), (0.55, 0.55, 0.5), (0.3, 0.12, 0.08)]
HG, HU = 320.0, 290.0


def _med_house(rng, bays=None):
    n = bays or rng.choice((1, 2, 2, 3, 3))
    floors = rng.choice((2, 3, 3, 4))
    return {"bays": n, "floors": floors, "door": rng.randrange(n), "flat": rng.random() < 0.25, "paint_a": rng.choice(A_SHUTTERS),
            "paint_b": rng.choice(B_WALLS), "shop": rng.random() < 0.35}


def _med_column(h, b, rng):
    if b == h["door"]:
        g = "Ground_Door"
    elif h["shop"] and rng.random() < 0.7:
        g = "Ground_Shop"
    else:
        g = "Ground_Window"
    out = [(g, 0)]
    for f in range(1, h["floors"]):
        r = rng.random()
        out.append(("Upper_Balcony" if r < 0.3 else "Upper_Closed" if r < 0.5 else "Upper_Window", HG + (f - 1) * HU))
    top = HG + (h["floors"] - 1) * HU
    out.append(("Parapet" if h["flat"] else "Eave", top))
    return out


def _med_top(h):
    return HG + (h["floors"] - 1) * HU


def _med_shell(h):
    L, d, top = h["bays"] * 220.0, 900.0, _med_top(h)
    ridge = top + (0 if h["flat"] else 140 + 12)
    # side walls rise to the ridge (a fire wall where the neighbour is lower); inset so they never touch the facade
    boxes = [("wall", 2, L - 2, d - 40, d, 0, top), ("wall", 2, 30, 52, d, 0, ridge + 15), ("wall", L - 30, L - 2, 52, d, 0, ridge + 15)]
    if h["flat"]:
        boxes.append(("trim", 0, L, 30, d, top - 10, top + 2))           # roof terrace
    else:
        boxes.append(("roof", 0, L, 450, d - 40, top, top + 120))        # back pitch filler under the ridge
    return boxes


def _med_chimneys(h, rng):
    if rng.random() < 0.6:
        top = _med_top(h)
        return [("Chimney", rng.uniform(40, h["bays"] * 220.0 - 40), 600.0, top + (0 if h["flat"] else 90))]
    return []


MEDITERRANEAN = {
    "name": "mediterranean", "path": "/Game/RuntimeArchitecture/Mediterranean/SM_ME_", "bay": 220.0, "wall": 50.0, "depth": 900.0,
    "house": _med_house, "column": _med_column, "shell": _med_shell, "chimneys": _med_chimneys,
    "street": {"pavement": 120.0, "kerb": 15.0, "road": 500.0, "lamp": None, "lamp_spacing": 2000.0, "trees": None, "tree_spacing": 0},
}

STYLES = {"paris": PARIS, "mediterranean": MEDITERRANEAN}
