"""Offline evidence check only; no credentials, network, signer or admission output.

Offsets/order come from the pinned Invariant source in SOURCES.json. This does
not establish equivalence of deployed program bytecode to that source.
"""
import base64
import hashlib
import importlib.util
import json
from pathlib import Path

SPEC = importlib.util.spec_from_file_location('account_data', Path(__file__).parents[1] / 'account_data.py')
A = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(A)
PROGRAM = 'HyaB3W9q6XdA5xwpU4XnSZV94htfmbmqJXZcEbRaJutt'
RESPONSE_SHA = 'd13657c1eb5c651545aea1cca0e01c85d4d89c7498a5e8432319e77c105d0f17'
PAYLOAD_SHA = '15bfdf29bcd880167ff8715a58ad7ccda512a312a8120960f828e6e855951760'


def decode(raw):
    """Independent bounded v0 wire inspection, not the production decoder."""
    pos = 0
    def take(n):
        nonlocal pos
        A.require(0 <= n <= len(raw) - pos, 'wire bounds')
        result = raw[pos:pos+n]; pos += n
        return result
    def count():
        value = 0
        for shift in (0, 7, 14):
            byte = take(1)[0]; value |= (byte & 127) << shift
            if byte < 128:
                A.require(value <= 65535 and (shift == 0 or byte != 0), 'canonical count')
                return value
        raise ValueError('count overflow')
    A.require(count() == 2 and take(128) == bytes(128), 'unsigned signatures')
    A.require(take(1) == b'\x80', 'v0 message')
    header = list(take(3)); keys = [take(32).hex() for _ in range(count())]
    take(32)  # retained blockhash; historical capture is not fresh admission
    instructions = []
    for _ in range(count()):
        program = take(1)[0]; accounts = list(take(count())); data = take(count())
        instructions.append(dict(program=program, accounts=accounts, data=data.hex()))
    lookups = []
    for _ in range(count()):
        table = take(32).hex(); writable = list(take(count())); readonly = list(take(count()))
        lookups.append(dict(table=table, writable=writable, readonly=readonly))
    A.require(pos == len(raw), 'no wire suffix')
    return dict(header=header, static_keys=keys, instructions=instructions, lookups=lookups)


def review(d, keys, accounts):
    checks = {}
    def check(name, condition):
        A.require(condition, name); checks[name] = True
    def seed_match(seeds, program, key, bump=None):
        # Equality to a captured address; not a general PDA derivation utility.
        candidates = range(256) if bump is None else [bump]
        return any(hashlib.sha256(b''.join(seeds) + bytes([b]) + program +
                   b'ProgramDerivedAddress').digest() == key for b in candidates)
    check('header_and_permissions', d['header'] == [2, 0, 6] and len(d['static_keys']) == 11
          and len(keys) == len(set(keys)) == len(accounts) == 19
          and d['lookups'][0]['writable'] == [205, 203, 199, 206]
          and d['lookups'][0]['readonly'] == [55, 129, 128, 204])
    ids = {5:A.SYSTEM, 6:'ComputeBudget111111111111111111111111111111',
           7:'JUP6LkbZbjS1jKKwapdHNy74zcZ3tLUZoi5QNyVTaV4', 8:A.TOKEN,
           9:'ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL', 15:A.SOL, 17:PROGRAM}
    check('canonical_programs_and_mint', all(keys[i] == A.decode58(s) for i,s in ids.items())
          and all(accounts[i]['executable'] is True for i in (5,6,7,8,9,17)))
    check('system_owner_and_sponsor', all(accounts[i]['owner'] == A.SYSTEM and
          accounts[i]['executable'] is False and accounts[i]['data'] == ['', 'base64'] for i in (0,1)))
    ts = {i:A.token(accounts[i]) for i in (2,4,12,13)}
    check('token_state', all(t['state'] == 1 and t['delegate_hex'] is None and
          t['delegated_atoms'] == 0 for t in ts.values()))
    check('source_close_authority_is_sponsor', ts[2]['close_authority_hex'] == keys[0].hex()
          and all(ts[i]['close_authority_hex'] is None for i in (4,12,13)))
    check('source_and_fee_usdc', all(ts[i]['mint_hex'] == A.decode58(A.USDC).hex() and
          ts[i]['native_reserve'] is None for i in (2,4,12)) and
          ts[2]['authority_hex'] == keys[1].hex() and ts[4]['authority_hex'] != keys[1].hex())
    check('destination_absent_and_ata_seeds', accounts[3] is None and
          seed_match([keys[1], keys[8], keys[15]], keys[9], keys[3]))
    check('event_authority_seeds', seed_match([b'__event_authority'], keys[7], keys[10]))
    pool = A.account_bytes(accounts[11], PROGRAM, 400)
    state = A.account_bytes(accounts[16], PROGRAM, 74)
    tickmap = A.account_bytes(accounts[14], PROGRAM, 11099)
    check('anchor_discriminators', all(b[:8] == hashlib.sha256(('account:'+n).encode()).digest()[:8]
          for b,n in ((pool,'Pool'), (state,'State'), (tickmap,'Tickmap'))))
    check('pool_mints_vaults_tickmap', pool[8:40] == A.decode58(A.USDC) and pool[40:72] == keys[15]
          and pool[72:104] == keys[12] and pool[104:136] == keys[13] and pool[222:254] == keys[14])
    check('state_and_vault_authority', state[41:73] == keys[18] and
          all(ts[i]['authority_hex'] == keys[18].hex() for i in (12,13)) and
          ts[13]['mint_hex'] == keys[15].hex() and ts[13]['native_reserve'] == 2039280)
    check('pool_state_authority_seeds', seed_match([b'poolv1', pool[8:40], pool[40:72],
          pool[154:170], pool[152:154]], keys[17], keys[11], pool[-1]) and
          seed_match([b'statev1'], keys[17], keys[16], state[-1]) and
          seed_match([b'Invariant'], keys[17], keys[18], state[40]))
    ix = d['instructions']
    check('outer_program_sequence', [v['program'] for v in ix] == [6,9,7,8,5])
    check('compute_limit', ix[0]['accounts'] == [] and ix[0]['data'] == '02c05c1500')
    check('sponsored_ata', ix[1]['accounts'] == [0,3,1,15,5,8] and ix[1]['data'] == '01')
    check('jupiter_and_invariant_accounts', ix[2]['accounts'] ==
          [8,1,2,3,7,15,4,10,7,17,16,11,14,2,3,12,13,1,18,8,7])
    route = bytes.fromhex(ix[2]['data'])
    check('single_invariant_x_to_y', len(route) == 36 and
          route[:17] == bytes.fromhex('e517cb977ae3ad2a010000001201640001'))
    amount = int.from_bytes(route[17:25], 'little'); output = int.from_bytes(route[25:33], 'little')
    slip = int.from_bytes(route[33:35], 'little'); fee = route[35]
    check('captured_amount_fee_slippage', (amount,output,slip,fee) == (1000000,9420476,500,20))
    check('close_to_owner', ix[3]['accounts'] == [3,1,1] and ix[3]['data'] == '09')
    check('fixed_repayment_to_sponsor', ix[4]['accounts'] == [1,0] and
          ix[4]['data'] == '02000000f01d1f0000000000')
    # Existing reviewed Jupiter contract; deployed-program rounding is not independently audited here.
    gross = (output*(10000-slip)+9999)//10000
    return dict(checks=checks, layout_verified_against_public_source=True,
                deployed_source_equivalence_verified=False, production_verifier_accepts=False,
                live_admitted=False, signed=False, broadcast=False,
                conditional_accounting=dict(usdc_debit_atoms=amount, explicit_fee_atoms=amount*fee//10000,
                    swap_principal_atoms=amount-amount*fee//10000, gross_floor_lamports=gross,
                    repayment_lamports=2039280, credited_sponsor_lamports=0,
                    net_floor_lamports=gross-2039280))


def load():
    private = Path.home()/'.velocita-credentials/truemarkets'
    capture = private/'bison-launch-quote-1a46fd8-20261014'
    evidence = private/'invariant-account-review-1a46fd8-20261014'
    raw = (capture/'response.private.json').read_bytes()
    A.require(A.sha(raw) == RESPONSE_SHA, 'response hash')
    payload = json.loads(raw)['payloads'][0]['payload']
    A.require(A.sha(payload.encode()) == PAYLOAD_SHA, 'payload hash')
    d = decode(bytes.fromhex(json.loads(payload)['parameters']['unsignedTransaction']))
    A.require(d == json.loads((capture/'decoded.json').read_bytes()), 'independent wire parity')
    resolved = json.loads((evidence/'resolved.json').read_bytes())
    keys = [bytes.fromhex(k) for k in resolved['keys_hex']]
    lookup_result = json.loads((evidence/'lookup-response.json').read_bytes())['result']
    account_result = json.loads((evidence/'accounts-response.json').read_bytes())['result']
    A.require(len(d['lookups']) == 1 and len(lookup_result['value']) == 1, 'lookup cardinality')
    table = A.account_bytes(lookup_result['value'][0], A.ALT)
    A.require(table[:4] == b'\x01\0\0\0' and table[4:12] == b'\xff'*8 and
              len(table) >= 56 and (len(table)-56)%32 == 0 and
              int.from_bytes(table[12:20], 'little') < lookup_result['context']['slot'], 'active warm table')
    addresses = [table[i:i+32] for i in range(56,len(table),32)]
    indices = d['lookups'][0]['writable'] + d['lookups'][0]['readonly']
    A.require(keys == [bytes.fromhex(k) for k in d['static_keys']] + [addresses[i] for i in indices],
              'resolved wire keys')
    A.require(account_result['context']['slot'] >= lookup_result['context']['slot'], 'context ordering')
    return d, keys, account_result['value']


if __name__ == '__main__':
    print(json.dumps(review(*load()), indent=2))
