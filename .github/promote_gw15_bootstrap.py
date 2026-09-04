from pathlib import Path
import base64, gzip, hashlib, subprocess, sys

sequence = [
    ('part00', 16000, '8d4ca68e6c01cc0415073642d4608d572047aec5a6d17402d308e5efa8346960'),
    ('part01', 16000, '1ec85022621aab871a2603c3c4dbd2bd22c3a56b1ee60306a41e2e2cad3cf159'),
    ('part02a', 8000, 'd30cfb940c2afa951046dc348cbe6ce69bea70b939d5433c96a73799c989beb6'),
    ('part02b', 8000, '6b644169c67396588fdfa7ca1f71dfa81f20d0866673daa03ad28d4e66c2ea30'),
    ('part03', 16000, '0cc85ff5cab2ec8a92a8a0024e0bff16b3ad76460fe977c557e51a4bc14ca825'),
    ('part04', 16000, '87891324db2d13b8f28496783c3b075146c994b87520e50621b0949635686419'),
    ('part05', 16000, '4785a2224f3b82b94c9e8eda40c1183e98784b894e444be96da6a56baf79b77f'),
    ('part06', 5364, 'c86e1eb8de2c43178be2f202018db14917ecc9a9c4de0d138ae811c3ca8d5c4d'),
]
chunks = []
for name, expected_size, expected_sha in sequence:
    p = Path('.github/ptar_gw15_payload') / name
    data = p.read_bytes()
    actual_sha = hashlib.sha256(data).hexdigest()
    print(f'{name}: size={len(data)} sha256={actual_sha}')
    if len(data) != expected_size or actual_sha != expected_sha:
        raise SystemExit(f'payload part mismatch: {name}')
    chunks.append(data)
encoded = b''.join(chunks)
script = gzip.decompress(base64.b64decode(encoded))
actual = hashlib.sha256(script).hexdigest()
expected = '1b98b24cea926d77dfc4ef38314cb54ec361940f72f6002e27a03eb2e7c16006'
if actual != expected:
    raise SystemExit(f'promotion script hash mismatch: {actual}')
out = Path('/tmp/promote_gw15.py')
out.write_bytes(script)
subprocess.run([sys.executable, str(out), '/tmp/ptar_main_base.dll'], check=True)
