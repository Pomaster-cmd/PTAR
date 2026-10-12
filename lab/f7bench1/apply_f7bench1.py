from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
proxy=ROOT/'compat/x86_d3d9/ptar_d3d9_proxy.cpp'
hud=ROOT/'compat/x86_d3d9/ptar_hud.h'
hud9=ROOT/'compat/x86_d3d9/ptar_gw16i_hud_d3d9.h'

p=proxy.read_text(encoding='utf-8')
h=hud.read_text(encoding='utf-8')
g=hud9.read_text(encoding='utf-8')

# Proxy: module include and passive observation immediately after each successful visible Present.
old='#include "ptar_resolution_policy.h"\n#include "ptar_hud.h"'
new='#include "ptar_resolution_policy.h"\n#include "ptar_f7_benchmark.h"\n#include "ptar_hud.h"'
assert p.count(old)==1, p.count(old)
p=p.replace(old,new,1)

old='''        PtFgPacerRecordVisible(generated);\n        PtD3D9DiagRecordPresent('''
new='''        PtFgPacerRecordVisible(generated);\n        PtF7BenchmarkRecordVisible(g_ptarFgPacer.lastVisibleQpc,generated);\n        PtD3D9DiagRecordPresent('''
assert p.count(old)==1, p.count(old)
p=p.replace(old,new,1)

old='''        PtFgPacerRecordVisible(generatedFrame);\n        PtD3D9DiagRecordPresent('''
new='''        PtFgPacerRecordVisible(generatedFrame);\n        PtF7BenchmarkRecordVisible(g_ptarFgPacer.lastVisibleQpc,generatedFrame);\n        PtD3D9DiagRecordPresent('''
assert p.count(old)==1, p.count(old)
p=p.replace(old,new,1)

# HUD configuration: preserve historic 3 s + 10 s defaults, while allowing the existing INI keys.
old='''static bool g_ptarHudConfigLoaded=false;\nstatic unsigned long g_ptarHudCaptureOrdinal=0;'''
new='''static bool g_ptarHudConfigLoaded=false;\nstatic unsigned long g_ptarHudCaptureOrdinal=0;\nstatic int g_ptarHudBenchmarkWarmupSeconds=3;\nstatic int g_ptarHudBenchmarkDurationSeconds=10;'''
assert h.count(old)==1, h.count(old)
h=h.replace(old,new,1)

old='''    if(g_ptarHudConfigLoaded)\n        return;\n    g_ptarHudConfigLoaded=true;\n\n    // Initialize the fullscreen-safe controller -> in-game HUD bridge'''
new='''    if(g_ptarHudConfigLoaded)\n        return;\n    g_ptarHudConfigLoaded=true;\n\n    // F7 is measurement-only. Configure defaults before any optional INI read\n    // so a missing file keeps the historical 3 s + 10 s contract.\n    PtF7BenchmarkConfigure(selfModule,3,10);\n\n    // Initialize the fullscreen-safe controller -> in-game HUD bridge'''
assert h.count(old)==1, h.count(old)
h=h.replace(old,new,1)

old='''    if(profile<0||profile>3) profile=2;\n    g_ptarHudFgProfile=profile;\n}'''
new='''    if(profile<0||profile>3) profile=2;\n    g_ptarHudFgProfile=profile;\n\n    g_ptarHudBenchmarkWarmupSeconds=GetPrivateProfileIntW(\n        L"WIN81_NIS",L"BenchmarkWarmupSeconds",3,iniPath);\n    g_ptarHudBenchmarkDurationSeconds=GetPrivateProfileIntW(\n        L"WIN81_NIS",L"BenchmarkDurationSeconds",10,iniPath);\n    PtF7BenchmarkConfigure(\n        selfModule,\n        g_ptarHudBenchmarkWarmupSeconds,\n        g_ptarHudBenchmarkDurationSeconds);\n}'''
assert h.count(old)==1, h.count(old)
h=h.replace(old,new,1)

# External F1/F5 messages remain highest priority; F7 BENCH is second.
old='''    if(PtDiagUiActive())\n        return PtDiagUiType();\n    return PtHudFeedbackActive()?g_ptarHudFeedbackType:0;'''
new='''    if(PtDiagUiActive())\n        return PtDiagUiType();\n    if(PtF7BenchmarkActive())\n        return 29; // F7 BENCH\n    return PtHudFeedbackActive()?g_ptarHudFeedbackType:0;'''
assert h.count(old)==1, h.count(old)
h=h.replace(old,new,1)

# Replace the permanent BUSY stub with the passive benchmark start contract.
old='''    if(f7 && !g_ptarHudPrevF7 && !ctrl)\n    {\n        // Benchmark backend is not yet ported to D3D9. Preserve the production\n        // HUD vocabulary instead of inventing another message.\n        PtHudFeedback(4,0,0,0); // F7 BUSY\n        PtDiagLogA("HOTKEY F7 Benchmark requested");\n    }'''
new='''    if(f7 && !g_ptarHudPrevF7 && !ctrl)\n    {\n        if(PtF7BenchmarkStart(g_ptarHudFgEnabled))\n        {\n            PtHudFeedback(29,0,0,0);\n            PtDiagLogA("HOTKEY F7 Benchmark started");\n        }\n        else\n        {\n            // Historical behavior: repeated F7 while a benchmark is already\n            // running is ignored instead of starting an overlapping window.\n            PtDiagLogA("INFO: F7 ignored: benchmark already in progress");\n        }\n    }'''
assert h.count(old)==1, h.count(old)
h=h.replace(old,new,1)

# Consume the completed result on the game thread, after the worker has stopped recording.
old='''static void PtHudFrameTick()\n{\n    PtDiagUiTick();\n    PtHudUpdateInput();\n    PtHudRecordRealFrame();\n}'''
new='''static void PtHudFrameTick()\n{\n    PtDiagUiTick();\n    PtHudUpdateInput();\n    PtHudRecordRealFrame();\n\n    PTF7BenchmarkResult benchmark={};\n    if(PtF7BenchmarkConsumeResult(&benchmark))\n    {\n        int avg=(int)(benchmark.averageVisibleFps+0.5);\n        int low=(int)(benchmark.onePercentLowFps+0.5);\n        int p99=(int)(benchmark.p99FrameMs+0.5);\n        if(avg<0) avg=0; if(avg>999) avg=999;\n        if(low<0) low=0; if(low>999) low=999;\n        if(p99<0) p99=0; if(p99>999) p99=999;\n        PtHudFeedback(30,avg,low,p99);\n    }\n}'''
assert h.count(old)==1, h.count(old)
h=h.replace(old,new,1)

# D3D9 CPU HUD: two F7-specific feedback panels only.
old='''        case 28:\n            PtGw16Text(dev,&batch,x,y,"C+F5 ERROR",fg);\n            break;\n        default:'''
new='''        case 28:\n            PtGw16Text(dev,&batch,x,y,"C+F5 ERROR",fg);\n            break;\n        case 29:\n            PtGw16Text(dev,&batch,x,y,"F7 BENCH",fg);\n            PtGw16Text(dev,&batch,x,originY+48,"MEASURE",fg);\n            break;\n        case 30:\n            PtGw16Text(dev,&batch,x,y,"F7 DONE",fg);\n            PtGw16Text(dev,&batch,x,originY+48,"AVG",fg);\n            PtGw16Number(dev,&batch,x+64,originY+48,(unsigned long)a,3,fg);\n            PtGw16Text(dev,&batch,x+128,originY+48,"LOW",fg);\n            PtGw16Number(dev,&batch,x+192,originY+48,(unsigned long)b,3,fg);\n            break;\n        default:'''
assert g.count(old)==1, g.count(old)
g=g.replace(old,new,1)

proxy.write_text(p,encoding='utf-8')
hud.write_text(h,encoding='utf-8')
hud9.write_text(g,encoding='utf-8')

# Static scope gates: the patch must not touch FG generation or pacer source.
assert '#include "ptar_f7_benchmark.h"' in p
assert p.count('PtF7BenchmarkRecordVisible(')==2
assert 'Benchmark backend is not yet ported to D3D9' not in h
assert 'PtF7BenchmarkStart(g_ptarHudFgEnabled)' in h
assert 'case 29:' in g and 'case 30:' in g

print('F7BENCH1_PATCH=PASS')
print('F7BENCH1_SCOPE=PASSIVE_PRESENT_MEASUREMENT_ONLY')
print('F7BENCH1_DEFAULT_WINDOW=3S_SETTLE_PLUS_10S_RECORD')
print('F7BENCH1_OVERLAP=IGNORED')
print('F7BENCH1_VISIBLE_PRESENT_HOOKS=2')
print('F7BENCH1_FG_CORE_CHANGE=NONE')
print('F7BENCH1_PACER_CHANGE=NONE')
