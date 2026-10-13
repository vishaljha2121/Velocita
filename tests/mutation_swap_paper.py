import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
MUTANTS = [
    ('buy_base_deduction', 'src/v2_swap_paper.cpp',
     'net = funded ? received - q.buy_base_deduction.value() : 0;',
     'net = funded ? received : 0;',
     'paper_observed_cost_oracle_and_full_close',
     '998000 principal buys 9980000 atoms minus 550840, exact fee makes total 1000000'),
    ('buy_exact_fee', 'src/v2_swap_paper.cpp', 'fee = q.use_exact_buy_fee ? Wide{q.exact_buy_fee.value()} :',
     'fee = false ? Wide{q.exact_buy_fee.value()} :',
     'paper_observed_cost_oracle_and_full_close',
     '998000 principal buys 9980000 atoms minus 550840, exact fee makes total 1000000'),
    ('buy_net_minimum', 'src/v2_swap_paper.cpp', 'net >= i.min_base_receive.value()',
     'received >= i.min_base_receive.value()', 'paper_observed_cost_boundaries',
     'net minimum, deduction underflow and exact fee cap fail without charge'),
    ('buy_deduction_underflow', 'src/v2_swap_paper.cpp',
     'const bool funded = received >= q.buy_base_deduction.value();', 'const bool funded = true;',
     'paper_observed_cost_boundaries', 'net minimum, deduction underflow and exact fee cap fail without charge'),
    ('buy_fee_mode_ambiguity', 'src/v2_swap_paper.cpp',
     'if (s.pending.intent.side == Side::buy && q.use_exact_buy_fee && q.fee_ppm)', 'if (false)',
     'paper_exact_fee_mode_and_unchanged_sell', 'ambiguous exact and rate buy fees are refused'),
    ('sell_inherits_buy_fee', 'src/v2_swap_paper.cpp',
     "fee = ceil_ratio(gross * q.fee_ppm, 1'000'000);",
     "fee = q.use_exact_buy_fee ? Wide{q.exact_buy_fee.value()} : ceil_ratio(gross * q.fee_ppm, 1'000'000);",
     'paper_observed_cost_oracle_and_full_close', 'buy deduction and exact buy fee do not apply to a sale'),
    ('role', 'src/v2_swap.cpp', 'if (role != role_for<T>())', 'if (false)',
     'funding_and_roles', 'strategy cannot fund itself'),
    ('lot_rounding', 'src/v2_swap.cpp', 's.base = BaseAtoms{static_cast<std::uint64_t>(base)};',
     "s.base = BaseAtoms{static_cast<std::uint64_t>(base) / 10'000'000 * 10'000'000};",
     'fractional_receipt_and_fee_accounting', 'fractional lot preserved exactly'),
    ('cash_fee_hold', 'src/v2_swap.cpp', 'debit > s.quote.value()', 'i.quote_input.value() > s.quote.value()',
     'insufficient_cash_and_inventory', 'one atom short including fee refuses atomically'),
    ('purchase_cap', 'src/v2_swap.cpp', 'if (Wide{s.purchases.value()} + debit > l.max_purchase_total.value())', 'if (false)',
     'sales_do_not_replenish_purchase_allowance', 'cash from sale cannot buy past cumulative cap'),
    ('minimum_receipt', 'src/v2_swap.cpp', 'x.base_credit.value() < i.min_base_receive.value()', 'false',
     'one_atom_bounds_conflict_without_booking', 'out-of-bound finalized evidence conflicts'),
    ('auxiliary_sol_debit', 'src/v2_swap.cpp', 'if (x.base_debit.value() || x.quote_credit.value()', 'if (false || x.quote_credit.value()',
     'one_atom_bounds_conflict_without_booking', 'out-of-bound finalized evidence conflicts'),
    ('sell_fee_cap', 'src/v2_swap.cpp', 'if (x.fee.value() > i.fee_cap.value())', 'if (false)',
     'sell_fee_and_transaction_reuse_conflict', 'sell fee one atom above authorization keeps inventory held'),
    ('unknown_release', 'src/v2_swap.cpp', 'if (s.pending.phase != Phase::reserved) return Code::bad_transition;\n    if (c.evidence.is_zero())',
     'if (s.pending.phase != Phase::reserved && s.pending.phase != Phase::unknown) return Code::bad_transition;\n    if (c.evidence.is_zero())',
     'pending_unknown_keeps_holds', 'timeout never releases possibly sent money'),
    ('loss_boundary', 'src/v2_swap.cpp', 'return s.realized_pnl <= -static_cast', 'return s.realized_pnl < -static_cast',
     'loss_stop_allows_only_sales', 'loss at boundary stops buys'),
    ('catalog_binding', 'src/v2_swap.cpp', 'if (i.binding != s.opening.binding)', 'if (false)',
     'identity_and_canonical_units', 'wrong identity or mixed input units refused without mutation'),
    ('transient_hold', 'src/v2_swap.cpp', 's.reserved_base = i.transient_base_hold;', 's.reserved_base = {};',
     'transient_liquidity_is_reserved', 'temporary SOL liquidity is held separately'),
    ('price_safety', 'src/v2_swap.cpp', 'i.min_base_receive.value() < ceil_ratio(debit * atom_scale, l.max_buy_price_per_sol.value())', 'false',
     'safety_floor_and_fee_caps', 'minimum receive must cover all-in 125 USDC per SOL ceiling'),
    ('transaction_reuse', 'src/v2_swap.cpp', 'if (x.transaction == s.settlements[n].transaction)', 'if (false)',
     'sell_fee_and_transaction_reuse_conflict', 'same transaction cannot fund two effects'),
    ('finality', 'src/v2_swap.cpp', 'if (!x.finalized || x.transaction.is_zero()', 'if (false || x.transaction.is_zero()',
     'role_checks_and_finality', 'unfinalized fill retains holds'),
    ('record_checksum', 'src/v2_swap_codec.cpp', 'if(!std::equal(hash.bytes.begin(),hash.bytes.end(),bytes.end()-32))', 'if(false)',
     'record_codec_has_independent_golden_and_rejects_corruption', 'single byte corruption rejects without destination mutation'),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    scratch = Path.home() / '.cache/velocita-build/swap-paper-mutations'
    scratch.mkdir(parents=True, exist_ok=True)
    results = []
    for name, relative, before, after, case, assertion in MUTANTS:
        source = (ROOT / relative).read_text()
        if source.count(before) != 1:
            raise RuntimeError(f'{name}: mutation target must occur once')
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            work = Path(directory)
            mutant = work / Path(relative).name
            mutant.write_text(source.replace(before, after))
            inputs = [ROOT / 'tests/v2_swap_paper_tests.cpp', ROOT / 'src/v2_swap.cpp',
                      ROOT / 'src/v2_swap_codec.cpp', ROOT / 'src/v2_swap_paper.cpp', ROOT / 'src/v2_digest.cpp']
            inputs = [mutant if p == ROOT / relative else p for p in inputs]
            built = subprocess.run(['c++', '-std=c++20', '-O1', '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128',
                                    '-I' + str(ROOT / 'include'), *map(str, inputs), '-o', str(work / 'test')],
                                   capture_output=True, text=True, timeout=60, check=False)
            if built.returncode:
                raise RuntimeError(f'{name}: compile failure is not a mutation kill: {built.stderr}')
            result = subprocess.run([str(work / 'test'), case], capture_output=True, text=True, timeout=10, check=False)
            killed = result.returncode == 1 and f'FAIL {case}: {assertion}' in result.stdout
            results.append(dict(name=name, case=case, named_assertion=assertion, killed=killed,
                                exit_code=result.returncode, output=result.stdout.strip(),
                                original_sha256=hashlib.sha256(source.encode()).hexdigest()))
            print(name, 'KILLED' if killed else 'SURVIVED', flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(results, indent=2) + '\n')
    return 0 if all(r['killed'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
