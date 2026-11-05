"""Retire old texture import payloads; retain provenance and verified Git rollback."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
repo = root/'Assets/maps'
def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args])
def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for chunk in iter(lambda: handle.read(8*1024*1024), b''):
            digest.update(chunk)
    return digest.hexdigest()

prefixes = ('SourceAssets/SketchfabTextures/', 'SourceAssets/ProceduralTextures/')
suffixes = {'.zip', '.png', '.jpg', '.jpeg', '.blend'}
media = Path(next(line.split('=', 1)[1] for line in git('lfs', 'env').decode().splitlines() if line.startswith('LocalMediaDir=')))
files = []
retained = []
for name in git('ls-files', '-z').decode().split('\0'):
    if not name.startswith(prefixes) or Path(name).suffix.lower() not in suffixes:
        continue
    file = repo/name
    if not file.exists():
        continue
    if file.is_symlink() or file.resolve() != file.absolute():
        raise RuntimeError('Unexpected linked source: '+name)
    blob = git('show', 'HEAD:'+name)
    digest = sha(file)
    if blob.startswith(b'version https://git-lfs.github.com/spec/v1'):
        oid = next(line.split(':', 1)[1] for line in blob.decode().splitlines() if line.startswith('oid sha256:'))
        payload = media/oid[:2]/oid[2:4]/oid
        if digest != oid:
            raise RuntimeError('Source changed: '+name)
        if not payload.is_file() or sha(payload) != oid:
            retained.append({'file': name, 'reason': 'Git LFS rollback payload not locally verified'})
            continue
    elif hashlib.sha256(blob).hexdigest() != digest:
        raise RuntimeError('Source differs from Git: '+name)
    files.append({'file': name, 'bytes': file.stat().st_size, 'sha256': digest})
report = {'git_revision': git('rev-parse', 'HEAD').decode().strip(), 'files': files,
          'retained': retained,
          'bytes': sum(row['bytes'] for row in files), 'applied': '--apply' in sys.argv}
if report['applied']:
    for row in files:
        file = repo/row['file']
        if sha(file) != row['sha256']:
            raise RuntimeError('Source changed during retirement: '+str(file))
        file.unlink()
out = root/'Saved/Testing/CeilingPanels/retired-source-files.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps({'files': len(files), 'bytes': report['bytes'], 'applied': report['applied']}))
