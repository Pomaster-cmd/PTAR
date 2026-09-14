from pathlib import Path
import sys
if len(sys.argv)<2: raise SystemExit('usage: normalize_rc54_probe_sources.py FILE...')
for arg in sys.argv[1:]:
    p=Path(arg);s=p.read_text(encoding='utf-8')
    if '#include <cstdio>' not in s:
        if '#include <cstring>\n' in s:s=s.replace('#include <cstring>\n','#include <cstring>\n#include <cstdio>\n',1)
        else:s='#include <cstdio>\n'+s
    s=s.replace('wsprintfA(q,','std::snprintf(q,sizeof(q),')
    p.write_text(s,encoding='utf-8',newline='\n')
    print('RC54_NORMALIZED='+str(p))
