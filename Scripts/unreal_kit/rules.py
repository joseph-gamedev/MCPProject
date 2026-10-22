"""Rule building: game logic as plain data for logic.define_rules (no hand-written JSON).

    from unreal_kit import rules as r
    r.define_vars(score=0, lives=3)
    r.add_rules([
        r.rule("coin", on="touch", target="tag:coin", actions=[r.hide("event.target"), r.add("score", 10)]),
        r.rule("dead", on="var_changed", filter="lives", condition="lives <= 0", actions=[r.player("freeze")]),
    ])
    r.start(); print(r.errors())

Expressions are strings ("score + 10", "lives <= 0"); text values need quotes inside ("'READY'").
on: start | key | click | button | timer | var_changed | event | tick | touch | contact | climb | damaged | died | ...
Full reference: logic.get_guide.
"""

from . import tools

# ------------------------------------------------------------------ rule DSL (plain dicts)


def rule(name, on, actions, target=None, key=None, filter=None, condition=None):
    """A rule: when <on> [target/key/filter] and <condition>, run <actions>.
    on: start | key | click | button | timer | var_changed | event | tick | touch | contact | climb | damaged | died | ..."""
    r = {"name": name, "on": on, "actions": list(actions)}
    for k, v in (("target", target), ("key", key), ("filter", filter), ("condition", condition)):
        if v is not None:
            r[k] = v
    return r


def set_(var, expr):
    return {"set": var, "value": str(expr)}


def add(var, amount):
    return {"set": var, "value": f"{var} + ({amount})"}


def let(name, expr):
    return {"let": name, "value": str(expr)}


def if_(condition, then, else_=None):
    a = {"if": condition, "then": list(then)}
    if else_:
        a["else"] = list(else_)
    return a


def for_(var, in_, do):
    return {"for": var, "in": in_, "do": list(do)}


def emit(event, **data):
    a = {"emit": event}
    if data:
        a["data"] = {k: (v if isinstance(v, str) and v.startswith("=") else f"={v}" if not isinstance(v, str) else f"='{v}'") for k, v in data.items()}
    return a


def timer(name, delay, repeat=False):
    return {"timer": name, "delay": str(delay), "repeat": bool(repeat)}


def cancel_timer(name):
    return {"cancel_timer": name}


def hide(target):
    return {"hide": target}


def show(target):
    return {"show": target}


def spawn(pool, at, as_="p"):
    return {"spawn": pool, "at": at, "as": as_}


def release(target):
    return {"release": target}


def tween(target, **fields):
    """Fields: move/location/rotate/rotation/scale/color, duration, delay (plain expression), ease, loop, then, emit."""
    a = {"tween": target}
    a.update(fields)
    return a


def damage(target, amount, type_=None):
    a = {"damage": target, "amount": str(amount)}
    if type_:
        a["type"] = type_
    return a


def heal(target, amount):
    return {"heal": target, "amount": str(amount)}


def player(op, value=None, **fields):
    """op: respawn | bounce | freeze | unfreeze | checkpoint | launch | teleport | set | spawn | remove."""
    a = {"player": op}
    if value is not None:
        a["value"] = str(value)
    if fields:
        a["fields"] = fields
    return a


def feedback(name):
    return {"feedback": name}


def restart(keep=()):
    return {"restart": True, "keep": list(keep)}


def call_tool(tool, as_=None, **args):
    """Runs any non-logic tool from a rule; argument strings starting with '=' are expressions."""
    a = {"call": tool, "args": args}
    if as_:
        a["as"] = as_
    return a


def log(text):
    return {"log": text}


def save(slot, vars_):
    return {"save": slot, "vars": list(vars_)}


def load(slot):
    return {"load": slot}


# ------------------------------------------------------------------ definitions


def define_vars(**vars_):
    return tools.logic.define_vars(vars=vars_)


def add_rules(rules, replace=False):
    return tools.logic.define_rules(rules=list(rules), replace=replace)


def start():
    return tools.logic.start()


def stop():
    return tools.logic.stop()


def state(log_limit=20):
    """Variables, recent events and rule errors: check this after every change."""
    return tools.logic.get_state(log_limit=log_limit)


def errors():
    return [e for e in state(60).get("log", []) if e.get("errors")]


