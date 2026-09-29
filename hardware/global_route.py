import heapq
import math
import sys

import pcbnew

import place_eval

CELL_MM = 0.5
HALF_SPAN_MM = 18.5
TRACK_PITCH_MM = 0.277
PAD_KEEP_MM = 0.2
LAYER_TRACKS = CELL_MM / TRACK_PITCH_MM
ITERATIONS = 5
PRESENT_WEIGHT = 2.0
HISTORY_STEP = 1.0
HOTSPOTS = 8
SIDE = int(2 * HALF_SPAN_MM / CELL_MM)
STEPS = [(dx, dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1) if dx or dy]


def cell_of(pt):
    return (min(SIDE - 1, max(0, int((pt[0] + HALF_SPAN_MM) / CELL_MM))),
            min(SIDE - 1, max(0, int((pt[1] + HALF_SPAN_MM) / CELL_MM))))


def centre(c):
    return -HALF_SPAN_MM + (c[0] + 0.5) * CELL_MM, -HALF_SPAN_MM + (c[1] + 0.5) * CELL_MM


def kicad_point(pt):
    return pcbnew.VECTOR2I(pcbnew.FromMM(pt[0]), pcbnew.FromMM(-pt[1]))


def rule_areas(board):
    zones = list(board.Zones())
    for fp in board.GetFootprints():
        zones += list(fp.Zones())
    return [(z.Outline(), [z.IsOnLayer(pcbnew.F_Cu), z.IsOnLayer(pcbnew.B_Cu)])
            for z in zones if z.GetIsRuleArea() and z.GetDoNotAllowTracks()]


def cell_layers(v, spec):
    outline, areas = spec
    layers = [outline.Contains(v)] * 2
    for poly, on in areas:
        if any(layers) and poly.Contains(v):
            layers = [a and not b for a, b in zip(layers, on)]
    return layers


def layer_free(board):
    outline = pcbnew.SHAPE_POLY_SET()
    board.GetBoardPolygonOutlines(outline, False)
    spec = (outline, rule_areas(board))
    free = {(i, j): cell_layers(kicad_point(centre((i, j))), spec) for i in range(SIDE) for j in range(SIDE)}
    pad_nets = {}
    for fp in board.GetFootprints():
        mark_pads((free, pad_nets), fp)
    mark_module(free, board)
    return free, pad_nets


def mark_pads(maps, fp):
    free, pad_nets = maps
    for pad in fp.Pads():
        layers = [k for k, lay in enumerate((pcbnew.F_Cu, pcbnew.B_Cu)) if pad.IsOnLayer(lay)]
        bb = pad.GetBoundingBox()
        x0, y1 = place_eval.layout_xy(bb.GetOrigin())
        x1, y0 = place_eval.layout_xy(bb.GetEnd())
        for c in cells_in((x0 - PAD_KEEP_MM, y0 - PAD_KEEP_MM, x1 + PAD_KEEP_MM, y1 + PAD_KEEP_MM)):
            pad_nets.setdefault(c, set()).add(pad.GetNetname())
            for k in layers:
                free[c][k] = False


def mark_module(free, board):
    court = board.FindFootprintByReference("U1").GetCourtyard(pcbnew.F_CrtYd).BBox()
    x0, y1 = place_eval.layout_xy(court.GetOrigin())
    x1, y0 = place_eval.layout_xy(court.GetEnd())
    for c in cells_in((x0, y0, x1, y1)):
        free[c][0] = False


def cells_in(bounds):
    a, b = cell_of(bounds[:2]), cell_of(bounds[2:])
    return [(i, j) for i in range(a[0], b[0] + 1) for j in range(a[1], b[1] + 1)]


def capacity(free):
    return {c: LAYER_TRACKS * sum(v) for c, v in free.items()}


def step_cost(cell, state):
    cap, use, hist = state["cap"][cell], state["use"].get(cell, 0), state["hist"].get(cell, 0.0)
    return 1.0 + hist + PRESENT_WEIGHT * max(0.0, use + 1 - cap)


def passable(n, spec):
    state, net, goal = spec
    return n in state["cap"] and (state["cap"][n] > 0 or n == goal or net in state["pad_nets"].get(n, ()))


def astar(wire, state):
    net, (a, b) = wire
    start, goal = cell_of(a), cell_of(b)
    seen, queue = {start: None}, [(0.0, 0.0, start)]
    state["best"][start] = 0.0
    done = set()
    while queue:
        _f, g, c = heapq.heappop(queue)
        if c == goal:
            break
        if c in done:
            continue
        done.add(c)
        for dx, dy in STEPS:
            n = (c[0] + dx, c[1] + dy)
            if not passable(n, (state, net, goal)):
                continue
            ng = g + math.hypot(dx, dy) * step_cost(n, state)
            if n not in seen or ng < state["best"].get(n, math.inf):
                seen[n], state["best"][n] = c, ng
                heapq.heappush(queue, (ng + math.dist(n, goal), ng, n))
    return trace(seen, goal)


def trace(seen, goal):
    if goal not in seen:
        return None
    path, c = [], goal
    while c is not None:
        path.append(c)
        c = seen[c]
    return path


def negotiate(wires, state):
    failed = 0
    for _ in range(ITERATIONS):
        state["use"], failed = {}, 0
        for w in wires:
            state["best"] = {}
            path = astar(w, state)
            failed += path is None
            for c in path or []:
                state["use"][c] = state["use"].get(c, 0) + 1
        for c, u in state["use"].items():
            if 0 < state["cap"][c] < u:
                state["hist"][c] = state["hist"].get(c, 0.0) + HISTORY_STEP
    return failed


def estimate(board):
    model = place_eval.model_from_board(board)
    wires = sorted(place_eval.airwires(model), key=lambda w: math.dist(*w[1]))
    free, pad_nets = layer_free(board)
    state = {"cap": capacity(free), "hist": {}, "pad_nets": pad_nets}
    failed = negotiate(wires, state)
    over = {c: u - state["cap"][c] for c, u in state["use"].items() if 0 < state["cap"][c] < u}
    spots = sorted(over.items(), key=lambda kv: -kv[1])[:HOTSPOTS]
    return {"wires": len(wires), "unroutable": failed, "overflow": round(sum(over.values()), 1),
            "cells": len(over), "hotspots": [(tuple(round(v, 1) for v in centre(c)), round(o, 1)) for c, o in spots]}


def main():
    for path in sys.argv[1:] or [place_eval.BOARD]:
        print(path, estimate(pcbnew.LoadBoard(path)))


if __name__ == "__main__":
    main()
