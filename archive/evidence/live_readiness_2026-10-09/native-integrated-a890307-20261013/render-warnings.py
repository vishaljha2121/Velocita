from pathlib import Path
import json,shutil,collections
w=Path.home()/'.cache/velocita-build/checkpoint-20261013';dest=Path('/Users/vishaljha/aeron-raft-pi/docs/evidence/live_readiness_2026-10-09/native-integrated-a890307-20261013');merged={};status=[]
for name,ip in [('nuc1','100.72.127.102'),('pi','100.84.151.116')]:
 p=w/f'warnings-{ip}.json';data=json.loads(p.read_text());shutil.copy2(p,dest/f'warnings-{name}.json');status.append(f"| {name} | {data['unique_warnings']} | {data['total_occurrences']} | {'complete build log' if data['build_finished'] else 'in-progress snapshot'} |")
 for v in data['warnings']:
  k=(v['path'],v['line'],v['column'],v['message'])
  row=merged.setdefault(k,{**v,'hosts':[],'counts':{}});row['hosts'].append(name);row['counts'][name]=v['occurrences']
rows=list(merged.values());(dest/'warnings.json').write_text(json.dumps({'source_commit':'86f440806c6adc6f4c1dc9dc7a271e3d95293d76','status':'next iteration cleanup requested; not a new live gate','warnings':rows},indent=2)+'\n')
text='''# Compiler warning inventory — checkpoint 86f4408

Owner requested October 13: fix all warnings in the next iteration so future
build logs are easy to follow. This inventory does not alter the qualified source
or suppress diagnostics. Completion of compilation is not completion of tests.

| Host | Unique diagnostics | Occurrences | Capture status |
|---|---:|---:|---|
'''+ '\n'.join(status)+'''

Exact messages and per-host occurrence counts are in [warnings.json](warnings.json),
including diagnostics from retained earlier attempts (identical log copies counted once), with log hashes in [NUC1](warnings-nuc1.json) and [Pi](warnings-pi.json).
Rows below are deduplicated by source location and full diagnostic text; long
messages are shortened here only. Project warnings should be corrected in source.
Diagnostics located in system headers may originate in project callers; inspect
the retained inlining context before assigning ownership. Dependency/compiler
diagnostics need individual review and, where appropriate,
a pinned upstream fix. Do not hide them using blanket warning suppression.

The command-client enum warning is currently unreachable: its decoder rejects the
swap-authority response before the command client's switch. The new swap code also
has misleading-indentation warnings from multiple statements on one line. Neither
observation excuses skipping review; preserve behavior with tests when cleaning up.

| File:line | Warning | Hosts | Diagnostic |
|---|---|---|---|
'''
for v in sorted(rows,key=lambda v:(v['scope'],v['path'],v['line'],v['message'])):
 msg=v['message'].replace('|','\\|').replace('\n',' ');msg=msg[:240]+('…' if len(msg)>240 else '')
 text+=f"| `{v['path'].removeprefix('/source/')}:{v['line']}` | `{v['category']}` | {', '.join(v['hosts'])} | {msg} |\n"
(dest/'WARNINGS.md').write_text(text)
print('warning inventory:',len(rows),'deduplicated diagnostics')
