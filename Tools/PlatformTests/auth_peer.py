"""Owned synthetic build12340 SRP peer for the actual reconstructed executable.

This fixture is not the compiled Esteria authserver. It exercises the client's
real SRP/framing/callback path; the paired integration gate must also run that
client against the pinned server and compare its persisted session key.
"""
import hashlib
import hmac
import os
from pathlib import Path
import re
import secrets
import socket
import struct
import threading
import time

N = int('894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7', 16)
N_BYTES = N.to_bytes(32, 'little')
VERSION_CHALLENGE = bytes.fromhex('baa31e99a00b2157fc373fb369cdd2f1')
ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'


class FixtureFailure(ValueError):
    pass


def require(value, message):
    if not value:raise FixtureFailure(message)


def sha(*blocks):
    value=hashlib.sha1()
    for block in blocks:value.update(block)
    return value.digest()


def interleave(secret):
    require(len(secret)==32,'shared secret length')
    count=0
    while count<32 and secret[count]==0:count+=1
    count += count & 1
    first, second=sha(secret[count::2]),sha(secret[count+1::2])
    return bytes(value for pair in zip(first,second) for value in pair)


class Exchange:
    def __init__(self, username, password, salt=None, private=None):
        require(username.isascii() and password.isascii() and 0<len(username)<=17 and 0<len(password)<=16,'fixture credential shape')
        self.name=username.upper().encode('ascii')
        self.salt=secrets.token_bytes(32) if salt is None else salt
        self.private=secrets.token_bytes(32) if private is None else private
        require(len(self.salt)==len(self.private)==32 and any(self.private),'fixture SRP scalar shape')
        x=int.from_bytes(sha(self.salt,sha(self.name+b':'+password.upper().encode('ascii'))),'little')
        self.verifier=pow(7,x,N)
        self.public=((3*self.verifier+pow(7,int.from_bytes(self.private,'little'),N))%N).to_bytes(32,'little')
        require(any(self.public),'fixture public scalar is zero')

    def accept(self, public, proof):
        require(len(public)==32 and len(proof)==20,'client proof size')
        a=int.from_bytes(public,'little');require(0<a<N,'client public scalar range')
        u=int.from_bytes(sha(public,self.public),'little');require(u!=0,'client scrambling scalar is zero')
        secret=pow((a*pow(self.verifier,u,N))%N,int.from_bytes(self.private,'little'),N).to_bytes(32,'little')
        key=interleave(secret)
        xor=bytes(a^b for a,b in zip(sha(N_BYTES),sha(b'\x07')))
        expected=sha(xor,sha(self.name),self.salt,public,self.public,key)
        require(hmac.compare_digest(proof,expected),'actual client SRP proof rejected')
        return sha(public,proof,key),key


def credentials():
    return dict(username='EST'+secrets.token_hex(7).upper(),password=''.join(secrets.choice(ALPHABET) for _ in range(16)),
                fixtureId=secrets.token_hex(16))


def write_input(path, value):
    require(re.fullmatch('EST[0-9A-F]{14}',value['username']) and re.fullmatch('['+ALPHABET+']{16}',value['password']) and
            re.fullmatch('[0-9a-f]{32}',value['fixtureId']),'private fixture input shape')
    raw=b'ESTAUTH1'+bytes.fromhex(value['fixtureId'])+value['username'].encode()+value['password'].encode()
    descriptor=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|getattr(os,'O_NOFOLLOW',0),0o600)
    with os.fdopen(descriptor,'wb') as target:target.write(raw);target.flush();os.fsync(target.fileno())


class Peer:
    """Own one fixed loopback listener and one bounded synthetic client exchange."""
    def __init__(self, value, mode='valid'):
        require(mode in ('valid','bad-server-proof','remote-realm'),'fixture mode')
        self.mode=mode;self.value=value;self.stop=threading.Event();self.error=None;self.key=None
        self.proofVerified=False;self.realmRequested=False;self.peerClosed=False
        self.listener=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
        self.connection=None
        try:
            if os.name=='nt':self.listener.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
            self.listener.bind(('127.0.0.1',3724));self.listener.listen(1);self.listener.settimeout(.2)
            self.thread=threading.Thread(target=self._run,daemon=True);self.thread.start()
        except BaseException:self.listener.close();raise

    def _run(self):
        deadline=time.monotonic()+25
        try:
            while not self.stop.is_set():
                require(time.monotonic()<deadline,'fixture accept deadline')
                try:self.connection,address=self.listener.accept();break
                except socket.timeout:pass
            require(self.connection is not None and address[0]=='127.0.0.1','actual local client connection')
            connection=self.connection
            def take(size):
                raw=bytearray()
                while len(raw)<size:
                    remain=deadline-time.monotonic();require(remain>0,'fixture packet deadline')
                    connection.settimeout(remain);part=connection.recv(size-len(raw));require(part,'incomplete client packet');raw.extend(part)
                return bytes(raw)
            fixed=take(34)
            require(fixed[:4]==b'\x00\x08\x2f\x00' and fixed[4:13]==b'WoW\0\3\3\5\x34\x30' and
                    fixed[13:25]==b'68x\0niW\0SUne' and fixed[25:34]==b'\0\0\0\0\x7f\0\0\1\x11',
                    'actual build12340 classic challenge header')
            require(take(17)==self.value['username'].encode(),'actual challenge account')
            exchange=Exchange(self.value['username'],self.value['password'])
            challenge=b'\0\0\0'+exchange.public+b'\1\7\x20'+N_BYTES+exchange.salt+VERSION_CHALLENGE+b'\0'
            connection.sendall(challenge[:23]);time.sleep(.03);connection.sendall(challenge[23:])
            proof=take(75);require(proof[0]==1 and proof[-2:]==b'\0\0','actual logon-proof framing/security')
            server_proof,self.key=exchange.accept(proof[1:33],proof[33:53]);self.proofVerified=True
            require(proof[53:73]==sha(proof[1:33],bytes(20)),'explicit non-game version checksum field')
            if self.mode=='bad-server-proof':server_proof=bytes([server_proof[0]^1])+server_proof[1:]
            reply=b'\1\0'+server_proof+bytes(10)
            # Deliberately split before the survey/flags tail to exercise the
            # actual stream parser's incomplete-proof retention.
            connection.sendall(reply[:26]);time.sleep(.03);connection.sendall(reply[26:])
            if self.mode!='bad-server-proof':
                require(take(5)==b'\x10'+bytes(4),'actual realm request');self.realmRequested=True
                address=b'127.0.0.1:8085' if self.mode=='valid' else b'203.0.113.1:8085'
                body=struct.pack('<IH',0,1)+bytes((0,0,6))+b'Esteria fixture\0'+address+b'\0'+struct.pack('<fBBB',.5,0,1,1)+struct.pack('<BBBH',3,3,5,12340)+b'\x10\0'
                wire=b'\x10'+struct.pack('<H',len(body))+body
                connection.sendall(wire[:7]);time.sleep(.03);connection.sendall(wire[7:])
            connection.settimeout(max(.1,deadline-time.monotonic()))
            require(connection.recv(1)==b'','client must close this auth connection');self.peerClosed=True
        except BaseException as error:
            # Exception objects from I/O/codecs can contain private bytes. Only
            # this controlled reason is eligible for public qualification output.
            self.error=str(error) if isinstance(error,FixtureFailure) else 'owned synthetic SRP peer I/O did not complete'
        finally:
            if self.connection is not None:self.connection.close()
            self.listener.close()

    def finish(self):
        self.thread.join(timeout=2)
        require(not self.thread.is_alive() and self.error is None and self.proofVerified and self.peerClosed,
                self.error or 'owned synthetic auth exchange failed')

    def close(self):
        self.stop.set()
        if self.connection is not None:
            try:self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:pass
        self.listener.close();self.thread.join(timeout=2)
        require(not self.thread.is_alive(),'owned auth peer failed to stop')

    def __enter__(self):return self
    def __exit__(self,*_):self.close()
