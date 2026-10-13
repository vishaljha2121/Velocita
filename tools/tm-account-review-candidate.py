"""Prepare an offline cash-only funding proposal; never initialize a ledger or authorize a trade."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import stat

spec = importlib.util.spec_from_file_location('history', Path(__file__).with_name('tm-account-history-check.py'))
H = importlib.util.module_from_spec(spec)
spec.loader.exec_module(H)


def read_private(path, limit=1048576):
    parent, normalized = H.TM.MINT._open_private_parent(path)
    try:
        fd = os.open(normalized.name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=parent)
    finally:
        os.close(parent)
    with os.fdopen(fd, 'rb') as stream:
        meta = os.fstat(stream.fileno())
        if not stat.S_ISREG(meta.st_mode) or meta.st_uid != os.getuid() or meta.st_mode & 0o077 or meta.st_nlink != 1:
            raise ValueError('unsafe evidence file')
        raw = stream.read(limit + 1)
    if len(raw) > limit:
        raise ValueError('evidence capacity')
    return raw


def exact(left, right):
    if type(left) is not type(right):
        return False
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(exact(left[k], right[k]) for k in left)
    if isinstance(left, list):
        return len(left) == len(right) and all(exact(a, b) for a, b in zip(left, right))
    return left == right


class OfflineReplay:
    def __init__(self, capture, manifest, raw_responses):
        self.capture, self.manifest, self.index = capture, manifest, 0
        self.raw_responses = raw_responses
        self.trace_raw = read_private(capture/'requests.private.json')
        self.trace = H.strict(self.trace_raw)
        if not isinstance(self.trace, list) or len(self.trace) != len(manifest):
            raise ValueError('request trace shape')

    def call(self, method, target, body=None):
        self.index += 1
        if self.index > len(self.manifest):
            raise ValueError('evidence exhausted')
        if not exact(self.trace[self.index-1], dict(method=method, target=target, body=body)):
            raise ValueError('request trace mismatch')
        return H.strict(self.raw_responses['response-%03d.json' % self.index])

    def get(self, path):
        return self.call('GET', path)

    def rpc(self, method, params):
        document = self.call('POST', H.RPC, dict(jsonrpc='2.0', id=self.index+1, method=method, params=params))
        if document.get('jsonrpc') != '2.0' or type(document.get('id')) is not int or document['id'] != self.index or 'error' in document or 'result' not in document:
            raise ValueError('RPC replay response')
        return document['result']


def owner_buy_alternative(observations, balances, evidence):
    """Bind one known external purchase; propose inventory, never authorize adoption."""
    if evidence.get('rpc_reports_success') is not True or evidence.get('production_live_admitted') is not False:
        raise ValueError('owner buy evidence must be successful and external')
    signature = H.key(evidence.get('transaction_signature'), 64)
    fields = ('slot', 'net_owner_receipt_lamports', 'total_usdc_debit_atoms')
    if any(type(evidence.get(k)) is not int or not 0 < evidence[k] <= 2**64-1 for k in fields):
        raise ValueError('owner buy evidence amount or slot')
    slot, base, quote = (evidence[k] for k in fields)
    matches = [row for row in observations if row['signature'] == signature]
    if len(matches) != 1:
        raise ValueError('owner buy missing or duplicate in fresh history')
    row = matches[0]
    if row['failed'] is not False or row['slot'] != slot or row['owner_native_delta'] != base or sum(row['current_usdc_account_deltas'].values()) != -quote:
        raise ValueError('owner buy fresh history mismatch')
    if base > balances['SOL'] or quote > 5000000:
        raise ValueError('owner buy exceeds balance or purchase cap')
    additional = [r['signature'] for r in observations if r['signature'] != signature and
                  sum(r['current_usdc_account_deltas'].values()) < 0 and r['owner_native_delta'] > 0]
    return dict(status='alternative_requires_owner_review', selected=False,
        signature=signature, slot=slot, classification='owner_cli_buy_outside_kernel',
        kernel_fill=False, ledger_mutation=False,
        proposed_funding=dict(managed_base_atoms=base, managed_base_cost_atoms=quote,
            managed_quote_atoms=balances['USDC'], outside_base_atoms=balances['SOL']-base,
            outside_quote_atoms=0, token_account_rent_included=False),
        observed_owner_purchase_spend_atoms=quote,
        maximum_total_purchase_spend_atoms=5000000,
        remaining_purchase_spend_atoms_before_other_activity_review=5000000-quote,
        additional_purchase_like_signatures_require_review=additional,
        budget_history_complete=False)


def select_all_current_inventory(candidate, balances):
    """Owner-selected inventory only; unknown opening cost prevents executable funding."""
    if 'owner_buy_alternative' not in candidate:
        raise ValueError('known purchase evidence required for current all-account proposal')
    known = candidate['owner_buy_alternative']['proposed_funding']
    candidate['status'] = 'inventory_selected_opening_valuation_required'
    candidate['inventory_selection'] = 'all_current_observed_sol_usdc'
    candidate['proposed_funding'] = dict(managed_base_atoms=balances['SOL'],
        managed_quote_atoms=balances['USDC'], managed_base_cost_atoms=None,
        outside_base_atoms=0, outside_quote_atoms=0, token_account_rent_included=False)
    candidate['opening_valuation'] = dict(status='requires_reviewed_activation_mark',
        method='mark_all_opening_SOL_at_reviewed_current_executable_sell_price',
        marked_base_cost_usdc_atoms=None, price_evidence_sha256=None, valued_at_ms=None,
        known_historical_subset_base_atoms=known['managed_base_atoms'],
        known_historical_subset_cost_usdc_atoms=known['managed_base_cost_atoms'],
        unknown_historical_cost_base_atoms=balances['SOL']-known['managed_base_atoms'],
        pnl_scope='change_since_reviewed_opening_mark_not_historical_return',
        proposed_opening_mark_convention='executable_net_liquidation_value',
        double_count_historical_purchase_cost=False)
    candidate['future_deposits'] = dict(automatically_managed=False,
        required_before_adoption='fresh_attributed_history_and_reviewed_funding_cut')
    candidate['unresolved_before_executable_review'].insert(0,
        'reviewed opening valuation for all managed SOL at activation; historical basis unknown')


def prepare(capture, output, owner_buy_evidence=None, owner_buy_sha256=None, all_current_inventory=False):
    manifest_raw = read_private(capture/'manifest.json')
    manifest = H.strict(manifest_raw)
    if not isinstance(manifest, dict) or not 1 <= len(manifest) <= 110:
        raise ValueError('manifest bound')
    if set(manifest) != {'response-%03d.json' % n for n in range(1, len(manifest)+1)} or not {'response-001.json', 'response-002.json'} <= manifest.keys():
        raise ValueError('manifest required response coverage')
    raw_responses = {}
    for name, digest in manifest.items():
        if not re.fullmatch(r'response-[0-9]{3}\.json', name) or not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest):
            raise ValueError('manifest identity')
        raw_responses[name] = read_private(capture/name)
        if hashlib.sha256(raw_responses[name]).hexdigest() != digest:
            raise ValueError('raw evidence hash mismatch')
    cut_raw = read_private(capture/'baseline-cut.json', 4096)
    cut = H.strict(cut_raw)
    result_raw = read_private(capture/'result.private.json')
    result = H.strict(result_raw)
    setup_raw = raw_responses['response-001.json']
    balances_raw = raw_responses['response-002.json']
    user, owner = H.identity(H.strict(setup_raw))
    balances = H.balances(H.strict(balances_raw))
    expected = dict(schema=1, scope='sol-usdc-current-accounts',
        owner_sha256=hashlib.sha256(H.key_bytes(owner)).hexdigest(), user_id_sha256=hashlib.sha256(user.encode()).hexdigest(),
        setup_sha256=hashlib.sha256(setup_raw).hexdigest(), balances_sha256=hashlib.sha256(balances_raw).hexdigest(),
        evidence_manifest_sha256=hashlib.sha256(manifest_raw).hexdigest(), base_atoms=balances['SOL'], quote_atoms=balances['USDC'])
    if not isinstance(cut, dict) or any(type(cut.get(k)) is not type(v) or cut[k] != v for k,v in expected.items()):
        raise ValueError('cut identity or balance mismatch')
    if cut.get('stable_window') is not True or cut.get('pagination_exhausted') is not True or type(cut.get('blocking_orders')) is not int or cut['blocking_orders'] != 0 or type(cut.get('unsupported_accounts')) is not int or cut['unsupported_accounts'] != 0:
        raise ValueError('ineligible history cut')
    if not isinstance(result, dict) or not exact(result.get('raw_sha256'), manifest) or type(result.get('unresolved_tm_orders')) is not int or result['unresolved_tm_orders'] != 0 or result.get('live_admitted') is not False:
        raise ValueError('result manifest or eligibility mismatch')
    for k in ('capture_started_ms','capture_finished_ms','start_slot','end_slot'):
        if type(cut.get(k)) is not int or cut[k] <= 0 or not exact(cut[k], result.get(k)):
            raise ValueError('cut time or slot mismatch')
    if cut['capture_finished_ms'] < cut['capture_started_ms'] or cut['capture_finished_ms']-cut['capture_started_ms'] > 300000 or cut['end_slot'] < cut['start_slot']:
        raise ValueError('cut bounds')
    if result.get('requires_reviewed_initial_cut') is not True or result.get('token_account_rent_in_managed_funding') is not False:
        raise ValueError('review or rent exclusion missing')
    replay = OfflineReplay(capture, manifest, raw_responses)
    observations = H.gather(replay, H.strict(setup_raw))
    if replay.index != len(manifest) or any(not exact(result.get(k), value) for k, value in observations.items()):
        raise ValueError('raw observations do not match result')
    if observations['unresolved_tm_orders'] != 0:
        raise ValueError('unresolved TM orders')
    candidate = dict(schema=1, status='proposal_requires_owner_review', launch_authorized=False, live_admitted=False, ledger_initialized=False,
        scope=cut['scope'], user_id=user, owner_wallet=owner,
        history_evidence_sha256=hashlib.sha256(cut_raw).hexdigest(), manifest_sha256=expected['evidence_manifest_sha256'],
        private_result_sha256=hashlib.sha256(result_raw).hexdigest(),
        captured_at_ms=cut['capture_finished_ms'],
        request_trace_sha256=hashlib.sha256(replay.trace_raw).hexdigest(),
        observations_require_owner_review=True,
        observations=observations['observations'],
        token_account_configs=observations['token_account_configs'],
        history_anchors=observations['histories'],
        proposed_funding={'managed_base_atoms':0, 'managed_quote_atoms':balances['USDC'], 'managed_base_cost_atoms':0,
                          'outside_base_atoms':balances['SOL'], 'outside_quote_atoms':0, 'token_account_rent_included':False},
        requested_limits={'maximum_total_purchases_including_fees_usdc_atoms':5000000, 'loss_stop_usdc_atoms':500000},
        unresolved_before_executable_review=['owner review of identity, observations and funding partition',
            'release incarnation, authority, custody/session and signer pins',
            'transaction-specific enforceable price/net-receipt and loss-stop bounds',
            'fresh cut and authenticated startup observations'],
        history_limitations=['current account scope; closed historical token accounts not enumerated',
            'trusted RPC observations, not independently verified archival history',
            'no simultaneous external wallet use during the supervised test; no future-activity guarantee'])
    if (owner_buy_evidence is None) != (owner_buy_sha256 is None):
        raise ValueError('owner buy evidence and digest required together')
    if owner_buy_evidence is not None:
        evidence_raw = read_private(owner_buy_evidence)
        if not isinstance(owner_buy_sha256, str) or not re.fullmatch('[0-9a-f]{64}', owner_buy_sha256) or hashlib.sha256(evidence_raw).hexdigest() != owner_buy_sha256:
            raise ValueError('owner buy evidence digest mismatch')
        evidence = H.strict(evidence_raw)
        if not isinstance(evidence, dict):
            raise ValueError('owner buy evidence shape')
        candidate['owner_buy_alternative'] = owner_buy_alternative(observations['observations'], balances, evidence)
        candidate['owner_buy_evidence_sha256'] = owner_buy_sha256
        candidate['requested_limits']['already_spent_owner_purchase_usdc_atoms'] = candidate['owner_buy_alternative']['observed_owner_purchase_spend_atoms']
        candidate['requested_limits']['remaining_purchase_usdc_atoms_before_other_activity_review'] = candidate['owner_buy_alternative']['remaining_purchase_spend_atoms_before_other_activity_review']
    if all_current_inventory:
        select_all_current_inventory(candidate, balances)
    parent, output = H.TM.MINT._open_private_parent(output)
    try:
        os.mkdir(output.name,mode=0o700,dir_fd=parent)
        os.fsync(parent)
    finally:
        os.close(parent)
    raw = (json.dumps(candidate,indent=2)+'\n').encode()
    H.B.private_write(output,'funding-proposal.private.json',raw)
    summary = {k:candidate[k] for k in ('status','launch_authorized','live_admitted','ledger_initialized','scope','captured_at_ms','proposed_funding','requested_limits','history_evidence_sha256')}
    if 'owner_buy_alternative' in candidate:
        alternative = candidate['owner_buy_alternative']
        summary['owner_buy_alternative'] = {k: alternative[k] for k in (
            'status', 'selected', 'classification', 'kernel_fill', 'ledger_mutation',
            'proposed_funding', 'observed_owner_purchase_spend_atoms',
            'maximum_total_purchase_spend_atoms',
            'remaining_purchase_spend_atoms_before_other_activity_review', 'budget_history_complete')}
        summary['additional_purchase_like_activity_count'] = len(alternative['additional_purchase_like_signatures_require_review'])
        summary['owner_buy_evidence_sha256'] = owner_buy_sha256
    if 'inventory_selection' in candidate:
        for name in ('inventory_selection', 'opening_valuation', 'future_deposits'):
            summary[name] = candidate[name]
    summary['private_proposal_sha256'] = hashlib.sha256(raw).hexdigest()
    H.B.private_write(output,'summary.json',(json.dumps(summary,indent=2)+'\n').encode())
    return summary


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--owner-buy-evidence', type=Path)
    parser.add_argument('--owner-buy-sha256')
    parser.add_argument('--all-current-inventory', action='store_true', help='Owner-selected current inventory; requires opening valuation, never funds a ledger')
    args=parser.parse_args()
    print(json.dumps(prepare(args.capture,args.output,args.owner_buy_evidence,args.owner_buy_sha256,args.all_current_inventory),indent=2))


if __name__=='__main__':
    try:
        main()
    except Exception as error:
        print('Offline proposal refused. Error type: '+type(error).__name__)
        raise SystemExit(1)
