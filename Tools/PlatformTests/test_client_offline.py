import copy
import json
from pathlib import Path
import tempfile
import unittest
import verify_client_offline as offline


class OfflineReceipt(unittest.TestCase):
    def test_actual_checks_and_scope_must_all_match(self):
        good = dict(schemaVersion=1, scope='executable-local-offline-boundaries-only', architecture='ARM64',
                    protocolBuild=12340, checks={name: True for name in offline.CHECKS}, remoteAttemptsRejected=3,
                    failureCallbacks=3, commonMainStarted=False, assetsLoaded=False, loopbackGameplayQualified=False)
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / 'receipt.json';path.write_text(json.dumps(good));offline.read_receipt(path, 'ARM64')
            changes = [('failureCallbacks', 0), ('remoteAttemptsRejected', 2), ('schemaVersion', True),
                       ('architecture', 'x64'), ('protocolBuild', 12341), ('commonMainStarted', True),
                       ('assetsLoaded', True), ('loopbackGameplayQualified', True)]
            for key, value in changes:
                bad = copy.deepcopy(good);bad[key] = value;path.write_text(json.dumps(bad))
                with self.subTest(key=key), self.assertRaises(ValueError):offline.read_receipt(path, 'ARM64')
            for check in offline.CHECKS:
                bad = copy.deepcopy(good);bad['checks'][check] = False;path.write_text(json.dumps(bad))
                with self.subTest(check=check), self.assertRaises(ValueError):offline.read_receipt(path, 'ARM64')


if __name__ == '__main__':unittest.main()
