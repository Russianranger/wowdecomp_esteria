#!/usr/bin/env python3
"""Exercise actual Grunt/SRP callbacks against an owned synthetic native peer."""
import argparse
import hashlib
import hmac
import json
from pathlib import Path
import shutil
import stat

import auth_peer
import verify_client_entry as entry
import verify_client_loopback as loopback

FLAG='--auth-check'
RECEIPT='Esteria-auth-check.json'
INPUT='Esteria-auth-input.bin'
KEY='Esteria-auth-session.key'
CHECKS=('actualCommandParser','localOfflineEnabled','twoWorkerThreads','gruntTimerThread','connectedCallback',
        'newSessionRequest','versionChallengeObserved','serverProofVerified','realmListCallback','controlledDisconnect',
        'connectionRefsReleased','gruntTimerJoined','networkThreadsJoined')


def read_receipt(path, architecture, fixture_id):
    info=path.lstat();entry.require(stat.S_ISREG(info.st_mode) and info.st_nlink==1,'plain new auth receipt required')
    with path.open('rb') as stream:raw=stream.read(4097)
    entry.require(len(raw)<=4096,'auth receipt size bound')
    value=json.loads(raw)
    expected=dict(schemaVersion=1,scope='executable-grunt-authentication-only',architecture=architecture,
        protocolBuild=12340,fixtureId=fixture_id,checks={name:True for name in CHECKS},authentications=1,realms=1,realmId=1,
        worldEndpoint='127.0.0.1:8085',privateSessionKeyBytes=40,versionChecksumQualified=False,commonMainStarted=False,
        assetsLoaded=False,compiledServerQualified=False,worldEntryQualified=False,gameplayQualified=False)
    entry.require(type(value) is dict and set(value)==set(expected)|{'realmFlags','realmBuildPresent'} and
        all(type(value[name]) is type(item) and value[name]==item for name,item in expected.items()) and
        all(value['checks'][name] is True for name in CHECKS),'auth receipt scope/checks differ')
    entry.require(type(value['realmFlags']) is int and 0<=value['realmFlags']<=255 and
        type(value['realmBuildPresent']) is bool and value['realmBuildPresent']==bool(value['realmFlags']&4),'realm flags/shape differ')
    return value,hashlib.sha256(raw).hexdigest()


def private_key(path):
    info=path.lstat();entry.require(stat.S_ISREG(info.st_mode) and info.st_nlink==1 and info.st_size==40,'private key output shape')
    with path.open('rb') as stream:raw=stream.read(41)
    entry.require(len(raw)==40,'private key output size');return raw


def export_log(private, public):
    # Never export private input/key or arbitrary client/crash diagnostic text.
    # These fixed status lines suffice to identify the failed fixture phase.
    with private.open('rb') as stream:raw=stream.read(entry.LOG_LIMIT+1)
    entry.require(len(raw)<=entry.LOG_LIMIT,'auth diagnostic bound')
    allowed=('PASS: Esteria actual Grunt/SRP callbacks,', 'FAIL: Esteria Grunt authentication check:',
             'ESTERIA_LOCAL_OFFLINE enabled;')
    lines=[line for line in raw.decode('utf-8',errors='replace').splitlines() if line.startswith(allowed)]
    public.write_text('\n'.join(lines)+'\n',encoding='utf-8')


def verify(directory, architecture, output):
    prior=loopback.verify(directory,architecture,output)
    output=output.absolute();work=output/'working-copy';evidence=output/'evidence'
    executable=str(work/'WowClient64.exe');command=[executable,FLAG,'--local-offline']
    report=dict(scope='native-windows-reconstructed-grunt-synthetic-peer-only',architecture=architecture,
        sourceRevision=prior['sourceRevision'],buildId=prior['buildId'],qualified=False,cases=[],
        compiledServerQualified=False,versionChecksumQualified=False,gameInitializationStarted=False,
        worldEntryQualified=False,gameplayQualified=False)
    try:
        for index,arguments in enumerate(((FLAG,), (FLAG,'--local-offline','extra'),
                                         (FLAG+'=1','--local-offline'),(FLAG.upper(),'--local-offline'))):
            log=work/('auth-invalid-%d.log'%index)
            code=entry.run_bounded([executable,*arguments],work,log)
            entry.require(code==64 and not (work/RECEIPT).exists() and not (work/KEY).exists(),'malformed auth mode reached startup')
        code=entry.run_bounded(command,work,work/'auth-missing-input.log')
        entry.require(code==65 and not (work/RECEIPT).exists() and not (work/KEY).exists(),'missing private auth fixture accepted')
        for index,raw in enumerate((b'ESTAUTH1',bytes(57),b'ESTAUTH1'+bytes(49))):
            try:
                with (work/INPUT).open('xb') as stream:stream.write(raw)
                code=entry.run_bounded(command,work,work/('auth-malformed-input-%d.log'%index))
                entry.require(code==65 and not (work/RECEIPT).exists() and not (work/KEY).exists(),
                              'malformed private auth fixture accepted')
            finally:(work/INPUT).unlink(missing_ok=True)
        report['malformedInputRejected']=True
        for index,mode in enumerate(('valid','bad-server-proof','remote-realm')):
            value=auth_peer.credentials();auth_peer.write_input(work/INPUT,value)
            try:
                with auth_peer.Peer(value,mode) as peer:
                    log=work/('auth-%d-private.log'%index)
                    try:code=entry.run_bounded(command,work,log)
                    finally:
                        if log.exists():export_log(log,evidence/('auth-%d.log'%index))
                    peer.finish()
                    case=dict(mode=mode,exitCode=code,peerVerifiedClientProof=peer.proofVerified,peerObservedClose=peer.peerClosed)
                    report['cases'].append(case)
                    if mode=='valid':
                        entry.require(code==0,'actual Grunt client failed synthetic authentication')
                        receipt,digest=read_receipt(work/RECEIPT,architecture,value['fixtureId'])
                        entry.require(hmac.compare_digest(private_key(work/KEY),peer.key),'actual client/server synthetic keys differ')
                        entry.require(peer.realmRequested and receipt['realmFlags']==6 and receipt['realmBuildPresent'],
                                      'actual synthetic realm callback differs')
                        shutil.copyfile(work/RECEIPT,evidence/'auth.json')
                        report.update(receipt=receipt,receiptSha256=digest,privateSessionKeyMatched=True)
                    else:
                        entry.require(code==65 and not (work/RECEIPT).exists() and not (work/KEY).exists(),
                                      'invalid server proof or nonlocal realm was accepted')
                if mode=='valid':
                    original_key=private_key(work/KEY)
                    code=entry.run_bounded(command,work,work/'auth-existing-private.log')
                    entry.require(code==66 and read_receipt(work/RECEIPT,architecture,value['fixtureId'])[1]==digest and
                                  hmac.compare_digest(private_key(work/KEY),original_key),'existing private/public auth output changed')
                    report['existingOutputsPreserved']=True
                    (work/RECEIPT).unlink();(work/KEY).unlink()
            finally:
                (work/INPUT).unlink(missing_ok=True)
                (work/KEY).unlink(missing_ok=True)
        report['qualified']=True;return report
    finally:
        (evidence/'auth-qualification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=Path,required=True);parser.add_argument('--architecture',choices=('ARM64','x64'),required=True)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    result=verify(args.directory,args.architecture,args.output)
    print(json.dumps({name:result[name] for name in ('scope','architecture','qualified','compiledServerQualified','gameplayQualified')}))


if __name__=='__main__':main()
