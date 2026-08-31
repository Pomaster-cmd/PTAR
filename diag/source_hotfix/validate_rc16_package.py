#!/usr/bin/env python3
from pathlib import Path
import hashlib,re,sys
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
EXP='f5873962a1add8e5a18dd2f0ee628d01700fb63a269dfe04016ef7104b83820b'
RC14='3808fcb1681f00342b7ce07e39aebc2b1cabbed8aeb2fd244788744136d1868f'
RC15='2c978a59cc7053edc6669de18e0dcf2ae5755b9b96da74c452c9b19405c1d785'
checks=[]
def ck(n,c):checks.append((n,bool(c)));print(('[PASS] ' if c else '[FAIL] ')+n)
def sha(p): return hashlib.sha256((ROOT/p).read_bytes()).hexdigest()
def text(p): return (ROOT/p).read_text(errors='replace')
ck('runtime exists',(ROOT/'win81_nis_dx11_x64.dll').is_file())
ck('runtime hash exact',sha('win81_nis_dx11_x64.dll')==EXP)
ck('RC14 field-good reference exact',sha('diag/r/r067.dll')==RC14)
ck('RC15 rejected reference exact',sha('diag/r/r068.dll')==RC15)
for p in ['01-INSTALL_FULLSTACK1.bat','VERIFY_FULLSTACK1_INSTALL.bat','diag/05-COLLECT_RESULTS.bat','win81_nis.ini','diag/PTAR_PACKAGE_ID.txt','diag/PRODUCTION_VALIDATION.txt','diag/PTAR_README.txt','diag/RC15_FIELD_FINDING_STARTUP_PRESENTER_REGRESSION.txt','diag/FGQUALITYHUD2_HARDWARE_PROTOCOL.txt','diag/FGQUALITYHUD2_STATIC_VALIDATION.txt','diag/FGQUALITYHUD2_DETERMINISM.txt','diag/source_hotfix/fgquality_hud2.s','diag/source_hotfix/fgquality_hud2.obj','diag/source_hotfix/patch_fgqualityhud2_rc14.py','diag/source_hotfix/validate_fgqualityhud2_rc16.py']:
 ck('asset '+p,(ROOT/p).is_file())
ini=text('win81_nis.ini')
for s in ['Enabled=1','UniversalSpatialPresenter=1','Overlay=1','FrameGenerationQuality=2','FrameGeneration=0','FrameGenerationRequireVSync=1','FrameGenerationTargetFPS=60','FrameGenerationBlendGuard=35','FrameGenerationMEScalePercent=50','VideoRecordProfile=3']:
 ck('INI '+s,s in ini)
ck('INI no hotkey collision: Status=F8','Status=F8' in ini)
inst=text('tools/installer/PTAR_CORE_INSTALL.bat'); ver=text('VERIFY_FULLSTACK1_INSTALL.bat'); col=text('diag/05-COLLECT_RESULTS.bat')
ck('installer RC16 marker','RC16' in inst and 'FGQUALITYHUD2' in inst)
ck('installer exact runtime hash',EXP in inst)
ck('installer emits startup parser policy','RC16_STARTUP_PARSER_POLICY=BYTE_IDENTICAL_TO_RC14_NO_HUD_WRAPPER_NO_EARLY_QUALITY_LOG' in inst)
ck('installer keeps CTRL+F8 and F8 roles','CTRL+F8' in inst and 'F8 reste strictement le statut' in inst)
ck('verifier exact runtime hash',EXP in ver)
ck('verifier RC16 marker','RC16' in ver and 'FGQUALITYHUD2' in ver)
ck('verifier checks startup parser marker','RC16_STARTUP_PARSER_POLICY=BYTE_IDENTICAL_TO_RC14_NO_HUD_WRAPPER_NO_EARLY_QUALITY_LOG' in ver)
ck('collector RC16 marker','RC16' in col and 'FGQUALITYHUD2' in col)
ck('collector GTX960M field marker','GTX_960M_WINDOWS_8_1' in col)
# Install/verify/collector must not delete arbitrary files.
for p,s in [('installer',inst),('verifier',ver),('collector',col)]:
 bad=bool(re.search(r'(?im)^\s*(del|erase|rd\s+/s|rmdir\s+/s)\b',s))
 ck(p+' no destructive delete command',not bad)
pid=text('diag/PTAR_PACKAGE_ID.txt')
for s in ['RC15_REJECTED_BLACK_STARTUP_NO_HUD_NO_RUNTIME_LOG_IMAGE_ONLY_AFTER_ALT_ENTER','RC16_STARTUP_PARSER_POLICY=BYTE_IDENTICAL_TO_RC14_NO_HUD_WRAPPER_NO_EARLY_QUALITY_LOG','FG_QUALITY_HUD=FGQUALITYHUD2_RUNTIME_ONLY_ACTIVE_SELECTED_PENDING_NOTICE','FG_AUTO_VSYNC_IMPLEMENTED=NO','RECORDER_CHANGED=NO']:
 ck('package ID '+s,s in pid)
prod=text('diag/PRODUCTION_VALIDATION.txt')
ck('production validation says parser byte-identical','parser call at RVA 0x2B46 is byte-identical' in prod)
ck('production validation says RC15 rejected','RC15 is rejected' in prod)
ck('hardware protocol tests startup before FG','Do NOT press Alt+Enter' in text('diag/FGQUALITYHUD2_HARDWARE_PROTOCOL.txt') and 'do not toggle FG' in text('diag/FGQUALITYHUD2_HARDWARE_PROTOCOL.txt'))
ck('binary validation passed','PASS 34/34' in text('diag/FGQUALITYHUD2_STATIC_VALIDATION.txt'))
ck('determinism object pass','OBJECT_REBUILD_1_EQ_2=PASS' in text('diag/FGQUALITYHUD2_DETERMINISM.txt'))
ck('determinism DLL pass','DLL_REBUILD_1_EQ_2=PASS' in text('diag/FGQUALITYHUD2_DETERMINISM.txt'))
passed=sum(v for _,v in checks); total=len(checks)
print(f'RC16 PACKAGE STATIC VALIDATION: {"PASS" if passed==total else "FAIL"} {passed}/{total}')
if passed!=total:sys.exit(1)
