from pathlib import Path
import hashlib, sys

src=Path(sys.argv[1]); out=Path(sys.argv[2]); template=Path(sys.argv[3]); out.mkdir(parents=True,exist_ok=True)
VERSION='6126a747df4c366ce26a06000375c3dd060edde9efe5beef0da8cec2d47958a0'
EXPECT={
'install.ps1':'960da852ce03ad6cbafa383ac3e5be347b5ae7ae718aef40d9f76fa4323eb911',
'verify.ps1':'e1df00708faeda638f039cb17cc91988647d81ec505ce0db4027f852a25921d8',
'rollback.ps1':'c05638444bb60dfd6ad4cd5866eabdbf2eb9c61a50066da57e3a519d905b5aa3',
'uninstall_overlay.ps1':'1349f9437f73cc598b228fc1d069e8086db099a1802af81bed07991e635f66be',
'collect_rc39.ps1':'fbc9537938309be16268f4445448846c32abf2e61fda74f24d2df980c3e5d7bc'}

def rd(n): return (src/n).read_text(encoding='utf-8-sig')
def wr(n,s):
    s=s.replace('\r\n','\n').replace('\r','\n').replace('\n','\r\n')
    (out/n).write_bytes(b'\xef\xbb\xbf'+s.encode('utf-8'))
def h(n): return hashlib.sha256((out/n).read_bytes()).hexdigest()

s=rd('install.ps1')
s=s.replace('RC38_BORDERLESS_AUTHORITY','RC39_DEFERRED_AUTHORITY').replace('PTAR_RC38_INSTALL_LAST.log','PTAR_RC39_INSTALL_LAST.log')
s=s.replace("$ExpectedSidecar='d7eef6d9a2c01e40fed34722c86cee36003a636d663a67174515af5086d274f8'","$ExpectedSidecar='f8654c6d20f243a4ba75cece6a2ceb7df559d9e33ecad2c1275376038826e89b'")
s=s.replace("$ExpectedVersion='dcbccff254f8caec3823a5f57e4d8177428178aa6f0b05cbcfa911288548c8bc'",f"$ExpectedVersion='{VERSION}'")
s=s.replace("$KnownSidecar=@(\n '4bbf83ac8001c2ef6dabc40f58a3d69fb9e35b72fc922605430463595b2257bd',\n 'd7eef6d9a2c01e40fed34722c86cee36003a636d663a67174515af5086d274f8'\n)","$KnownSidecar=@(\n '4bbf83ac8001c2ef6dabc40f58a3d69fb9e35b72fc922605430463595b2257bd',\n 'd7eef6d9a2c01e40fed34722c86cee36003a636d663a67174515af5086d274f8',\n 'f8654c6d20f243a4ba75cece6a2ceb7df559d9e33ecad2c1275376038826e89b'\n)")
s=s.replace("schema=4;package='PTAR_RC39_DEFERRED_AUTHORITY'","schema=5;package='PTAR_RC39_DEFERRED_AUTHORITY'").replace('Payload RC38 hash mismatch.','Payload RC39 hash mismatch.').replace('MODE=RC38_BORDERLESS_AUTHORITY','MODE=RC39_DEFERRED_AUTHORITY')
wr('install.ps1',s)

s=rd('verify.ps1').replace('RC38_BORDERLESS_AUTHORITY','RC39_DEFERRED_AUTHORITY').replace('d7eef6d9a2c01e40fed34722c86cee36003a636d663a67174515af5086d274f8','f8654c6d20f243a4ba75cece6a2ceb7df559d9e33ecad2c1275376038826e89b').replace('dcbccff254f8caec3823a5f57e4d8177428178aa6f0b05cbcfa911288548c8bc',VERSION).replace('RC38 hook -> RVA','RC39 runtime hook -> RVA').replace('RC38 hook bytes','RC39 runtime hook bytes').replace('VERIFY_RC38_BORDERLESS_AUTHORITY=PASS','VERIFY_RC39_DEFERRED_AUTHORITY=PASS')
wr('verify.ps1',s)

s=rd('rollback.ps1').replace('RC38','RC39')
s=s.replace("'win81_nis_version.txt'='dcbccff254f8caec3823a5f57e4d8177428178aa6f0b05cbcfa911288548c8bc'",f"'win81_nis_version.txt'='{VERSION}'")
s=s.replace("foreach($n in $E.Keys){$src=Join-Path $base $n;if((Sha $src)-ne $E[$n]){Write-Host ('[FAIL] Base GitHub alteree: '+$n);exit 10};$dst=Join-Path $g $n;$cur=Sha $dst;if(($cur -ne $C[$n]) -and ($cur -ne $E[$n])){Write-Host ('[FAIL] Fichier actif inconnu/modifie, rollback refuse: '+$n+' '+$cur);exit 11}}","""foreach($n in $E.Keys){
 $src=Join-Path $base $n;if((Sha $src)-ne $E[$n]){Write-Host ('[FAIL] Base GitHub alteree: '+$n);exit 10}
 $dst=Join-Path $g $n;$cur=Sha $dst
 $allowed=@($C[$n],$E[$n])
 if($n -eq 'win81_nis_version.txt'){$allowed+=@('dcbccff254f8caec3823a5f57e4d8177428178aa6f0b05cbcfa911288548c8bc')}
 if($allowed -notcontains $cur){Write-Host ('[FAIL] Fichier actif inconnu/modifie, rollback refuse: '+$n+' '+$cur);exit 11}
}""")
s=s.replace("$side=Join-Path $g 'ptar_borderless.dll';$sh=Sha $side;if($sh -and ($sh -ne 'd7eef6d9a2c01e40fed34722c86cee36003a636d663a67174515af5086d274f8')){Write-Host ('[FAIL] Sidecar actif inconnu/modifie, rollback refuse: '+$sh);exit 13}","$side=Join-Path $g 'ptar_borderless.dll';$sh=Sha $side;$KnownSide=@('d7eef6d9a2c01e40fed34722c86cee36003a636d663a67174515af5086d274f8','f8654c6d20f243a4ba75cece6a2ceb7df559d9e33ecad2c1275376038826e89b');if($sh -and ($KnownSide -notcontains $sh)){Write-Host ('[FAIL] Sidecar actif inconnu/modifie, rollback refuse: '+$sh);exit 13}")
s=s.replace("if((Sha $side) -eq 'd7eef6d9a2c01e40fed34722c86cee36003a636d663a67174515af5086d274f8'){Remove-Item -LiteralPath $side -Force}","if($KnownSide -contains (Sha $side)){Remove-Item -LiteralPath $side -Force}").replace('rc38_sidecar_removed','rc39_sidecar_removed')
wr('rollback.ps1',s)

s=rd('uninstall_overlay.ps1').replace('RC38_BORDERLESS_AUTHORITY','RC39_DEFERRED_AUTHORITY').replace('PTAR_RC38_INSTALL_LAST.log','PTAR_RC39_INSTALL_LAST.log')
s=s.replace("$sidecarExpected='d7eef6d9a2c01e40fed34722c86cee36003a636d663a67174515af5086d274f8'","$sidecarExpected=@('d7eef6d9a2c01e40fed34722c86cee36003a636d663a67174515af5086d274f8','f8654c6d20f243a4ba75cece6a2ceb7df559d9e33ecad2c1275376038826e89b')").replace('if($h -eq $sidecarExpected){','if($sidecarExpected -contains $h){').replace('Sidecar RC38 retire','Sidecar RC39/RC38 connu retire').replace('Additif RC38 modifie','Additif RC39 modifie').replace('sidecar/additifs RC38','sidecar/additifs RC39')
wr('uninstall_overlay.ps1',s)

wr('collect_rc39.ps1',template.read_text(encoding='utf-8'))
for n,x in EXPECT.items():
    got=h(n)
    print(n,got)
    if got!=x: raise SystemExit(f'HASH MISMATCH {n}: {got} != {x}')
print('RC39_PACKAGE_SCRIPT_RECONSTRUCTION=PASS')
