from pathlib import Path
import base64, gzip, hashlib, subprocess, sys

expected_parts = {
    'part00': (16000, '8d4ca68e6c01cc0415073642d4608d572047aec5a6d17402d308e5efa8346960'),
    'part01': (16000, '1ec85022621aab871a2603c3c4dbd2bd22c3a56b1ee60306a41e2e2cad3cf159'),
    'part02': (16000, '0822dac2a5cc04b6c583332f01f20f51bbbc8b4a3a51924fc2799d7d970a2048'),
    'part03': (16000, '0cc85ff5cab2ec8a92a8a0024e0bff16b3ad76460fe977c557e51a4bc14ca825'),
    'part04': (16000, '87891324db2d13b8f28496783c3b075146c994b87520e50621b0949635686419'),
    'part05': (16000, '4785a2224f3b82b94c9e8eda40c1183e98784b894e444be96da6a56baf79b77f'),
    'part06': (5364, 'c86e1eb8de2c43178be2f202018db14917ecc9a9c4de0d138ae811c3ca8d5c4d'),
}
parts = sorted(Path('.github/ptar_gw15_payload').glob('part*'))
if len(parts) != 7:
    raise SystemExit(f'expected 7 payload parts, got {len(parts)}')
for p in parts:
    data = p.read_bytes()
    actual = (len(data), hashlib.sha256(data).hexdigest())
    print(f'{p.name}: size={actual[0]} sha256={actual[1]}')
    if actual != expected_parts[p.name]:
        raise SystemExit(f'payload part mismatch: {p.name}')
encoded = b''.join(p.read_bytes() for p in parts)
script = gzip.decompress(base64.b64decode(encoded))
actual = hashlib.sha256(script).hexdigest()
expected = '1b98b24cea926d77dfc4ef38314cb54ec361940f72f6002e27a03eb2e7c16006'
if actual != expected:
    raise SystemExit(f'promotion script hash mismatch: {actual}')
out = Path('/tmp/promote_gw15.py')
out.write_bytes(script)
subprocess.run([sys.executable, str(out), '/tmp/ptar_main_base.dll'], check=True)
