import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys

SOURCE_FILES = ('include/velocita/v2/swap_https.hpp', 'src/v2_swap_https.cpp',
                'tests/v2_swap_https_probe.cpp', 'tests/test_swap_https.py',
                'tools/qualify-swap-transport.py')


class Refusal(ValueError):
    pass


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_env():
    return {'PATH': '/usr/bin:/bin', 'HOME': str(Path.home()), 'LC_ALL': 'C',
            'GIT_CONFIG_NOSYSTEM': '1', 'GIT_CONFIG_GLOBAL': '/dev/null',
            'GIT_OPTIONAL_LOCKS': '0', 'GIT_NO_REPLACE_OBJECTS': '1',
            'PYTHONDONTWRITEBYTECODE': '1', 'PYTHONNOUSERSITE': '1'}


def command(args, *, cwd=None, timeout=30, env=None):
    result = subprocess.run(args, cwd=cwd, env=safe_env() if env is None else env,
                            capture_output=True, timeout=timeout)
    if result.returncode:
        raise Refusal('command failed: ' + str(args[0]) + ' exit=' + str(result.returncode))
    if len(result.stdout) > 4 * 1024 * 1024:
        raise Refusal('command output exceeds bound')
    return result.stdout


def git(source, *args):
    return command(['/usr/bin/git', '-c', 'core.hooksPath=/dev/null', '-c', 'core.fsmonitor=false',
                    '-c', 'core.untrackedCache=false', '-c', 'core.pager=cat', '-C', str(source), *args])


def verify_source(source, commit):
    if not re.fullmatch('[0-9a-f]{40}', commit):
        raise Refusal('full source commit required')
    if git(source, 'rev-parse', 'HEAD').decode().strip() != commit:
        raise Refusal('checkout differs from requested commit')
    if git(source, 'status', '--porcelain=v1', '--untracked-files=all', '--ignored=matching'):
        raise Refusal('isolated clean checkout required, including untracked and ignored files')
    exported = {}
    for rel in SOURCE_FILES:
        entry = git(source, 'ls-tree', commit, '--', rel).decode()
        if not entry.startswith(('100644 blob ', '100755 blob ')):
            raise Refusal('required regular committed source absent')
        data = git(source, 'show', commit + ':' + rel)
        target = source / rel
        if target.is_symlink() or target.read_bytes() != data:
            raise Refusal('working source differs from commit')
        exported[rel] = data
    if exported['tools/qualify-swap-transport.py'] != Path(__file__).read_bytes():
        raise Refusal('running qualification script differs from commit')
    return exported


def verify_manifest(path, expected_sha256, actual):
    data = path.read_bytes()
    if not re.fullmatch('[0-9a-f]{64}', expected_sha256) or digest(data) != expected_sha256:
        raise Refusal('manifest hash mismatch')
    manifest = json.loads(data)
    if manifest != {'schema': 1, 'fingerprint': actual}:
        raise Refusal('installed dependency fingerprint differs from reviewed manifest')


def verify_runtime(runtime, expected_version):
    if (runtime.get('curl_headers') != expected_version or runtime.get('curl_runtime') != expected_version
            or not runtime.get('tls_backend') or runtime.get('ssl') is not True
            or runtime.get('async_dns') is not True):
        raise Refusal('linked runtime does not match required curl/TLS capabilities')


def file_identity(path):
    resolved = Path(path).resolve(strict=True)
    if not resolved.is_file():
        raise Refusal('dependency is not a regular file')
    return {'path': str(resolved), 'sha256': digest(resolved.read_bytes())}


def dependencies(path):
    listing = command(['/usr/bin/ldd', str(path)]).decode()
    if 'not found' in listing:
        raise Refusal('unresolved dynamic dependency')
    paths = set(re.findall(r'(?:=>\s+|^\s*)(/[^\s]+)', listing, re.MULTILINE))
    if not paths:
        raise Refusal('dynamic dependency inventory unavailable')
    return [file_identity(p) for p in sorted(paths)]


def flags(value):
    result = shlex.split(value)
    if any(not re.fullmatch(r'-(?:I/[-a-zA-Z0-9_./+]+|L/[-a-zA-Z0-9_./+]+|l[a-zA-Z0-9_+.-]+|pthread)',
                            token) for token in result):
        raise Refusal('pkg-config returned unsupported flags')
    return result


def fingerprint():
    if platform.system() != 'Linux' or platform.machine() not in ('x86_64', 'aarch64'):
        raise Refusal('native Linux x86_64 or aarch64 required')
    result = {'system': platform.system(), 'arch': platform.machine(),
              'os_release': Path('/etc/os-release').read_text(), 'tools': {}}
    for name, path, version_args in (
            ('compiler', '/usr/bin/c++', ['--version']),
            ('pkg_config', '/usr/bin/pkg-config', ['--version']),
            ('openssl', '/usr/bin/openssl', ['version', '-a']),
            ('python', str(Path(sys.executable).resolve()), ['--version'])):
        identity = file_identity(path)
        result['tools'][name] = dict(identity, version=command([path, *version_args]).decode().strip(),
                                    dependencies=dependencies(identity['path']))
    pkg = '/usr/bin/pkg-config'
    version = command([pkg, '--modversion', 'libcurl']).decode().strip()
    match = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)', version)
    if not match or tuple(map(int, match.groups())) < (7, 85, 0):
        raise Refusal('libcurl development version must be at least 7.85.0')
    include_dir = command([pkg, '--variable=includedir', 'libcurl']).decode().strip()
    lib_dir = command([pkg, '--variable=libdir', 'libcurl']).decode().strip()
    result['curl_version'] = version
    result['cflags'] = flags(command([pkg, '--cflags', 'libcurl']).decode())
    result['libs'] = flags(command([pkg, '--libs', 'libcurl']).decode())
    headers = sorted((Path(include_dir) / 'curl').glob('*.h'))
    if not any(p.name == 'curl.h' for p in headers) or not any(p.name == 'curlver.h' for p in headers):
        raise Refusal('libcurl public headers absent')
    result['headers'] = [file_identity(p) for p in headers]
    library = file_identity(Path(lib_dir) / 'libcurl.so')
    result['curl_library'] = library
    result['curl_dependencies'] = dependencies(library['path'])
    for component in ('cc1plus', 'as', 'ld'):
        name = command(['/usr/bin/c++', '-print-prog-name=' + component]).decode().strip()
        path = name if '/' in name else shutil.which(name, path='/usr/bin:/bin')
        if not path:
            raise Refusal('compiler component absent')
        result['tools'][component] = dict(file_identity(path), dependencies=dependencies(path))
    ssl_data = json.loads(command([sys.executable, '-I', '-c',
                                  'import ssl, _ssl, json; print(json.dumps([ssl.OPENSSL_VERSION, _ssl.__file__]))']))
    result['python_ssl'] = {'version': ssl_data[0], 'module': file_identity(ssl_data[1]),
                            'dependencies': dependencies(ssl_data[1])}
    return result


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def run(args):
    source = args.source.resolve()
    output = args.output.resolve()
    if output == source or source in output.parents:
        raise Refusal('output must be outside source checkout')
    if output.exists():
        raise Refusal('output must be a new directory')
    if args.mode == 'run' and not all((args.commit, args.expected_arch, args.toolchain_manifest,
                                      args.toolchain_manifest_sha256)):
        raise Refusal('run requires explicit commit, architecture and reviewed manifest/hash')
    output.mkdir(parents=True)
    write_json(output / 'status.json', {'qualified': False, 'phase': 'preflight'})
    actual = fingerprint()
    write_json(output / 'candidate-manifest.json', {'schema': 1, 'fingerprint': actual})
    if args.mode == 'fingerprint':
        return
    if actual['arch'] != args.expected_arch:
        raise Refusal('architecture differs from requested target')
    verify_manifest(args.toolchain_manifest, args.toolchain_manifest_sha256, actual)
    exported = verify_source(source, args.commit)
    export = output / 'source'
    for rel, data in exported.items():
        target = export / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    binary = output / 'swap-https-probe'
    compile_command = [actual['tools']['compiler']['path'], '-std=c++20', '-O1', '-g',
                       '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                       '-fno-omit-frame-pointer', '-DVELOCITA_SWAP_HTTPS_TEST_ONLY=1',
                       '-I' + str(export / 'include'), *actual['cflags'],
                       str(export / 'src/v2_swap_https.cpp'), str(export / 'tests/v2_swap_https_probe.cpp'),
                       *actual['libs'], '-o', str(binary)]
    write_json(output / 'compile-command.json', compile_command)
    result = subprocess.run(compile_command, env=safe_env(), capture_output=True, timeout=300)
    (output / 'compile.log').write_bytes(result.stdout + result.stderr)
    if result.returncode:
        raise Refusal('targeted probe compilation failed; inspect compile.log')
    linked = dependencies(binary)
    if actual['curl_library'] not in linked:
        raise Refusal('probe linked a different libcurl')
    runtime = json.loads(command([str(binary), '--fingerprint']))
    verify_runtime(runtime, actual['curl_version'])
    write_json(output / 'runtime.json', runtime)
    if fingerprint() != actual:
        raise Refusal('dependencies changed during build')
    env = dict(safe_env(), VELOCITA_SWAP_HTTPS_PROBE=str(binary),
               VELOCITA_TEST_OPENSSL=actual['tools']['openssl']['path'],
               ASAN_OPTIONS='detect_leaks=1', UBSAN_OPTIONS='halt_on_error=1')
    result = subprocess.run([actual['tools']['python']['path'], '-I', '-B',
                             str(export / 'tests/test_swap_https.py'), '-v'],
                            env=env, capture_output=True, timeout=120)
    (output / 'fake-tls.log').write_bytes(result.stdout + result.stderr)
    if result.returncode or b'Ran 10 tests' not in result.stderr or b'\nOK\n' not in result.stderr:
        raise Refusal('fake TLS suite did not complete all ten tests')
    if fingerprint() != actual:
        raise Refusal('dependencies changed during tests')
    write_json(output / 'status.json', {'qualified': True, 'scope': 'native isolated fake TLS only',
               'commit': args.commit, 'arch': actual['arch'], 'source_sha256': {
                   rel: digest(data) for rel, data in exported.items()}, 'probe': file_identity(binary),
               'linked_dependencies': linked, 'manifest_sha256': args.toolchain_manifest_sha256,
               'production_qualified': False})


def main():
    parser = argparse.ArgumentParser(description='Offline native swap transport qualification; loopback fixtures only')
    parser.add_argument('--mode', required=True, choices=['fingerprint', 'run'])
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--commit')
    parser.add_argument('--expected-arch', choices=['x86_64', 'aarch64'])
    parser.add_argument('--toolchain-manifest', type=Path)
    parser.add_argument('--toolchain-manifest-sha256')
    args = parser.parse_args()
    try:
        run(args)
    except (Refusal, OSError, ValueError, subprocess.TimeoutExpired) as error:
        print('REFUSED: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
