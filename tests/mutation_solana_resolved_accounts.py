import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source_path = root / "src/v2_solana_resolved_accounts.cpp"
    source = source_path.read_text()
    mutations = [
        ("skip_decoded_equality", "if (decoded != checked)", "if (false)",
         "forged_decoded_values_are_refused", "forged decoded metadata cannot produce resolution"),
        ("reverse_loaded_order", "{true, false}", "{false, true}",
         "account_order_and_privileges", "all writable lookups precede every readonly lookup"),
        ("drop_signer_flags", "), signer, writable);", "), false, writable);",
         "account_order_and_privileges", "only required static accounts are signers"),
        ("make_loaded_readonly", "image.addresses[index], false, writable", "image.addresses[index], false, false",
         "account_order_and_privileges", "header and loaded privileges match independent oracle"),
        ("skip_evidence_cardinality", "if (evidence.size() != checked.lookup_count)", "if (evidence.size() < checked.lookup_count)",
         "lookup_evidence_failures", "extra lookup evidence refused"),
        ("skip_evidence_slot", "evidence[i].observed_slot == 0 || ", "",
         "lookup_evidence_failures", "missing evidence slot refused"),
        ("skip_evidence_hash", " || evidence[i].evidence_digest.is_zero()", "",
         "lookup_evidence_failures", "missing evidence hash refused"),
        ("skip_duplicate_evidence", "if (evidence[i].table == evidence[j].table)", "if (false)",
         "lookup_evidence_failures", "duplicate evidence table refused"),
        ("skip_duplicate_lookup", "if (result.lookups[j].table == table)", "if (false)",
         "aliases_and_failure_atomicity", "duplicate transaction lookup table refused"),
        ("skip_lookup_index", "if (index >= image.address_count)", "if (false)",
         "lookup_evidence_failures", "out of range table address index refused"),
        ("permit_aliases", "if (result.accounts[i].key == key)", "if (false)",
         "aliases_and_failure_atomicity", "loaded signer alias refused and old success cleared"),
        ("truncate_transaction_digest", "std::span{checked.transaction.data(), checked.transaction_size}",
         "std::span{checked.transaction.data(), static_cast<std::size_t>(checked.transaction_size - 1)}",
         "owned_digests_and_legacy", "transaction digest matches independent SHA256 oracle"),
        ("alter_address_digest_count", "hash.update(count);", "hash.update(std::span{count}.first(1));",
         "owned_digests_and_legacy", "address image matches independent SHA256 oracle"),
        ("lose_evidence_slot", "{table, image.observed_slot, image.evidence_digest,", "{table, 1, image.evidence_digest,",
         "account_order_and_privileges", "table identities and evidence provenance copied in transaction order"),
    ]
    common = ["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
              "-DVELOCITA_TARGET_CACHE_LINE_SIZE=128", "-fsanitize=address,undefined",
              "-fno-omit-frame-pointer", "-I" + str(root / "include"),
              str(root / "tests/v2_solana_resolved_accounts_tests.cpp"),
              str(root / "src/v2_solana_payload_view.cpp"),
              str(root / "src/v2_truemarkets_wire.cpp"), str(root / "src/v2_digest.cpp")]
    baseline = output / "baseline"
    subprocess.run(common + [str(source_path), "-o", str(baseline)], check=True)
    subprocess.run([str(baseline)], check=True)
    results = []
    for name, old, new, case, assertion in mutations:
        if source.count(old) != 1:
            raise RuntimeError(f"mutation {name} needs exactly one source match")
        mutated = output / f"{name}.cpp"
        binary = output / name
        mutated.write_text(source.replace(old, new, 1))
        compile_result = subprocess.run(common + [str(mutated), "-o", str(binary)], capture_output=True, text=True)
        if compile_result.returncode:
            raise RuntimeError(f"mutation {name} did not compile: {compile_result.stderr}")
        result = subprocess.run([str(binary), case], capture_output=True, text=True)
        killed = result.returncode == 1 and f"FAIL {case}: {assertion}\n" in result.stdout
        results.append({"mutation": name, "case": case, "named_assertion": assertion,
                        "exit": result.returncode, "killed": killed, "stdout": result.stdout,
                        "stderr": result.stderr})
        if not killed:
            raise RuntimeError(f"mutation {name} survived or failed for the wrong reason: {result.stdout} {result.stderr}")
    files = [source_path, root / "include/velocita/v2/solana_resolved_accounts.hpp",
             root / "tests/v2_solana_resolved_accounts_tests.cpp", root / "src/v2_solana_payload_view.cpp",
             root / "include/velocita/v2/solana_payload_view.hpp", root / "src/v2_truemarkets_wire.cpp",
             root / "src/v2_digest.cpp"]
    report = {"source_sha256": {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                                for path in files}, "results": results}
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Killed {len(results)} mutations by their designated assertions")


if __name__ == "__main__":
    main()
