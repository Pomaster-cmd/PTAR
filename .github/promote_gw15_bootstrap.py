from pathlib import Path
import base64, gzip, hashlib, subprocess, sys

parts = sorted(Path('.github/ptar_gw15_payload').glob('part*'))
if len(parts) != 7:
    raise SystemExit(f'expected 7 payload parts, got {len(parts)}')
encoded = b''.join(p.read_bytes() for p in parts)
if hashlib.sha256(encoded).hexdigest() != '3f593185e7ce5277991a637ec526439edfdd0e6c57d86f3a7ef1b91278013df7':
    raise SystemExit('encoded payload hash mismatch')
script = gzip.decompress(base64.b64decode(encoded))
if hashlib.sha256(script).hexdigest() != '1b98b24cea926d77dfc4ef38314cb54ec361940f72f6002e27a03eb2e7c16006':
    raise SystemExit('promotion script hash mismatch')
out = Path('/tmp/promote_gw15.py')
out.write_bytes(script)
subprocess.run([sys.executable, str(out), '/tmp/ptar_main_base.dll'], check=True)
