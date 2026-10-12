import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SCRATCH = Path.home() / '.cache/velocita-build/native-transport-20261012'
MUTANTS = [
    ('system_include_path', "if index == len(result) or not re.fullmatch(r'/[-a-zA-Z0-9_./+]+', result[index]):",
     "if index == len(result):", 'test_system_include_flags_are_bounded_pairs', 'unsafe_pkg_flags_refused'),
    ('manifest_content', "if manifest != {'schema': 1, 'fingerprint': actual}:", 'if False:',
     'test_manifest_content_must_match', 'dependency_mismatch_refused_before_compile'),
    ('manifest_hash', "if not re.fullmatch('[0-9a-f]{64}', expected_sha256) or digest(data) != expected_sha256:",
     'if False:', 'test_manifest_hash_must_match', 'manifest_hash_must_match'),
    ('source_clean', "if git(source, 'status', '--porcelain=v1', '--untracked-files=all', '--ignored=matching'):",
     'if False:', 'test_source_rejects_wrong_commit_and_dirty_files', 'untracked_source_refused'),
    ('runtime_ssl', "or not runtime.get('tls_backend') or runtime.get('ssl') is not True",
     "or not runtime.get('tls_backend') or False", 'test_runtime_requirements', 'runtime_dependencies_must_match'),
    ('runtime_async', "or runtime.get('async_dns') is not True):", 'or False):',
     'test_runtime_requirements', 'runtime_dependencies_must_match'),
]


def main():
    SCRATCH.mkdir(parents=True, exist_ok=True)
    result_dir = Path(tempfile.mkdtemp(prefix='mutation-', dir=SCRATCH))
    original = (ROOT / 'tools/qualify-swap-transport.py').read_text()
    results = []
    for name, before, after, test, assertion in MUTANTS:
        if original.count(before) != 1:
            raise RuntimeError('mutation anchor not unique: ' + name)
        target = result_dir / name
        for rel in ('tools/qualify-swap-transport.py', 'tests/test_qualify_swap_transport.py',
                    'include/velocita/v2/swap_https.hpp', 'src/v2_swap_https.cpp',
                    'tests/v2_swap_https_probe.cpp', 'tests/test_swap_https.py'):
            (target / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / rel, target / rel)
        (target / 'tools/qualify-swap-transport.py').write_text(original.replace(before, after))
        process = subprocess.run([sys.executable, '-B', '-m', 'unittest',
                                  'test_qualify_swap_transport.QualificationTests.' + test],
                                 cwd=target / 'tests', capture_output=True, text=True, timeout=30)
        log = process.stdout + process.stderr
        (target / 'result.log').write_text(log)
        killed = (process.returncode != 0 and assertion in log and 'AssertionError' in log
                  and 'ERROR:' not in log)
        results.append({'mutation': name, 'named_assertion': assertion, 'killed': killed})
    (result_dir / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps({'results': results, 'evidence': str(result_dir)}, indent=2))
    return 0 if all(item['killed'] for item in results) else 1


if __name__ == '__main__':
    sys.exit(main())
