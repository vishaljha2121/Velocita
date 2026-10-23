"""The new-incarnation profile changes exactly the incarnation-bound fields; hashes recomputed independently."""
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('prepare_documents', Path(__file__).with_name('prepare_documents.py'))
D = importlib.util.module_from_spec(spec)
spec.loader.exec_module(D)
NEW = '0123456789abcdef0123456789abcdef'
OLD_HASH = hashlib.sha256(bytes.fromhex(D.OLD)).hexdigest()
PREVIOUS = dict(schema='1', incarnation=OLD_HASH, catalog='c' * 64, custody='d' * 64, authority_domain='e' * 64, account='2', instrument='2',
                account_assets='f' * 64, session_evidence='1' * 64, owner_wallet='2' * 64, signer_public_key='03' + '4' * 64, bearer_digest='5' * 64,
                expires_at_ms='1000', signer_instance='6' * 64, account_review_digest='7' * 64, transaction_review_digest='8' * 64, user_id='u')


class Profile(unittest.TestCase):
    def test_only_incarnation_bound_fields_change(self):
        p, session, signer = D.profile_for(PREVIOUS, NEW, '9' * 64, 'key-id', b'setup', b'history', 5000)
        changed = {k for k in p if p[k] != PREVIOUS[k]}
        self.assertEqual(changed, {'incarnation', 'authority_domain', 'session_evidence', 'bearer_digest', 'expires_at_ms', 'signer_instance', 'account_review_digest'})
        self.assertEqual(p['incarnation'], hashlib.sha256(bytes.fromhex(NEW)).hexdigest())
        self.assertEqual(p['bearer_digest'], hashlib.sha256(b'key-id').hexdigest(), 'gateway credential pin is the key id')
        self.assertEqual(session, dict(schema=1, setup_sha256=hashlib.sha256(b'setup').hexdigest(), history_sha256=hashlib.sha256(b'history').hexdigest(),
                                       bearer_sha256=hashlib.sha256(b'key-id').hexdigest()))
        self.assertEqual(p['session_evidence'], hashlib.sha256((json.dumps(session, sort_keys=True, separators=(',', ':')) + '\n').encode()).hexdigest())
        self.assertEqual(signer, dict(schema=1, incarnation=NEW, uid=29002, caller=23, custody='d' * 64, key_version=1))
        self.assertEqual((p['expires_at_ms'], p['authority_domain']), ('5000', '9' * 64))
        self.assertEqual(PREVIOUS['incarnation'], OLD_HASH, 'input not mutated')

    def test_only_the_b79e65be_profile_is_carried_forward(self):
        with self.assertRaisesRegex(ValueError, 'b79e65be'):
            D.profile_for(dict(PREVIOUS, incarnation='0' * 64), NEW, '9' * 64, 'key-id', b's', b'h', 5000)

    def test_opening_parameters_are_the_owner_envelope(self):
        fields = dict(line.split('=', 1) for line in D.opening_parameters(10, 20).decode().splitlines())
        self.assertEqual(fields, dict(schema='1', now_ms='10', review_expires_ms='20', managed_quote_atoms='25000000', kill_epoch='1',
                                      executor_generation='1', max_buy_price_per_sol='200000000', min_sell_price_per_sol='20000000'))


if __name__ == '__main__':
    unittest.main()
