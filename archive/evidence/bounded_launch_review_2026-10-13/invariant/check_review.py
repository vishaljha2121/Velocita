"""Offline corruption checks against the retained capture; private inputs required."""
import base64
import copy
import json
import review_capture as R


def main():
    original = R.load()
    accepted = R.review(*original)
    assert accepted['conditional_accounting']['net_floor_lamports'] == 6910173
    assert not accepted['live_admitted'] and not accepted['production_verifier_accepts']
    def data_byte(a, index, offset, value):
        raw = bytearray(base64.b64decode(a[index]['data'][0])); raw[offset] = value
        a[index]['data'][0] = base64.b64encode(raw).decode()
    cases = [
        ('header_and_permissions', lambda d,k,a: d['header'].__setitem__(2,5)),
        ('token_state', lambda d,k,a: data_byte(a,2,108,2)),
        ('source_close_authority_is_sponsor', lambda d,k,a: data_byte(a,2,129,0)),
        ('source_and_fee_usdc', lambda d,k,a: data_byte(a,2,0,0)),
        ('destination_absent_and_ata_seeds', lambda d,k,a: a.__setitem__(3,copy.deepcopy(a[13]))),
        ('pool_mints_vaults_tickmap', lambda d,k,a: data_byte(a,11,8,0)),
        ('state_and_vault_authority', lambda d,k,a: data_byte(a,16,41,0)),
        ('compute_limit', lambda d,k,a: d['instructions'][0].__setitem__('data','02c15c1500')),
        ('sponsored_ata', lambda d,k,a: d['instructions'][1]['accounts'].__setitem__(0,1)),
        ('jupiter_and_invariant_accounts', lambda d,k,a: d['instructions'][2]['accounts'].__setitem__(13,4)),
        ('single_invariant_x_to_y', lambda d,k,a: d['instructions'][2].__setitem__('data',
            d['instructions'][2]['data'][:24]+'00'+d['instructions'][2]['data'][26:])),
        ('captured_amount_fee_slippage', lambda d,k,a: d['instructions'][2].__setitem__('data',
            d['instructions'][2]['data'][:-2]+'15')),
        ('close_to_owner', lambda d,k,a: d['instructions'][3]['accounts'].__setitem__(1,0)),
        ('fixed_repayment_to_sponsor', lambda d,k,a: d['instructions'][4].__setitem__('data',
            '02000000f11d1f0000000000')),
    ]
    results = []
    for expected, mutate in cases:
        d,k,a = copy.deepcopy(original); mutate(d,k,a)
        try:
            R.review(d,k,a)
        except ValueError as error:
            assert str(error) == expected, (expected, str(error))
            results.append(dict(changed_input_rejected_by=expected))
        else:
            raise AssertionError('corrupted input accepted: '+expected)
    print(json.dumps(dict(baseline_checks=len(accepted['checks']), negative_cases=len(results),
                         results=results, production_tests=False),indent=2))


if __name__ == '__main__':
    main()
