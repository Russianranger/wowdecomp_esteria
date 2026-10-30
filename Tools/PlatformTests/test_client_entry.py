import json
from pathlib import Path
import sys
import tempfile
import unittest
import verify_client_entry as entry


class ClientEntryTests(unittest.TestCase):
    def test_receipt_requires_every_real_check_and_excludes_game_initialization(self):
        receipt = dict(schemaVersion=1, scope="executable-entry-only", architecture="ARM64",
                       protocolBuild=12340, extensionVersion=4,
                       checks={name: True for name in entry.CHECKS}, commonMainStarted=False, assetsLoaded=False)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / entry.RECEIPT
            path.write_text(json.dumps(receipt))
            self.assertEqual(entry.read_receipt(path, "ARM64")[0], receipt)
            for key in entry.CHECKS:
                wrong = dict(receipt, checks=dict(receipt["checks"], **{key: 1}))
                path.write_text(json.dumps(wrong))
                with self.assertRaises(ValueError):
                    entry.read_receipt(path, "ARM64")
            for key in ("commonMainStarted", "assetsLoaded"):
                path.write_text(json.dumps(dict(receipt, **{key: True})))
                with self.assertRaises(ValueError):
                    entry.read_receipt(path, "ARM64")
            path.write_bytes(b" " * 4097)
            with self.assertRaises(ValueError):
                entry.read_receipt(path, "ARM64")

    def test_output_is_drained_but_never_retained_beyond_limit(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            log = directory / "large.log"
            code = "import os\nfor i in range(320): os.write(1,b'x'*65536)"
            with self.assertRaisesRegex(ValueError, "within bound"):
                entry.run_bounded([sys.executable, "-c", code], directory, log)
            self.assertEqual(log.stat().st_size, entry.LOG_LIMIT)
            self.assertEqual(entry.run_bounded([sys.executable, "-c", "print('entry helper output')"],
                                               directory, directory / "small.log"), 0)
            self.assertIn(b"entry helper output", (directory / "small.log").read_bytes())


if __name__ == "__main__":
    unittest.main()
