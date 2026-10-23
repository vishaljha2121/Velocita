"""Export completed offline logs and synthetic drill evidence, never credentials."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import sys
import tarfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--commit', required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[0-9a-f]{40}', args.commit):
        raise SystemExit('exact lowercase commit required')
    root = Path.home()/('.cache/velocita-build/bounded-native-20261013-'+args.commit)
    if root.is_symlink() or not root.is_dir():
        raise SystemExit('real evidence directory required')
    status = json.loads((root/'status.json').read_text())
    if status.get('commit') != args.commit or not (status.get('qualified') or status.get('stage') == 'failed'):
        raise SystemExit('only completed or failed evidence may be collected')
    files = [p for p in sorted(root.iterdir()) if p.is_file() and p.suffix in ('.json', '.log', '.xml')]
    allowed = {'result.json', 'venue.log', 'processes.jsonl', 'shutdown.jsonl', 'replicas.jsonl'}
    files += [p for p in sorted(root.glob('home/.cache/velocita-build/process-drill-20261013/run-*/case-*/*'))
              if p.name in allowed and p.is_file()]
    files += [p for p in sorted(root.glob('home/.cache/velocita-build/continuous-*/c*/*'))
              if p.name in {'result.json', 'events.log'} and p.is_file()]
    if not all(not p.is_symlink() and p.resolve().is_relative_to(root.resolve()) for p in files):
        raise SystemExit('unexpected evidence symlink/path')
    manifest = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    with tarfile.open(fileobj=sys.stdout.buffer, mode='w|gz') as archive:
        for p in files:
            archive.add(p, arcname=str(p.relative_to(root)), recursive=False)
        body = (json.dumps(manifest, indent=2)+'\n').encode()
        info = tarfile.TarInfo('artifact-manifest.json')
        info.size = len(body)
        archive.addfile(info, io.BytesIO(body))


if __name__ == '__main__':
    main()
