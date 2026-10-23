"""FluidNinja LIVE's sea-coast demo beach, built in the game with one tool call.

    python sea_coast.py            # needs the game running with the FluidNinja LIVE pack

ops.run_recipe "sea_coast" runs a stored batch of ordinary tool calls (made by Scripts/recipes/make_recipes.py from the
harvested demo level, Scripts/kit_examples/sea_coast_scene.json): the sculpted coast ground, its sun / sky / fog / grading,
59 rocks, wrecks and mountains, a third-person character, the shore water and wet sand that travel with it, and floating
barrels. Then walk with WASD + mouse, or change anything with the other tools.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import unreal_kit as uk

if __name__ == "__main__":
    uk.connect()
    r = uk.call("ops.run_recipe", name="sea_coast")   # skip=["props"] for a bare beach
    bad = [x for x in r["results"] if not x.get("ok")]
    print(f"{r['completed']}/{r['total']} calls, {len(bad)} failed")
    for b in bad:
        print(" ", b["i"], b["tool"], b.get("error"))
