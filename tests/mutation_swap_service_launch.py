"""Named mutation checks on isolated service-source copies; no network or secrets."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PROFILE = 'deploy/v2/swap-runtime/service_profile.py'
INSTALL = 'deploy/v2/swap-runtime/install_disabled.py'
LAUNCH = 'deploy/v2/swap-runtime/service_launcher.py'
INIT_TEST = 'test_swap_service_profile.ServiceProfileTests.test_explicit_initialization_is_manifest_pinned_and_never_default'
INSTALL_TEST = 'test_swap_disabled_install.DisabledInstallTests.test_replacement_changes_release_only_and_preserves_dormant_policy'
LEGACY_TEST = 'test_swap_service_launcher.LauncherTests.test_legacy_profile_refused_before_secret'
GUARD = "    transaction_pin = contract['public_files_sha256'].get(PROFILE.PUBLIC+'/transaction.review')\n    if (not isinstance(transaction_pin, str) or re.fullmatch('[0-9a-f]{64}', transaction_pin) is None or\n        transaction_pin == '0'*64):\n        raise FD.Refused('reviewed transaction review required before credential access')\n"
CASES = [
    ('release_hash', INSTALL, "allowed={'source_commit','binary','binary_sha256'}", "allowed={'source_commit','binary'}", INSTALL_TEST, 'new_release_hash_is_allowed'),
    ('release_optional_policy', INSTALL, '(set(old)|set(new))-allowed', 'PROFILE.FIELDS-allowed', INSTALL_TEST, 'replacement_preserves_reviewed_policy'),
    ('release_active', INSTALL, "old['launch_authorized'] or new['launch_authorized'] or\n        ", '', INSTALL_TEST, 'active_replacement_refused'),
    ('initialization_type', PROFILE, "    if type(m.get('initialize_swap_recovery', False)) is not bool:\n        raise ValueError('initialization must be an explicit boolean')\n", '', INIT_TEST, 'initialization_requires_boolean'),
    ('initialization_missing', PROFILE, "    if m.get('initialize_swap_recovery', False): args.append('--initialize-swap-recovery')\n", '', INIT_TEST, 'explicit_initialization_reaches_both_roles'),
    ('initialization_default', PROFILE, "if m.get('initialize_swap_recovery', False): args.append", 'if True: args.append', INIT_TEST, 'normal_start_never_initializes'),
    ('initialization_manifest_pin', PROFILE, 'json.dumps(manifest, indent=2, sort_keys=True)', "json.dumps({k:v for k,v in manifest.items() if k != 'initialize_swap_recovery'}, indent=2, sort_keys=True)", INIT_TEST, 'initialization_changes_reviewed_manifest_pin'),
    ('legacy_before_secret', LAUNCH, GUARD, '', LEGACY_TEST, 'legacy_profile_refused_before_secret'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    results = []
    cases = [('baseline', None, None, None, None, None)] + CASES
    for name, filename, before, after, test, assertion in cases:
        target = args.output/name
        shutil.copytree(ROOT/'deploy/v2/swap-runtime', target/'deploy/v2/swap-runtime', ignore=shutil.ignore_patterns('__pycache__'))
        (target/'tests').mkdir()
        for source in ('test_swap_service_profile.py', 'test_swap_disabled_install.py', 'test_swap_service_launcher.py'):
            shutil.copy2(ROOT/'tests'/source, target/'tests'/source)
        if filename:
            path=target/filename
            content=path.read_text()
            if content.count(before) != 1: raise ValueError('mutation anchor differs: '+name)
            path.write_text(content.replace(before,after,1))
        tests=[test] if test else [INIT_TEST, INSTALL_TEST, LEGACY_TEST]
        env=dict(os.environ, PYTHONPATH=str(target/'tests'), PYTHONDONTWRITEBYTECODE='1')
        run=subprocess.run([sys.executable,'-B','-m','unittest','-v',*tests], cwd=target, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=30)
        (target/'result.log').write_text(run.stdout)
        passed=run.returncode == 0 if not filename else (run.returncode != 0 and assertion in run.stdout and '\nFAIL:' in run.stdout and '\nERROR:' not in run.stdout)
        results.append(dict(name=name,passed=passed,exit_code=run.returncode,named_assertion=assertion))
    (args.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(results,indent=2))
    return 0 if all(row['passed'] for row in results) else 1


if __name__ == '__main__': raise SystemExit(main())
