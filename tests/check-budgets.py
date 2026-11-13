#!/usr/bin/env python3
"""Budget check: derives the worst frame from assets and src constants, compares it with disc.toml [budget.*].

Run: python3 tests/check-budgets.py (exit 0 = pass).
Parses src/squid-view.hpp, src/squid-params.hpp, src/squid-body.hpp and scenes/main.scene.toml,
so no table is mirrored.
Sound is read from the sfx WAV headers: the check requires mono S16 44100 Hz, which bakes to the same size (frames x 2).
"""
import glob, json, os, re, sys, tomllib, wave

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
def read(p): return open(os.path.join(root, p), encoding="utf-8").read()
def die(msg): sys.exit("check-budgets: " + msg)

def tris(name):  # triangles of a model = index count / 3 over all primitives of its .gltf
    j = json.load(open(os.path.join(root, "assets/models", name.replace(".mppcmod", ".gltf"))))
    return sum(j["accessors"][p["indices"]]["count"] // 3 for m in j["meshes"] for p in m["primitives"])

def array(src, name):  # the brace body of `name[...] = { ... };`
    m = re.search(name + r"[^=]*=\s*\{(.*?)\};", src, re.S)
    return m.group(1) if m else die("cannot find " + name)

def const(src, name):  # the integer of `name = N;`
    m = re.search(name + r"\s*=\s*(\d+)\s*;", src)
    return int(m.group(1)) if m else die("cannot find " + name)

view, params, body = read("src/squid-view.hpp"), read("src/squid-params.hpp"), read("src/squid-body.hpp")

# Ball models by size, limited to the first SQUID_VIEW_SIZE_VARIANTS names of each row (the view takes variant % count).
rows = re.findall(r"\{([^{}]*)\}", array(view, "SQUID_VIEW_MODELS"))
counts = [int(x) for x in re.findall(r"\d+", array(view, "SQUID_VIEW_SIZE_VARIANTS"))]
size_tri = [max(tris(n) for n in re.findall(r'"([^"]+)"', row)[:c]) for row, c in zip(rows, counts)]
size_by_mass = [int(x) for x in re.findall(r"\d+", array(body, "SQUID_SIZE_BY_MASS"))]

# Ball mass budget: the start rows { count, mass }; split and absorb keep the total.
start_rows = array(params, "SQUID_START_ROWS")
budget_mass = sum(int(c) * int(m) for c, m in re.findall(r"\{\s*(\d+),\s*(\d+)\s*\}", start_rows))

# Worst ball layout: exact-mass unbounded knapsack over the ball masses 1..8, value = the size's costliest model.
best = [0] + [-1] * budget_mass
for m in range(1, budget_mass + 1):
    options = [best[m - k] + size_tri[size_by_mass[k] - 1]
               for k in range(1, len(size_by_mass)) if m >= k and best[m - k] >= 0]
    best[m] = max(options, default=-1)
balls = best[budget_mass]

def name(var): return re.search(var + r'\s*=\s*"([^"]+)"', view).group(1)
bottle_tri = max(tris(name("SQUID_VIEW_BOTTLE_MODEL")), tris(name("SQUID_VIEW_BOTTLE_MARKED_MODEL")))
bottles = const(params, "SQUID_BOTTLE_MAX") * bottle_tri
scene = read("scenes/main.scene.toml")
structure = sum(tris(s) for s in re.findall(r'^source\s*=\s*"([^"]+\.gltf)"', scene, re.M))
alkoldun = max(tris(name("SQUID_VIEW_ALKOLDUN_MODEL")), tris(name("SQUID_VIEW_FLAME_MODEL")))  # drawn one at a time
worst = balls + bottles + structure + alkoldun

effects = 0
for f in sorted(glob.glob(os.path.join(root, "assets/snd/sfx_*.wav"))):
    with wave.open(f) as w:
        if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (1, 2, 44100):
            die(f"{os.path.basename(f)} is not mono S16 44100 Hz; the baked size is not frames x 2")
        effects += w.getnframes() * 2

b = tomllib.loads(read("disc.toml"))["budget"]
tc, fc, sm = b["pccg"]["triangle_capacity"], b["pccv"]["frame_capacity"], b["pcca"]["sound_memory_size"]
print(f"ball mass budget {budget_mass}, max triangles per size {size_tri}")
print(f"worst frame {worst} = balls {balls} + bottles {bottles} + structure {structure} + alkoldun {alkoldun}")
print(f"effects PCM {effects} bytes")
print(f"disc.toml: triangle_capacity {tc}, frame_capacity {fc}, sound_memory_size {sm}")

checks = [
    (f"triangle_capacity {tc} >= worst x 1.25 = {worst * 1.25:.0f}", tc >= worst * 1.25),
    (f"triangle_capacity {tc} <= worst x 1.6 = {worst * 1.6:.0f} (not wasteful)", tc <= worst * 1.6),
    (f"frame_capacity {fc} == 2 x triangle_capacity = {2 * tc}", fc == 2 * tc),
    (f"sound_memory_size {sm} >= effects x 1.25 = {effects * 1.25:.0f}", sm >= effects * 1.25),
    (f"sound_memory_size {sm} <= effects x 2 = {effects * 2} (not wasteful)", sm <= effects * 2),
]
for text, ok in checks:
    print(("PASS " if ok else "FAIL ") + text)
sys.exit(0 if all(ok for _, ok in checks) else 1)
