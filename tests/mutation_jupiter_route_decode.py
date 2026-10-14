import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir', type=Path, required=True)
    output = parser.parse_args().output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[1]
    path = root / 'src/v2_jupiter_route_decode.cpp'
    source = path.read_text()
    mutations = [
        ('discriminator', 'bytes[i] != discriminator[i]', 'false', 'boundaries', 'other_instruction_refused'),
        ('count_capacity', 'value > kJupiterRouteSteps', 'value > kJupiterRouteSteps - 1', 'shapes', 'eight_steps_fit'),
        ('empty_route', 'value == 0', 'false', 'shapes', 'empty_route_refused'),
        ('enum_range', 'value > 1', 'value > 2', 'shapes', 'invalid_side_refused'),
        ('step_percent', 'step.percent = static_cast<std::uint8_t>(value);', 'step.percent = 0;', 'captured', 'deriverse_fields_in_order'),
        ('instruction_id', 'step.instruction_id = static_cast<std::uint32_t>(value);', 'step.instruction_id = 0;', 'captured', 'deriverse_fields_in_order'),
        ('input_index', 'step.input_index = static_cast<std::uint8_t>(value);', 'step.input_index = 0;', 'captured', 'bison_fields_in_order'),
        ('output_index', 'step.output_index = static_cast<std::uint8_t>(value);', 'step.output_index = 0;', 'captured', 'deriverse_fields_in_order'),
        ('input_amount', 'cursor.integer(8,data.input_atoms)', 'cursor.integer(8,value)', 'captured', 'captured_input_and_quote'),
        ('quote_amount', 'cursor.integer(8,data.quoted_output_atoms)', 'cursor.integer(8,value)', 'captured', 'captured_input_and_quote'),
        ('slippage', 'data.slippage_bps = static_cast<std::uint16_t>(value);', 'data.slippage_bps = 0;', 'captured', 'captured_slippage_and_fee'),
        ('fee', 'data.platform_fee_bps = static_cast<std::uint8_t>(value);', 'data.platform_fee_bps = 0;', 'captured', 'captured_slippage_and_fee'),
        ('trailing', 'cursor.offset != bytes.size()', 'false', 'shapes', 'trailing_bytes_refused'),
        ('direct_tag', 'bytes[i]!=direct_tag[i]', 'false', 'direct', 'shared_not_direct_route'),
        ('direct_variant', 'if(number!=18 && number!=177 && number!=26 && number!=109 && number!=181 && number!=156)return {};', 'if(false)return {};', 'direct_boundaries', 'direct_unknown_variant_refused'),
        ('raydium_support', 'if(number!=18 && number!=177 && number!=26 && number!=109 && number!=181 && number!=156)return {};', 'if(number!=177)return {};', 'raydium', 'raydium_no_variant_payload'),
        ('raydium_payload', 'if(step.kind==JupiterSwapKind::invariant || step.kind==JupiterSwapKind::bison_fi_market_backed || step.kind==JupiterSwapKind::kipseli_v3){', 'if(true){', 'raydium', 'raydium_no_variant_payload'),
        ('raydium_kind', 'step.kind=static_cast<JupiterSwapKind>(number);', 'step.kind=JupiterSwapKind::bison_fi_market_backed;', 'raydium', 'raydium_no_variant_payload'),
        ('obsidian_support', ' && number!=109', ' && number!=110', 'current_variants', 'current_variants_decode'),
        ('kipseli_support', ' && number!=181', ' && number!=182', 'current_variants', 'current_variants_decode'),
        ('kipseli_payload', ' || step.kind==JupiterSwapKind::kipseli_v3', '', 'current_variants', 'current_variants_decode'),
        ('invariant_support', 'number!=18 && ', '', 'invariant', 'invariant_data_only_decode'),
        ('invariant_payload', 'step.kind==JupiterSwapKind::invariant || ', '', 'invariant', 'invariant_data_only_decode'),
        ('invariant_direction', 'step.side_or_direction=static_cast<std::uint8_t>(number);', 'step.side_or_direction=0;', 'invariant', 'invariant_bool_true_and_account_flow'),
        ('invariant_bool', 'number>1', 'number>2', 'invariant', 'invariant_bool_two_refused'),
        ('invariant_input', 'cursor.integer(8,route.input_atoms)', 'cursor.integer(8,number)', 'invariant', 'invariant_literal_suffix_offset'),
        ('invariant_quote', 'cursor.integer(8,route.quoted_output_atoms)', 'cursor.integer(8,number)', 'invariant', 'invariant_literal_suffix_offset'),
        ('invariant_fee', 'route.platform_fee_bps=static_cast<std::uint8_t>(number);', 'route.platform_fee_bps=0;', 'invariant', 'invariant_literal_suffix_offset'),
        ('invariant_slippage', 'route.slippage_bps=static_cast<std::uint16_t>(number);', 'route.slippage_bps=0;', 'invariant', 'invariant_literal_suffix_offset'),
        ('invariant_trailing', 'cursor.offset!=bytes.size()', 'false', 'invariant', 'invariant_trailing_byte_refused'),
        ('direct_bool', 'number>1', 'number>2', 'direct_boundaries', 'direct_invalid_bool_refused'),
        ('direct_count', 'number>kJupiterRouteSteps', 'number>kJupiterRouteSteps-1', 'direct_boundaries', 'direct_eight_steps_fit'),
        ('direct_input', 'cursor.integer(8,route.input_atoms)', 'cursor.integer(8,number)', 'direct', 'direct_route_exact_amounts'),
        ('direct_output', 'cursor.integer(8,route.quoted_output_atoms)', 'cursor.integer(8,number)', 'direct', 'direct_route_exact_amounts'),
        ('direct_fee', 'route.platform_fee_bps=static_cast<std::uint8_t>(number);', 'route.platform_fee_bps=0;', 'direct', 'direct_route_slippage_and_fee'),
        ('direct_slippage', 'route.slippage_bps=static_cast<std::uint16_t>(number);', 'route.slippage_bps=0;', 'direct', 'direct_route_slippage_and_fee'),
        ('direct_trailing', 'cursor.offset!=bytes.size()', 'false', 'direct_boundaries', 'direct_trailing_bytes_refused'),
    ]
    command = ['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
               '-fno-omit-frame-pointer', '-I' + str(root / 'include'), str(root / 'tests/v2_jupiter_route_decode_tests.cpp')]
    baseline = output / 'baseline'
    subprocess.run(command + [str(path), '-o', str(baseline)], check=True)
    subprocess.run([str(baseline)], check=True)
    results = []
    for name, old, new, case, assertion in mutations:
        if source.count(old) != 1:
            raise RuntimeError(f'{name}: expected one source match')
        mutated, binary = output / f'{name}.cpp', output / name
        mutated.write_text(source.replace(old, new, 1))
        subprocess.run(command + [str(mutated), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary), case], capture_output=True, text=True)
        killed = result.returncode == 1 and f'FAIL {case}: {assertion}\n' in result.stderr
        results.append({'name': name, 'case': case, 'assertion': assertion, 'exit': result.returncode,
                        'killed': killed, 'stdout': result.stdout, 'stderr': result.stderr})
        if not killed:
            raise RuntimeError(f'{name}: not a named assertion kill: {result}')
    files = ['src/v2_jupiter_route_decode.cpp', 'include/velocita/v2/jupiter_route_decode.hpp',
             'tests/v2_jupiter_route_decode_tests.cpp', 'tests/mutation_jupiter_route_decode.py']
    report = {'files': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in files},
              'results': results}
    (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(results)} named assertion kills')


if __name__ == '__main__':
    main()
