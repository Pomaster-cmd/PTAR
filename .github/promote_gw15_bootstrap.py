from pathlib import Path
import base64, gzip, hashlib, subprocess, sys

parts = sorted(Path('.github/ptar_gw15_payload').glob('part*'))
if len(parts) != 7:
    raise SystemExit(f'expected 7 payload parts, got {len(parts)}')
encoded = b''.join(p.read_bytes() for p in parts)
script = gzip.decompress(base64.b64decode(encoded))
actual = hashlib.sha256(script).hexdigest()
expected = '1b98b24cea926d77dfc4ef38314cb54ec361940f72f6002e27a03eb2e7c16006'
if actual != expected:
    raise SystemExit(f'promotion script hash mismatch: {actual}')
out = Path('/tmp/promote_gw15.py')
out.write_bytes(script)
subprocess.run([sys.executable, str(out), '/tmp/ptar_main_base.dll'], check=True)
