"""One-second candle closes become available one second after their open."""
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('venue_basis', Path(os.environ.get('BASIS_SCRIPT', ROOT / 'docs/evidence/venue_price_basis_2026-10-09/basis.py')))
basis = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(basis)


class BasisTimingTests(unittest.TestCase):
    def test_close_is_available_after_its_interval(self):
        rows = [[1000, '10', '12', '9', '11', '0', 1999], [2000, '11', '14', '10', '13', '0', 2999]]
        with patch.object(basis.urllib.request, 'urlopen', return_value=io.BytesIO(json.dumps(rows).encode())), patch.object(basis.time, 'sleep'):
            closes = basis.binance_closes(1, 3, None)
        self.assertEqual(closes, {2: 11.0, 3: 13.0})
        self.assertNotIn(1, closes)

    def test_old_timestamp_cache_is_refused(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'docs/evidence/live_promotion_review_2026-10-09') as directory:
            cache = Path(directory) / 'old.json'
            cache.write_text('{"1": 11}')
            with patch.object(basis.urllib.request, 'urlopen') as network:
                with self.assertRaisesRegex(ValueError, 'timestamp convention'):
                    basis.binance_closes(1, 3, cache)
                network.assert_not_called()


if __name__ == '__main__':
    unittest.main()
