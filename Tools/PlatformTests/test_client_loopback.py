import copy
import json
from pathlib import Path
import tempfile
import unittest
import verify_client_loopback as fixture


class LoopbackReceipt(unittest.TestCase):
    def test_all_actual_checks_counts_and_narrow_scope_required(self):
        valid = dict(schemaVersion=1, scope='executable-loopback-transport-only', architecture='ARM64',
                     protocolBuild=12340, checks={name: True for name in fixture.CHECKS}, connections=2,
                     receivedFrames=2, sentFrames=2, receivedStreamBytes=1027, sentStreamBytes=1031,
                     commonMainStarted=False, assetsLoaded=False, authenticationQualified=False, gameplayQualified=False)
        with tempfile.TemporaryDirectory() as root:
            receipt = Path(root) / 'receipt.json'
            receipt.write_text(json.dumps(valid));self.assertEqual(valid, fixture.read_receipt(receipt, 'ARM64')[0])
            invalid = []
            for key in ('protocolBuild', 'connections', 'receivedFrames', 'sentFrames', 'receivedStreamBytes', 'sentStreamBytes'):
                bad = copy.deepcopy(valid);bad[key] = 0;invalid.append(bad)
            for key in ('commonMainStarted', 'assetsLoaded', 'authenticationQualified', 'gameplayQualified'):
                bad = copy.deepcopy(valid);bad[key] = True;invalid.append(bad)
            for key in fixture.CHECKS:
                bad = copy.deepcopy(valid);bad['checks'][key] = False;invalid.append(bad)
            bad = copy.deepcopy(valid);bad['checks']['extra'] = True;invalid.append(bad)
            bad = copy.deepcopy(valid);bad['connections'] = 2.0;invalid.append(bad)
            bad = copy.deepcopy(valid);bad['architecture'] = 'x64';invalid.append(bad)
            for bad in invalid:
                receipt.write_text(json.dumps(bad))
                with self.assertRaises(ValueError):fixture.read_receipt(receipt, 'ARM64')
            receipt.write_bytes(b' ' * 4097)
            with self.assertRaises(ValueError):fixture.read_receipt(receipt, 'ARM64')


if __name__ == '__main__':unittest.main()
