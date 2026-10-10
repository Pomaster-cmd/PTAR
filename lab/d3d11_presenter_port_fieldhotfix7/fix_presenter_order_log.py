from pathlib import Path
p=Path('compat/x86_d3d9/ptar_d3d9_isolated_presenter.h')
s=p.read_text(encoding='utf-8')
old='order=REAL_THEN_GENERATED'
new='order=GENERATED_THEN_MATCHING_REAL'
if s.count(old)!=1:
    raise SystemExit('presenter order log anchor mismatch')
s=s.replace(old,new,1)
p.write_text(s,encoding='utf-8',newline='\n')
print('PRESENTER_ORDER_LOG_STALEGUARD1=PASS')
