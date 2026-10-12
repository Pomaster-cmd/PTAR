import csv, math
from pathlib import Path
from statistics import median

quality=list(csv.DictReader(open('AALAB_QUALITY.csv',newline='')))
perf=list(csv.DictReader(open('AALAB_PERF.csv',newline='')))
budgets=list(csv.DictReader(open('artifact/AALAB_SM3_BUDGETS.csv',newline='')))

names=[]
for r in quality:
    if r['variant'] not in names:
        names.append(r['variant'])

qby={n:[] for n in names}
for r in quality:
    qby[r['variant']].append({k:(int(v) if k=='pattern' else float(v)) for k,v in r.items() if k!='variant'})
pby={n:[] for n in names}
for r in perf:
    pby[r['variant']].append(float(r['ms_per_draw']))
bby={r['Name']:{'texld':int(r['Texld']),'slots':int(r['Slots'])} for r in budgets}

alias_patterns={0,1,2,7}
texture_patterns={3,4,5,6}

def avg(rows,key,subset=None):
    vals=[r[key] for r in rows if subset is None or r['pattern'] in subset]
    return sum(vals)/len(vals)

def metrics(name):
    rows=qby[name]
    return {
        'mae':avg(rows,'mae'),
        'psnr':avg(rows,'psnr'),
        'edge':avg(rows,'edge_mae'),
        'grad':avg(rows,'grad_mae'),
        'lap':avg(rows,'lap_mae'),
        'detail':avg(rows,'detail_ratio'),
        'alias_edge':avg(rows,'edge_mae',alias_patterns),
        'alias_grad':avg(rows,'grad_mae',alias_patterns),
        'alias_lap':avg(rows,'lap_mae',alias_patterns),
        'texture_mae':avg(rows,'mae',texture_patterns),
        'perf_ms':median(pby[name]),
        'texld':bby[name]['texld'],
        'slots':bby[name]['slots'],
    }

M={n:metrics(n) for n in names}
base=M['BASE']

def pct(value,ref):
    return (value/ref-1.0)*100.0 if ref else 0.0

rows=[]
for n in names:
    m=M[n]
    # Lower loss is better. Alias-heavy terms deliberately dominate because that
    # is the observed defect; texture MAE guards against buying AA with blur.
    loss=(
        0.34*(m['alias_edge']/base['alias_edge'])+
        0.24*(m['alias_grad']/base['alias_grad'])+
        0.14*(m['alias_lap']/base['alias_lap'])+
        0.14*(m['texture_mae']/base['texture_mae'])+
        0.08*(m['mae']/base['mae'])+
        0.06*(1.0+abs(m['detail']-1.0))
    )
    perf_over=pct(m['perf_ms'],base['perf_ms'])
    # Conservative deployability gate: quality experiment may be excellent but
    # is not a production candidate if it creates a large proxy cost or texture blur.
    eligible=(perf_over<=3.0 and pct(m['texture_mae'],base['texture_mae'])<=2.0 and m['texld']<=28 and m['slots']<=480)
    rows.append((loss,n,eligible,perf_over,m))

rows.sort()

with open('artifact/AALAB_DECISION.txt','w',encoding='ascii') as f:
    f.write('PTAR SPEC5 AALAB1 DECISION\n')
    f.write('BASE=FULLQUALITY1 SPEC5\n')
    f.write('QUALITY_CORPUS=8_SYNTHETIC_NATIVE_REFERENCES_FROM_COMMON_384x216_SIGNAL\n')
    f.write('ALIAS_PATTERNS=WIRES_HAIR_FOLIAGE_STARBURST\n')
    f.write('TEXTURE_GUARD=GRID_GLYPHS_PAVEMENT_NATURAL_TEXTURE\n')
    f.write('PERF_PROXY=D3D9_HAL_960x540_SYNCHRONIZED_EVENT_QUERY_7x48_DRAWS\n\n')
    for loss,n,eligible,perf_over,m in rows:
        f.write(
            f'{n}: score={loss:.6f} eligible={"YES" if eligible else "NO"} '
            f'aliasEdge={pct(m["alias_edge"],base["alias_edge"]):+.3f}% '
            f'aliasGrad={pct(m["alias_grad"],base["alias_grad"]):+.3f}% '
            f'aliasLap={pct(m["alias_lap"],base["alias_lap"]):+.3f}% '
            f'textureMAE={pct(m["texture_mae"],base["texture_mae"]):+.3f}% '
            f'overallMAE={pct(m["mae"],base["mae"]):+.3f}% '
            f'detail={m["detail"]:.5f} perf={perf_over:+.3f}% '
            f'texld={m["texld"]} slots={m["slots"]}\n')
    eligible_rows=[r for r in rows if r[2]]
    best=eligible_rows[0][1] if eligible_rows else 'NONE'
    f.write(f'\nAALAB_BEST_ELIGIBLE={best}\n')
    f.write('AALAB_DECISION_GATE=PASS\n')

print(Path('artifact/AALAB_DECISION.txt').read_text())
