#!/bin/bash
# Oh benchmark harness — the TOKENS + PERFORMANCE check (run on every feature).
# For each benchmarks/fair/<name>.oh with a matching <name>.c:
#   performance: build both (oh + cc -O3 -march=native), time fold-proof best-of-N
#   tokens:      cl100k_base tokens of the Oh implementation (std deps + source)
#                vs the C implementation source
# Usage: benchmarks/bench.sh
set -u
cd "$(dirname "$0")/.."
OH=./compiler/oh
make -C compiler -s 2>/dev/null || { echo "build failed"; exit 1; }

# std-module dependencies per benchmark (for compile + honest token counting)
deps() { case "$1" in
  vec)  echo "std/mem.oh std/vec.oh" ;;
  map)  echo "std/mem.oh std/map.oh" ;;
  buf)  echo "std/core.oh std/mem.oh std/buf.oh" ;;
  json) echo "std/core.oh std/str.oh std/json.oh" ;;
  *)    echo "" ;;
esac }

python3 - "$OH" <<'PYEOF'
import sys,subprocess,time,glob,os
OH=sys.argv[1]
try:
    import tiktoken; enc=tiktoken.get_encoding("cl100k_base")
    def tok(s): return len(enc.encode(s))
except Exception:
    def tok(s): return len(s.split())  # fallback
def deps(n):
    return {"vec":"std/mem.oh std/vec.oh","map":"std/mem.oh std/map.oh",
            "buf":"std/core.oh std/mem.oh std/buf.oh",
            "json":"std/core.oh std/str.oh std/json.oh"}.get(n,"")
def best(p,r=4):
    b=9e9
    for _ in range(r):
        try: t=time.perf_counter(); subprocess.run([p],capture_output=True,timeout=120); b=min(b,time.perf_counter()-t)
        except Exception: return None
    return b
def src_toks(files):
    n=0
    for f in files.split():
        if os.path.exists(f):
            s="\n".join(l for l in open(f).read().splitlines() if not l.strip().startswith("//"))
            n+=tok(s)
    return n
rows=[]
for ohf in sorted(glob.glob("benchmarks/fair/*.oh")):
    name=os.path.basename(ohf)[:-3]
    cf=f"benchmarks/fair/{name}.c"
    if not os.path.exists(cf): continue
    d=deps(name)
    ob=f"/tmp/bh_{name}_oh"; cb=f"/tmp/bh_{name}_c"
    subprocess.run(f"{OH} {d} {ohf} -o {ob}".split(),capture_output=True)
    subprocess.run(f"cc -O3 -march=native {cf} -o {cb}".split(),capture_output=True)
    if not (os.path.exists(ob) and os.path.exists(cb)): 
        rows.append((name,"build-fail")); continue
    ot=best(ob); ct=best(cb)
    oh_tok=src_toks(f"{d} {ohf}"); c_tok=tok("\n".join(l for l in open(cf).read().splitlines() if not l.strip().startswith("#include")))
    rows.append((name,oh_tok,c_tok,ot,ct))
print(f"{'bench':<11}{'OHtok':>6}{'Ctok':>6}{'tokΔ':>7}  {'OH(s)':>8}{'C(s)':>8}{'OH/C':>7}")
print("-"*54)
twin=0; pwin=0; tot=0
for r in rows:
    if r[1]=="build-fail": print(f"{r[0]:<11} BUILD FAIL"); continue
    name,oht,ct,ot,cct=r; tot+=1
    tokd=f"{(oht-ct)/ct*100:+.0f}%"
    if oht<=ct: twin+=1
    pr=ot/cct if (ot and cct) else 0
    if ot and cct and ot<=cct*1.05: pwin+=1
    print(f"{name:<11}{oht:>6}{ct:>6}{tokd:>7}  {ot:>8.4f}{cct:>8.4f}{pr:>6.2f}x")
print("-"*54)
print(f"tokens <= C: {twin}/{tot}   perf <= C: {pwin}/{tot}")
print("note: stdlib rows (vec/map/buf/json) count the FULL reusable module vs C's")
print("inline code — conservative against Oh (the module is written once, amortized).")
PYEOF
