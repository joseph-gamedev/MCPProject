"""
A complete, playable tic-tac-toe made only from tool calls (what an agent does from a prompt): board, pieces,
rules, HUD, restart button, R to restart, optional computer opponent. After it runs the game plays by itself;
the script/agent can disconnect.

    python Scripts/examples/tictactoe.py [--ai] [--demo] [--out shot.jpg] [--clear]
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from rtbuild import Runtime, box, mat  # noqa: E402

CELL = 300


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8018/mcp")
    ap.add_argument("--ai", action="store_true", help="play X against the computer")
    ap.add_argument("--demo", action="store_true", help="play a few moves to show the board")
    ap.add_argument("--out", default=None)
    ap.add_argument("--clear", action="store_true")
    args = ap.parse_args()
    rt = Runtime(args.url)
    if args.clear:
        rt.call("logic.stop")
        rt.call("scene.clear_all", confirm=True)

    # ---- board
    root = rt.group("TicTacToe")
    wood = mat("wood", color=[0.22, 0.12, 0.06], tile=24)
    frame = [box([1060, 1060, 40], at=[0, 0, 0], bevel=8, **wood)]
    for k in (-0.5, 0.5):
        frame.append(box([20, 920, 30], at=[k * CELL, 0, 40], **mat("black", color=[0.05, 0.04, 0.03])))
        frame.append(box([920, 20, 30], at=[0, k * CELL, 40], **mat("black", color=[0.05, 0.04, 0.03])))
    rt.build("Board", frame, parent=root)
    positions = []
    for i in range(9):
        x, y = (1 - i % 3) * CELL, (1 - i // 3) * CELL  # camera looks along +Y, so screen-left is +X
        positions.append([x, y, 80])
        cell = rt.call("build.create_primitive", shape="box", size=[CELL - 30, CELL - 30, 6], location=[x, y, 40], name=f"cell_{i}",
                       tags=["cell"], parent=root, material="plaster", color=[0.8, 0.74, 0.6])["id"]
        rt.call("logic.set_data", target=cell, data={"index": i})

    # ---- game definition
    rt.call("logic.define_vars", replace=True, vars={
        "board": [""] * 9, "turn": "X", "winner": "", "moves": 0, "pos": positions, "lines": [], "ai": args.ai,
        "score_x": 0, "score_o": 0})
    x_piece = {"call": "build.create_assembly", "args": {"name": "X_{moves}", "location": "=pos[event.index]", "material": "red",
               "color": [0.75, 0.05, 0.04], "roughness": 0.35, "parts": [
                   {"shape": "box", "size": [250, 46, 46], "pivot": "center", "rotation": [0, 45, 0], "bevel": 6},
                   {"shape": "box", "size": [250, 46, 46], "pivot": "center", "rotation": [0, -45, 0], "bevel": 6}]}}
    o_piece = {"call": "build.create_primitive", "args": {"name": "O_{moves}", "shape": "torus", "size": [210, 210, 46],
               "location": "=pos[event.index]", "material": "blue", "color": [0.04, 0.2, 0.75], "roughness": 0.35, "detail": "high"}}
    rt.call("logic.define_rules", rules=[
        {"name": "setup", "on": "start", "actions": [{"set": "lines", "value": "grid_lines(3, 3, 3)"}]},
        {"name": "player_click", "on": "click", "target": "tag:cell",
         "condition": "winner == '' && board[event.data.index] == '' && (!ai || turn == 'X')",
         "actions": [{"emit": "move", "data": {"index": "=event.data.index"}}]},
        {"name": "move", "on": "event", "filter": "move", "actions": [
            {"set_item": "board", "index": "event.index", "value": "turn"},
            {"if": "turn == 'X'", "then": [x_piece], "else": [o_piece]},
            {"set": "moves", "value": "moves + 1"},
            {"if": "line_winner(board, lines, '') != null", "then": [
                {"set": "winner", "value": "turn"},
                {"if": "turn == 'X'", "then": [{"set": "score_x", "value": "score_x + 1"}], "else": [{"set": "score_o", "value": "score_o + 1"}]},
                {"for": "i", "in": "winning_line(board, lines, '')", "do": [
                    {"call": "build.create_primitive", "args": {"name": "glow_{i}", "shape": "box", "size": [CELL - 40, CELL - 40, 4],
                                                                "location": "=[pos[i][0], pos[i][1], 46]", "material": "light",
                                                                "color": [1, 0.8, 0.3], "emissive": [1, 0.7, 0.15], "emissive_strength": 40}}]}],
             "else": [{"if": "moves == 9", "then": [{"set": "winner", "value": "'draw'"}]}]},
            {"if": "winner == ''", "then": [{"set": "turn", "value": "turn == 'X' ? 'O' : 'X'"}]}]},
        {"name": "ai_turn", "on": "var_changed", "filter": "turn", "condition": "ai && turn == 'O' && winner == ''",
         "actions": [{"timer": "ai", "delay": 0.6}]},
        {"name": "ai_move", "on": "timer", "filter": "ai", "condition": "winner == ''", "actions": [
            {"let": "m", "value": "winning_move(board, lines, 'O', '')"},
            {"if": "m < 0", "then": [{"set": "m", "value": "winning_move(board, lines, 'X', '')"}]},
            {"if": "m < 0 && board[4] == ''", "then": [{"set": "m", "value": "4"}]},
            {"if": "m < 0", "then": [{"set": "m", "value": "pick(indices(board, ''))"}]},
            {"emit": "move", "data": {"index": "=m"}}]},
        {"name": "restart_button", "on": "button", "filter": "restart", "actions": [{"restart": True}]},
        {"name": "restart_key", "on": "key", "key": "R", "actions": [{"restart": True}]},
    ])
    # Scores survive a restart: restart resets variables, so carry them in the start rule.
    rt.call("logic.define_ui", items=[
        {"name": "title", "kind": "text", "text": "TIC-TAC-TOE", "anchor": [0.5, 0.05], "font_size": 30, "background": [0, 0, 0, 0.55]},
        {"name": "status", "kind": "text", "anchor": [0.5, 0.13], "font_size": 24, "color": [1, 0.85, 0.4, 1],
         "text": "{winner == '' ? (ai && turn == 'O' ? 'Computer is thinking...' : turn + ' to move') : (winner == 'draw' ? 'Draw!' : winner + ' wins!')}"},
        {"name": "restart", "kind": "button", "text": "Restart (R)", "anchor": [0.5, 0.92], "font_size": 22},
        {"name": "hint", "kind": "text", "text": "Click a square to play" + (" X against the computer" if args.ai else ""), "anchor": [0.5, 0.97],
         "font_size": 14, "background": [0, 0, 0, 0]},
    ])
    rt.call("logic.set_input_mode", mode="cursor", lock_camera=True)
    rt.call("lighting.apply_mood", mood="noon")
    rt.call("camera.set_camera", location=[0, -1150, 1550], look_at=[0, -60, 0], fov=60)
    rt.call("logic.start")
    if args.demo:
        for cell in ([0, 4] if args.ai else [0, 4, 8, 2, 6]):
            rt.call("logic.fire_event", on="click", target=f"TicTacToe/cell_{cell}")
            if args.ai:
                rt.call("logic.step", seconds=1)
    state = rt.call("logic.get_state", log_limit=5)
    print("vars:", {k: state["vars"][k] for k in ("board", "turn", "winner", "moves")})
    if args.out:
        rt.call("ops.wait", seconds=1)
        rt.screenshot(args.out, show_ui=True)
        print("saved", args.out)


if __name__ == "__main__":
    main()
