#!/usr/bin/env python3
"""Run the actual client's loopback workers/framing, separately from login/gameplay."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import verify_client_entry as entry
import verify_client_offline as offline

FLAG = '--loopback-check'
RECEIPT = 'Esteria-loopback-check.json'
CHECKS = ('actualCommandParser', 'localOfflineEnabled', 'twoWorkerThreads', 'loopbackConnectCallbacks',
          'partialFrameHeaders', 'shortAndExtendedFrameBytes', 'queuedPacketSendBytes', 'streamReadAndSendBytes',
          'controlledDisconnectCallbacks', 'connectionRefsReleased', 'networkThreadsJoined', 'stopTimeoutRetainsNetwork')


def read_receipt(path, architecture):
    with path.open('rb') as stream:raw = stream.read(4097)
    entry.require(len(raw) <= 4096, 'loopback receipt exceeds bound')
    data = json.loads(raw)
    expected = dict(schemaVersion=1, scope='executable-loopback-transport-only', architecture=architecture,
                    protocolBuild=12340, checks={name: True for name in CHECKS}, connections=2,
                    receivedFrames=2, sentFrames=2, receivedStreamBytes=1027, sentStreamBytes=1031,
                    commonMainStarted=False, assetsLoaded=False, authenticationQualified=False, gameplayQualified=False)
    entry.require(type(data) is dict and set(data) == set(expected) and all(type(data[name]) is type(value) and
                  data[name] == value for name, value in expected.items()) and
                  all(data['checks'].get(name) is True for name in CHECKS), 'loopback receipt scope or checks differ')
    return data, hashlib.sha256(raw).hexdigest()


def verify(directory, architecture, output):
    prior = offline.verify(directory, architecture, output)
    output = output.absolute();work = output / 'working-copy';evidence = output / 'evidence'
    executable = str(work / 'WowClient64.exe')
    report = dict(scope='native-windows-client-loopback-transport-only', architecture=architecture,
                  sourceRevision=prior['sourceRevision'], buildId=prior['buildId'], qualified=False,
                  gameInitializationStarted=False, authenticationQualified=False, gameplayQualified=False)
    try:
        for index, arguments in enumerate(((FLAG,), (FLAG, '--local-offline', 'extra'),
                (FLAG + '=1', '--local-offline'), (FLAG.upper(), '--local-offline'))):
            code = entry.run_bounded([executable, *arguments], work, evidence / f'loopback-invalid-{index}.log')
            entry.require(code == 64 and not (work / RECEIPT).exists(), 'malformed loopback diagnostic reached normal startup')
        command = [executable, FLAG, '--local-offline']
        code = entry.run_bounded(command, work, evidence / 'loopback.log')
        report['exitCode'] = code
        entry.require(code == 0, 'actual loopback transport diagnostic failed')
        receipt, digest = read_receipt(work / RECEIPT, architecture)
        shutil.copyfile(work / RECEIPT, evidence / 'loopback.json')
        report.update(receipt=receipt, receiptSha256=digest)
        code = entry.run_bounded(command, work, evidence / 'loopback-existing.log')
        entry.require(code == 66 and read_receipt(work / RECEIPT, architecture)[1] == digest,
                      'existing loopback diagnostic receipt was changed')
        report['existingReceiptPreserved'] = True;report['qualified'] = True
        return report
    finally:
        (evidence / 'loopback-qualification.json').write_text(json.dumps(report, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--architecture', choices=('ARM64', 'x64'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args();result = verify(args.directory, args.architecture, args.output)
    print(json.dumps({key: result[key] for key in ('scope', 'architecture', 'qualified', 'gameInitializationStarted')}))


if __name__ == '__main__':main()
