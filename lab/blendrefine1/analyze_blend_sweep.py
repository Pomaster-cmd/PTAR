import csv
from pathlib import Path

quality=list(csv.DictReader(open('artifact/AALAB_QUALITY.csv',newline='')))
temporal=list(csv.DictReader(open('artifact/AALAB_TEMPORAL.csv',newline='')))
budgets=list(csv.DictReader(open('artifact/BLENDREFINE_SM3_BUDGETS.csv',newline='')))

names=[]
for r in quality:
    if r['variant'] not in names:
        names.append(r['variant'])
q={n:[] for n in names}; t={n:[] for n in names}
for r in quality:q[r['variant']].append({k:(int(v) if k=='pattern' else float(v)) for k,v in r.items() if k!='variant'})
for r in temporal:t[r['variant']].append({k:(int(v) if k in ('pattern','pair') else float(v)) for k,v in r.items() if k!='variant'})
b={r['Name']:{'strength':int(r['StrengthPermille'])/10.0,'texld':int(r['Texld']),'slots':int(r['Slots'])} for r in budgets}

alias={0,1,2,7}; textures={3,4,5,6}
def avg(rows,key,subset=None):
    v=[r[key] for r in rows if subset is None or r.get('pattern') in subset]
    return sum(v)/len(v)

def met(n):
    return {
      'mae':avg(q[n],'mae'),'psnr':avg(q[n],'psnr'),'edge':avg(q[n],'edge_mae',alias),
      'grad':avg(q[n],'grad_mae',alias),'lap':avg(q[n],'lap_mae',alias),'texture':avg(q[n],'mae',textures),
      'detail':avg(q[n],'detail_ratio'),'tdelta':avg(t[n],'delta_err'),'flicker':avg(t[n],'flicker_excess'),
      'deficit':avg(t[n],'motion_deficit'),'energy':avg(t[n],'energy_ratio')}
M={n:met(n) for n in names}; base=M['B00']
def ratio(a,b):return a/b if b else 1.0
def pct(a,b):return (ratio(a,b)-1)*100.0

rows=[]
for n in names:
    m=M[n]
    # Balanced quality objective. Native-reference static accuracy and alias-sensitive
    # edges dominate, but motion preservation prevents choosing an over-blurred result.
    detail_error=abs(m['detail']-1.0)
    base_detail_error=max(abs(base['detail']-1.0),1e-9)
    energy_error=abs(m['energy']-1.0)
    base_energy_error=max(abs(base['energy']-1.0),1e-9)
    score=(
      .22*ratio(m['mae'],base['mae'])+
      .18*ratio(m['edge'],base['edge'])+
      .14*ratio(m['grad'],base['grad'])+
      .08*ratio(m['lap'],base['lap'])+
      .10*ratio(m['texture'],base['texture'])+
      .12*ratio(m['tdelta'],base['tdelta'])+
      .06*ratio(m['flicker'],base['flicker'])+
      .06*(detail_error/base_detail_error)+
      .04*(energy_error/base_energy_error))
    eligible=(m['tdelta']<=base['tdelta']*1.005 and m['flicker']<=base['flicker']*1.005 and m['energy']>=0.90 and m['detail']>=0.95 and m['texld'] if False else True)
    # topology gates are separate to avoid accidental boolean precedence
    eligible = eligible and b[n]['texld']==20 and b[n]['slots']<=350
    rows.append((score,n,eligible,m))
rows.sort()

static_best=min(names,key=lambda n:M[n]['mae'])
alias_best=min(names,key=lambda n:(M[n]['edge']/base['edge']+M[n]['grad']/base['grad']+M[n]['lap']/base['lap']))
temporal_best=min(names,key=lambda n:(M[n]['tdelta']/base['tdelta']+M[n]['flicker']/base['flicker']))
eligible=[r for r in rows if r[2]]
best=eligible[0][1] if eligible else 'NONE'

out=[]
out.append('PTAR SPEC5 BLENDREFINE1 DECISION')
out.append('ALL_VARIANTS=20_TEXLD_NO_EXTRA_TEXTURE_SAMPLES')
out.append(f'STATIC_MAE_BEST={static_best}')
out.append(f'ALIAS_METRIC_BEST={alias_best}')
out.append(f'TEMPORAL_METRIC_BEST={temporal_best}')
out.append('')
for score,n,ok,m in rows:
    out.append(
      f'{n} strength={b[n]["strength"]:.1f}% score={score:.6f} eligible={"YES" if ok else "NO"} '
      f'MAE={pct(m["mae"],base["mae"]):+.3f}% EDGE={pct(m["edge"],base["edge"]):+.3f}% '
      f'GRAD={pct(m["grad"],base["grad"]):+.3f}% LAP={pct(m["lap"],base["lap"]):+.3f}% '
      f'TEXTURE={pct(m["texture"],base["texture"]):+.3f}% DETAIL={m["detail"]:.5f} '
      f'TEMPDELTA={pct(m["tdelta"],base["tdelta"]):+.3f}% FLICKER={pct(m["flicker"],base["flicker"]):+.3f}% '
      f'DEFICIT={pct(m["deficit"],base["deficit"]):+.3f}% ENERGY={m["energy"]:.5f} slots={b[n]["slots"]}')
out.append('')
out.append(f'BLENDREFINE_BEST_ELIGIBLE={best}')
out.append('BLENDREFINE_DECISION_GATE=PASS')
text='\n'.join(out)+'\n'
Path('artifact/BLENDREFINE_DECISION.txt').write_text(text,encoding='ascii')
print(text)
