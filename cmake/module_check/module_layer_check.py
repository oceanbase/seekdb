#!/usr/bin/env python3
"""Module-layering DAG guard (include graph).
- Traverses modules tagged for scan (currently oblib + share); other modules serve only as targets, used to resolve which module/layer an include points to.
- Rule: a module may only include modules with layer<=its own (same-layer mutual deps allowed); upward edges = violations.
- baseline(module_layer_baseline.txt): known historical residue (each line "src_relpath -> include"); entries in it are not counted as new violations, and should be cleared to zero gradually.
- Any out-of-baseline upward edge -> non-zero exit.
Usage: python3 module_layer_guard.py [repo_root] [--strict]
      --strict: rc=1 if any baseline residue remains (for the "fully clean" milestone gate); default ratchet mode only blocks new ones
      python3 module_layer_guard.py --update-baseline [repo_root]   # write current upward edges into baseline
Config: module_layers.conf (same directory).
"""
import os,re,sys
HERE=os.path.dirname(os.path.abspath(__file__))
args=[a for a in sys.argv[1:] if not a.startswith("--")]
UPDATE="--update-baseline" in sys.argv
STRICT="--strict" in sys.argv  # strict mode: non-zero exit if any residue remains
REPO=os.path.abspath(args[0]) if args else os.path.abspath(os.path.join(HERE,"../.."))
CONF=os.path.join(HERE,"module_layers.conf")
BASE=os.path.join(HERE,"module_layer_baseline.txt")
OBLIB=os.path.join(REPO,"deps","oblib")
mods=[]  # (fs_abs, name, layer, inc_prefix, scan) -- name=path; level in [targets], scan from [scanned]
SRCROOTS=("deps/oblib/src/","src/")
def _derive_prefix(path):
    for r in SRCROOTS:
        if path.startswith(r): return path[len(r):]
    return path
_targets={}  # path -> (layer, prefix)
_scan=set()
section=None
for ln in open(CONF):
    ln=ln.split("#")[0].strip()
    if not ln: continue
    if ln.startswith("[") and ln.endswith("]"):
        section=ln[1:-1]; continue       # [scanned] / [targets]
    if section is None: continue
    p=ln.split()
    if section=="scanned":
        _scan.add(p[0]); continue        # [scanned]: lists paths only, no level
    if len(p)<2: continue
    path=p[0]; lay=int(p[1]); inc=None
    for tok in p[2:]:
        if tok.startswith("@"): inc=tok[1:]
    if inc is None: inc=_derive_prefix(path)
    _targets[path]=(lay,inc)             # [targets]: identity+layer registry
_missing=[x for x in _scan if x not in _targets]
assert not _missing, "scanned path not in [targets]: %s"%_missing
for _p,(_lay,_inc) in _targets.items():
    mods.append((os.path.join(REPO,_p),_p,_lay,_inc,_p in _scan))
layer={m[1]:m[2] for m in mods}
layer['__extsrc__']=99  # synthetic: any non-oblib reference resolving to a real file under src/
inc_map=sorted(((m[3],m[1]) for m in mods), key=lambda x:-len(x[0]))
def file_mod(path):
    best=None;bl=-1
    for fs,name,_,_,_ in mods:
        if (path==fs or path.startswith(fs+os.sep)) and len(fs)>bl: best,bl=name,len(fs)
    return best
def inc_mod(s):
    if s.startswith("deps/oblib/src/"): s=s[len("deps/oblib/src/"):]
    for pre,name in inc_map:
        if s==pre or s.startswith(pre+"/"): return name
    # default-deny: not configured but a real file is found under src/ => external src upward edge (auto-covers all src dirs, including future new ones)
    # also accepts both spellings: 1) rooted "share/.." => src/share/..; 2) full path "src/share/.." => REPO/src/share/..
    if os.path.isfile(os.path.join(REPO,"src",s)) or (s.startswith("src/") and os.path.isfile(os.path.join(REPO,s))):
        return "__extsrc__"
    return None
INC=re.compile(r'#\s*include\s+"([^"]+)"')
base=set()
if os.path.exists(BASE):
    for ln in open(BASE):
        ln=ln.split("#")[0].strip()
        if " -> " in ln: a,b=ln.split(" -> ",1); base.add((a.strip(),b.strip()))
EXT=(".h",".hpp",".hh",".cpp",".cc",".cxx",".ipp",".c"); seen=set(); cur=[]; edges=0
for fs,name,lv,inc,scan in mods:
    if not scan: continue
    for r,_,files in os.walk(fs):
        if os.sep+"build" in r: continue
        for f in files:
            if not f.endswith(EXT): continue
            p=os.path.join(r,f)
            if p in seen: continue
            seen.add(p); M=file_mod(p)
            if M is None: continue
            try: t=open(p,errors="ignore").read()
            except: continue
            for m in INC.finditer(t):
                N=inc_mod(m.group(1))
                if N is None or N==M: continue
                edges+=1
                if layer[N]>layer[M]: cur.append((os.path.relpath(p,REPO),m.group(1),M,layer[M],N,layer[N]))
curset=set((f,h) for f,h,*_ in cur)
if UPDATE:
    with open(BASE,"w") as w:
        w.write("# oblib upward-edge include baseline (historical residue, to be cleared to zero gradually). Each line: <relative path> -> <include>\n")
        for f,h,M,lm,N,ln in sorted(cur): w.write("%s -> %s\n"%(f,h))
    print("baseline written: %d entries"%len(cur)); sys.exit(0)
new=[v for v in cur if (v[0],v[1]) not in base]
print("module-layer check: cross-module include %d, upward edges %d (baseline known %d, new %d)"%(edges,len(cur),len(base),len(new)))
print("layers: "+", ".join("%s=L%d"%(m[3],m[2]) for m in sorted([x for x in mods],key=lambda x:x[2])))
if new:
    print("\n[FAIL] new upward-edge include (not in baseline):")
    for f,h,M,lm,N,ln in sorted(new): print("   %s [%s L%d] -> \"%s\" [%s L%d]"%(f,M,lm,h,N,ln))
    print("\nfixes: 1) sink the base file 2) move up the upper-layer code 3) forward-declare/push to .cpp/extract interface")
    sys.exit(1)
stale=base-curset
if stale:
    print("\n[NOTE] baseline has eliminated entries (%d), run --update-baseline to tighten:"%len(stale))
    for f,h in sorted(stale)[:10]: print("   %s -> %s"%(f,h))
residual=sorted((v[0],v[1]) for v in cur if (v[0],v[1]) in base)
if residual:
    print("\n[OK] no new upward edges. baseline residue %d (to be cleared to zero gradually):"%len(residual))
    for f,h in residual: print("   %s -> %s"%(f,h))
    if STRICT:
        print("\n[STRICT FAIL] still %d residue entries not cleared"%len(residual)); sys.exit(1)
else:
    print("[OK] no upward edges, baseline is empty")
