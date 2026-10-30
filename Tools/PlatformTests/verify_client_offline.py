#!/usr/bin/env python3
"""Exercise actual client command parsing and offline refusal paths, without game data."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import verify_client_entry as entry

FLAG = '--offline-check'
RECEIPT = 'Esteria-local-offline-check.json'
CHECKS = ('actualCommandParser', 'osUrlRejected', 'blizzardUrlRejected', 'initiationFailureCallbacksPreserved',
          'trackerReturned', 'hostConnectionRejected', 'numericConnectionRejected', 'invalidPortRejected',
          'connectFailureCallbacks', 'connectionRefsReleased')


def read_receipt(path, architecture):
    with path.open('rb') as stream:raw = stream.read(4097)
    entry.require(len(raw) <= 4096, 'offline receipt exceeds bound')
    data = json.loads(raw)
    expected = dict(schemaVersion=1, scope='executable-local-offline-boundaries-only', architecture=architecture,
                    protocolBuild=12340, checks={name: True for name in CHECKS}, remoteAttemptsRejected=3,
                    failureCallbacks=3, commonMainStarted=False, assetsLoaded=False, loopbackGameplayQualified=False)
    entry.require(type(data) is dict and set(data) == set(expected) and all(type(data[name]) is type(value) and
                  data[name] == value for name, value in expected.items()) and
                  all(data['checks'].get(name) is True for name in CHECKS), 'offline receipt scope or checks differ')
    return data, hashlib.sha256(raw).hexdigest()


def verify(directory, architecture, output):
    # Reuse exact image/hash/native-architecture checks and the actual entry test;
    # its receipt remains independent and does not imply this new scope passed.
    prior = entry.verify(directory, architecture, output)
    output = output.absolute();work = output / 'working-copy';evidence = output / 'evidence'
    executable = str(work / 'WowClient64.exe')
    report = dict(scope='native-windows-client-offline-boundaries-only', architecture=architecture,
                  sourceRevision=prior['sourceRevision'], buildId=prior['buildId'], qualified=False,
                  gameInitializationStarted=False, loopbackGameplayQualified=False, cases=[])
    try:
        for index, arguments in enumerate(((FLAG,), (FLAG, '--local-offline', 'extra'),
                (FLAG + '=1', '--local-offline'), (FLAG.upper(), '--local-offline'))):
            code = entry.run_bounded([executable, *arguments], work, evidence / f'offline-invalid-{index}.log')
            entry.require(code == 64 and not (work / RECEIPT).exists(), 'malformed offline diagnostic reached normal startup')
        for spelling in ('-local-offline', '--local-offline'):
            label = 'short' if spelling.startswith('-local') else 'long'
            code = entry.run_bounded([executable, FLAG, spelling], work, evidence / ('offline-' + label + '.log'))
            entry.require(code == 0, 'actual offline command/parser/boundary check failed')
            receipt, digest = read_receipt(work / RECEIPT, architecture)
            shutil.copyfile(work / RECEIPT, evidence / ('offline-' + label + '.json'))
            report['cases'].append(dict(spelling=spelling, exitCode=code, receipt=receipt, receiptSha256=digest))
            if label == 'short':(work / RECEIPT).unlink() # Only this verified private fixture receipt.
        code = entry.run_bounded([executable, FLAG, '--local-offline'], work, evidence / 'offline-existing.log')
        entry.require(code == 66 and read_receipt(work / RECEIPT, architecture)[1] == digest,
                      'existing offline diagnostic receipt was changed')
        report['existingReceiptPreserved'] = True;report['qualified'] = True
        return report
    finally:
        (evidence / 'offline-qualification.json').write_text(json.dumps(report, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--architecture', choices=('ARM64', 'x64'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args();result = verify(args.directory, args.architecture, args.output)
    print(json.dumps({key: result[key] for key in ('scope', 'architecture', 'qualified', 'gameInitializationStarted')}))


if __name__ == '__main__':main()
