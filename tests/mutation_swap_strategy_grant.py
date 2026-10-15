import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
MUTANTS=[
 ('pregrant_bypass','src/v2_kernel.cpp','return kOperatorRole | (permitted ? kStrategyRole : 0);','return kOperatorRole | kStrategyRole;','grant_old_requests_still_refused'),
 ('sequence_floor','src/v2_kernel.cpp','command.identity.request_sequence>=grant.first_sequence &&','true &&','grant_cannot_reissue_old_sequences'),
 ('caller_binding','src/v2_kernel.cpp','grant.caller==command.identity.caller_id &&','true &&','grant_other_strategy_refused'),
 ('kill_fence','src/v2_kernel.cpp','grant.kill_epoch==state.kill_epoch.value() &&','true &&','grant_epoch_change_revokes_authority'),
 ('executor_fence','src/v2_kernel.cpp','grant.executor_generation==state.executor_generation.value();','true;','grant_executor_change_revokes_authority'),
 ('operator_only','src/v2_kernel.cpp','std::holds_alternative<swap::AmendBuyPrice>(value.record.command)) return kOperatorRole;','std::holds_alternative<swap::AmendBuyPrice>(value.record.command)) return kOperatorRole | kStrategyRole;','grant_strategy_cannot_self_grant'),
 ('one_time','src/v2_swap.cpp','if (s.strategy_grant != StrategyGrant{}) return Code::stale;','if (false) return Code::stale;','grant_is_one_time'),
 ('halted_grant','src/v2_swap.cpp','if (s.strategy_grant != StrategyGrant{}) return Code::stale;\n    if (s.mode != Mode::halted) return Code::stopped;','if (s.strategy_grant != StrategyGrant{}) return Code::stale;','grant_requires_halted'),
 ('opening_binding','src/v2_swap.cpp','if (!now || c.opening_digest != opening_digest(s.opening) || c.review_evidence.is_zero() ||\n        !c.caller.value()', 'if (!now || c.review_evidence.is_zero() ||\n        !c.caller.value()', 'grant_invalid_binding_target_or_epoch_refused'),
 ('target_role','src/v2_swap_kernel.cpp','!strategy_caller(s,grant->caller) ||','false ||','grant_invalid_binding_target_or_epoch_refused'),
 ('snapshot_role','src/v2_swap_kernel.cpp','if (s.spot_swap.strategy_grant != swap::StrategyGrant{} && !strategy_caller(s,s.spot_swap.strategy_grant.caller)) return false;','if (false) return false;','grant_snapshot_role_corruption_refused'),
 ('snapshot_evidence','src/v2_swap.cpp','!g.first_sequence.value() || g.evidence.is_zero() ||','!g.first_sequence.value() ||','grant_snapshot_empty_evidence_refused'),
 ('pristine_account','src/v2_swap.cpp','if (s.pending != Pending{} || s.last_effect || s.settled_total || s.last_release != ReleaseUnsent{} ||\n        s.reserved_base.value() || s.reserved_quote.value() || s.purchases.value() || s.fees.value() || s.realized_pnl ||\n        s.base != s.opening.base || s.quote != s.opening.quote || s.cost != s.opening.base_cost) return Code::busy;', 'if (false) return Code::busy;', 'grant_requires_pristine_account'),
 ('expired_floor','src/v2_swap_kernel.cpp','grant->first_sequence<=s.caller_cursors[n].expired_below','false','grant_floor_must_exceed_expired_cursor'),
 ('grant_codec_sequence','src/v2_swap_codec.cpp','w.atom(g.first_sequence);','w.number(4);','snapshot preserves exact balances and holds'),
]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    build=args.build.resolve()
    link=shlex.split((build/'CMakeFiles/v2-swap-integration-tests.dir/link.txt').read_text())
    flags=(build/'CMakeFiles/v2-swap-integration-tests.dir/flags.make').read_text()
    includes=shlex.split(next(line.split('=',1)[1] for line in flags.splitlines() if line.startswith('CXX_INCLUDES =')))
    defines=shlex.split(next(line.split('=',1)[1] for line in flags.splitlines() if line.startswith('CXX_DEFINES =')))
    scratch=Path.home()/'.cache/velocita-build/grant-mutations'
    scratch.mkdir(parents=True,exist_ok=True)
    baseline=subprocess.run([str(build/'v2-swap-integration-tests')],capture_output=True,text=True,timeout=30)
    if baseline.returncode:raise RuntimeError('baseline must pass before mutation testing: '+baseline.stdout+baseline.stderr)
    results=[]
    for name,relative,old,new,assertion in MUTANTS:
        original=(ROOT/relative).read_text()
        assert original.count(old)==1,name
        with tempfile.TemporaryDirectory(prefix=name+'-',dir=scratch) as directory:
            work=Path(directory);source=work/'mutant.cpp';obj=work/'mutant.o';binary=work/'check'
            source.write_text(original.replace(old,new))
            compile_command=[link[0],'-std=c++20',*defines,*includes,'-c',str(source),'-o',str(obj)]
            compiled=subprocess.run(compile_command,capture_output=True,text=True)
            if compiled.returncode:raise RuntimeError(name+' compile failure is not a kill: '+compiled.stderr)
            command=link[:];command[command.index('-o')+1]=str(binary);command.insert(1,str(obj))
            linked=subprocess.run(command,cwd=build,capture_output=True,text=True)
            if linked.returncode:raise RuntimeError(name+' link failure is not a kill: '+linked.stderr)
            tested=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            output=tested.stdout+tested.stderr;killed=tested.returncode==1 and ': '+assertion in output
            results.append(dict(name=name,killed=killed,assertion=assertion,output=output,compile_command=compile_command,
                original_sha256=hashlib.sha256(original.encode()).hexdigest(),mutant_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(),binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()))
            print(name,'KILLED' if killed else 'SURVIVED',flush=True)
        args.output.write_text(json.dumps(results,indent=2)+'\n')
    return 0 if all(row['killed'] for row in results) else 1


if __name__=='__main__':
    raise SystemExit(main())
