from pathlib import Path
p=Path('lab/aalab1/aalab_quality_gpu.cpp')
s=p.read_text(encoding='utf-8')
old="float mortar=(std::fmod(px*16.0f+(int)(py*12)&1?0.5f:0.0f,1.0f)<.08f||std::fmod(py*12.0f,1.0f)<.08f)?1.0f:0.0f;"
new="float rowOffset=(((int)(py*12.0f))&1)?0.5f:0.0f;float mortar=(std::fmod(px*16.0f+rowOffset,1.0f)<.08f||std::fmod(py*12.0f,1.0f)<.08f)?1.0f:0.0f;"
assert s.count(old)==1, s.count(old)
p.write_text(s.replace(old,new,1),encoding='utf-8')
print('AALAB_QUALITY_SOURCE_PREP=PASS')
