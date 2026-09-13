from pathlib import Path
import hashlib,sys

src=Path(sys.argv[1]); out=Path(sys.argv[2]); verify_template=Path(sys.argv[3]); out.mkdir(parents=True,exist_ok=True)
VERSION='f32d1b3b79a2d5a4973cb28cb1207326dbc42a02c4647489debd3cba3786fc99'
EXPECT={
'install.ps1':'f0f782831918526902af71e499f3c162f267f944f789f7b65b21b861bc726c0d',
'verify.ps1':'6cd92296f7e22eef7ce30c559836545bf12dba37ce341222fd05a9a1d659abbd',
'rollback.ps1':'802c31b4688f3996f9f5eee82b8541280634fb74bd5f9f944c6f8015e495a3b3',
'uninstall_overlay.ps1':'d67d963b40c75a6c219ddc221d15ad3bfa31bc36d886e6432ef7ebbeea9ae99f',
'collect_rc40.ps1':'b9a565ddcd1ebc258e3d018aa1bd41de7ac8923d73f99f8f02549a3b9be03faf'}

def rd(n): return (src/n).read_text(encoding='utf-8-sig')
def wr(n,s):
    s=s.replace('\r\n','\n').replace('\r','\n').replace('\n','\r\n')
    (out/n).write_bytes(b'\xef\xbb\xbf'+s.encode('utf-8'))
def h(n): return hashlib.sha256((out/n).read_bytes()).hexdigest()

s=rd('install.ps1')
s=s.replace('PTAR_RC39_INSTALL_LAST.log','PTAR_RC40_INSTALL_LAST.log')
s=s.replace("$ExpectedRuntime='cb7202e5097b78c4965a8300a28a6004038d05bbfe1aaa25089e0a2c5f1b8abb'","$ExpectedRuntime='774f88c976ed296d7496d5fce1fad9f8e1056337ee70e0111371866074e75669'")
s=s.replace("$ExpectedSidecar='f8654c6d20f243a4ba75cece6a2ceb7df559d9e33ecad2c1275376038826e89b'","$ExpectedSidecar='ce497e72837f95503f877f73239c63a692c90b8646dbf7a3bbd4ba8082083416'")
s=s.replace("$ExpectedVersion='6126a747df4c366ce26a06000375c3dd060edde9efe5beef0da8cec2d47958a0'",f"$ExpectedVersion='{VERSION}'")
s=s.replace(" 'cb7202e5097b78c4965a8300a28a6004038d05bbfe1aaa25089e0a2c5f1b8abb'\n)"," 'cb7202e5097b78c4965a8300a28a6004038d05bbfe1aaa25089e0a2c5f1b8abb',\n '774f88c976ed296d7496d5fce1fad9f8e1056337ee70e0111371866074e75669'\n)")
s=s.replace(" 'f8654c6d20f243a4ba75cece6a2ceb7df559d9e33ecad2c1275376038826e89b'\n)"," 'f8654c6d20f243a4ba75cece6a2ceb7df559d9e33ecad2c1275376038826e89b',\n 'ce497e72837f95503f877f73239c63a692c90b8646dbf7a3bbd4ba8082083416'\n)")
s=s.replace('Payload RC39 hash mismatch.','Payload RC40 hash mismatch.')
s=s.replace("package='PTAR_RC39_DEFERRED_AUTHORITY'","package='PTAR_RC40_POSTWNDPROC_AUTHORITY'")
s=s.replace("L 'MODE=RC39_DEFERRED_AUTHORITY'","L 'MODE=RC40_POSTWNDPROC_AUTHORITY'")
wr('install.ps1',s)

wr('verify.ps1',verify_template.read_text(encoding='utf-8'))

s=rd('rollback.ps1')
s=s.replace("$C=@{'d3d11.dll'='cb7202e5097b78c4965a8300a28a6004038d05bbfe1aaa25089e0a2c5f1b8abb';'win81_nis_dx11_x64.dll'='cb7202e5097b78c4965a8300a28a6004038d05bbfe1aaa25089e0a2c5f1b8abb';'win81_nis.ini'='bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50';'win81_nis_version.txt'='6126a747df4c366ce26a06000375c3dd060edde9efe5beef0da8cec2d47958a0'}",f"$C=@{{'d3d11.dll'='774f88c976ed296d7496d5fce1fad9f8e1056337ee70e0111371866074e75669';'win81_nis_dx11_x64.dll'='774f88c976ed296d7496d5fce1fad9f8e1056337ee70e0111371866074e75669';'win81_nis.ini'='bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50';'win81_nis_version.txt'='{VERSION}'}}")
s=s.replace("$allowed=@($C[$n],$E[$n])","$allowed=@($C[$n],$E[$n]);if(($n -eq 'd3d11.dll') -or ($n -eq 'win81_nis_dx11_x64.dll')){$allowed+='cb7202e5097b78c4965a8300a28a6004038d05bbfe1aaa25089e0a2c5f1b8abb'}")
s=s.replace("$allowed+=@('dcbccff254f8caec3823a5f57e4d8177428178aa6f0b05cbcfa911288548c8bc')","$allowed+=@('dcbccff254f8caec3823a5f57e4d8177428178aa6f0b05cbcfa911288548c8bc','6126a747df4c366ce26a06000375c3dd060edde9efe5beef0da8cec2d47958a0')")
s=s.replace("$KnownSide=@('d7eef6d9a2c01e40fed34722c86cee36003a636d663a67174515af5086d274f8','f8654c6d20f243a4ba75cece6a2ceb7df559d9e33ecad2c1275376038826e89b')","$KnownSide=@('d7eef6d9a2c01e40fed34722c86cee36003a636d663a67174515af5086d274f8','f8654c6d20f243a4ba75cece6a2ceb7df559d9e33ecad2c1275376038826e89b','ce497e72837f95503f877f73239c63a692c90b8646dbf7a3bbd4ba8082083416')")
s=s.replace('rc39_sidecar_removed','rc40_sidecar_removed').replace('[PASS] RC39 retire;','[PASS] RC40 retire;')
wr('rollback.ps1',s)

s=rd('uninstall_overlay.ps1').replace('RC39_DEFERRED_AUTHORITY','RC40_POSTWNDPROC_AUTHORITY').replace('PTAR_RC39_INSTALL_LAST.log','PTAR_RC40_INSTALL_LAST.log')
s=s.replace("@('d7eef6d9a2c01e40fed34722c86cee36003a636d663a67174515af5086d274f8','f8654c6d20f243a4ba75cece6a2ceb7df559d9e33ecad2c1275376038826e89b')","@('d7eef6d9a2c01e40fed34722c86cee36003a636d663a67174515af5086d274f8','f8654c6d20f243a4ba75cece6a2ceb7df559d9e33ecad2c1275376038826e89b','ce497e72837f95503f877f73239c63a692c90b8646dbf7a3bbd4ba8082083416')")
s=s.replace('Sidecar RC39/RC38 connu','Sidecar RC40/RC39/RC38 connu').replace('Additif RC39 modifie','Additif RC40 modifie').replace('additifs RC39','additifs RC40')
wr('uninstall_overlay.ps1',s)

s=rd('collect_rc39.ps1').replace('RC39','RC40')
wr('collect_rc40.ps1',s)

for n,x in EXPECT.items():
    got=h(n); print(n,got)
    if got!=x: raise SystemExit(f'HASH MISMATCH {n}: {got} != {x}')
print('RC40_PACKAGE_SCRIPT_RECONSTRUCTION=PASS')
