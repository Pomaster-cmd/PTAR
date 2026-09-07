#!/usr/bin/env python3
from pathlib import Path
import hashlib,re,sys
ROOT=Path(__file__).resolve().parent.parent
DLL=ROOT/'payload/win81_nis_dx11_x64.dll'
INI=ROOT/'payload/win81_nis.ini'
B03=ROOT/'03-INSTALL_QSV_HELPER_WIN81.bat'
B04=ROOT/'04-TEST_QSV_5S_MP4.bat'
PS=ROOT/'tools/installer/INSTALL_QSV_HELPER_WIN81.ps1'
INSTALL=ROOT/'diag/install.ps1'
checks=[]
def ck(name,ok,detail=''):
    checks.append((name,bool(ok),detail))
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
# Exact production 1.0.6 batch files.
ck('PROD_03_BATCH_EXACT',sha(B03)=='6e57ef44bc1d76ef3ad62d4fe22f45ef35f9a4b50b76429e73eabb91cfd1935d',sha(B03))
ck('PROD_04_BATCH_EXACT',sha(B04)=='cb009622b1e44b3f3666ab3e96950b05647ceac8b4b27880c266f87086e901e2',sha(B04))
ps=PS.read_text(encoding='utf-8',errors='strict')
ck('HELPER_NAME_B18K17','b18k17_ffmpeg.exe' in ps)
ck('READY_MARKER','B18K18_QSV_READY.txt' in ps)
ck('NO_OVERWRITE_TARGET','Aucun ecrasement automatique' in ps)
ck('H264_QSV_PROBE',"return ($text -match 'h264_qsv')" in ps)
ck('LOCAL_PLUS_TARGET_COPY',"$LocalFF" in ps and "$TargetFF" in ps and 'Copy-Item -LiteralPath $LocalFF -Destination $TargetFF -Force' in ps)
ck('LIVE_FFMPEG_URL','ffbinaries/ffbinaries-prebuilt/releases/download/v4.4.1/ffmpeg-4.4.1-win-64.zip' in ps)
ck('TLS12','Tls12' in ps)
ck('PRODUCTION_LOG_NAME','INSTALL_QSV_HELPER_WIN81.log' in ps)
# Runtime contract really expects b18k17 helper, not the invented b18k18 name.
d=DLL.read_bytes()
utf16=lambda s:s.encode('utf-16le')
ck('RUNTIME_EXPECTS_B18K17',utf16(r'tools\qsv\b18k17_ffmpeg.exe') in d)
ck('RUNTIME_NOT_B18K18_HELPER',utf16(r'tools\qsv\b18k18_ffmpeg.exe') not in d)
ck('RUNTIME_CTRL_F9_RECORDER',b'CTRL+F9 toggles the FG-only fixed-QPC Intel QSV MP4 recorder' in d)
# INI production profile behavior.
ini=INI.read_text(encoding='utf-8',errors='strict')
ck('INI_PROFILE3_DEFAULT',re.search(r'(?m)^VideoRecordProfile=3\s*$',ini) is not None)
ck('INI_VIDEO_CTRL_F9',re.search(r'(?m)^VideoRecord=CTRL\+F9\s*$',ini) is not None)
# Production test is profile-aware, not a fixed 540p-only synthetic contract.
b04=B04.read_text(encoding='utf-8',errors='strict')
for token in ['VideoRecordProfile=','1920','1080','1600','900','22000','17000','16000','PROFILE !PROFILE!']:
    ck('TEST_PROFILE_'+token.replace(' ','_').replace('!','').replace('=','EQ'), token in b04)
ck('TEST_WRITES_GAME_RECORDINGS',r'recordings\PTAR_PROFILE' in b04)
ck('TEST_MANUAL_PAUSE','pause' in b04.lower())
b03=B03.read_text(encoding='utf-8',errors='strict')
ck('INSTALL_MANUAL_PAUSE','pause' in b03.lower())
# Current GW16 installer hands exact game root to original production scripts.
ins=INSTALL.read_text(encoding='utf-8',errors='strict')
ck('TARGET_HANDOFF_WRITTEN',"win81_nis_install_target.txt" in ins and '-Value $g' in ins)
# Production payload identity retained.
ck('GW16H_RUNTIME_SHA',sha(DLL)=='864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c',sha(DLL))
failed=[x for x in checks if not x[1]]
for n,ok,detail in checks:
    print(('PASS ' if ok else 'FAIL ')+n+((' :: '+detail) if detail else ''))
print('TOTAL=%d PASS=%d FAIL=%d'%(len(checks),len(checks)-len(failed),len(failed)))
sys.exit(1 if failed else 0)
