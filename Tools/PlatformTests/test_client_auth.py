import copy
import json
from pathlib import Path
import tempfile
import unittest

import auth_peer
import verify_client_auth as auth


class AuthFixture(unittest.TestCase):
    def test_server_math_matches_independent_fixed_client_vector_and_rejects_wrong_proof(self):
        # Fixed public test credentials; vector cross-checked with the integration
        # client codec already accepted by the pinned server's real C++ SRP6.
        exchange=auth_peer.Exchange('ADMIN','PASSWORD',bytes(range(32)),bytes([7])*32)
        self.assertEqual(exchange.public.hex(),'32af1e660e96e421d17169e141b4e3dec4100bf95a928c6813883109842fd181')
        public=bytes.fromhex('64b1ba8bc318485f1dd14917e34680ecb842c44f3b2ef3f20ebefc302f159968')
        proof=bytes.fromhex('c4de8c7c1284d232c62ed8e93567e50eb61d9258')
        m2,key=exchange.accept(public,proof)
        self.assertEqual(m2.hex(),'fbae762fb71445b6be62e8212dd1590a86df1b06')
        self.assertEqual(key.hex(),'15cd41f86da0dec25ea202824a48b5cccdd29a47d86295cf8c6cafad08084c36b15afa880f220b1a')
        for a,m in ((public,bytes([proof[0]^1])+proof[1:]),(bytes(32),proof),(auth_peer.N_BYTES,proof)):
            with self.assertRaises(ValueError):exchange.accept(a,m)

    def test_private_input_is_exact_bounded_and_exclusive(self):
        with tempfile.TemporaryDirectory() as raw:
            path=Path(raw)/'input';value=auth_peer.credentials();auth_peer.write_input(path,value)
            content=path.read_bytes();self.assertEqual(len(content),57);self.assertEqual(content[:8],b'ESTAUTH1')
            self.assertEqual(content[8:24],bytes.fromhex(value['fixtureId']))
            with self.assertRaises(FileExistsError):auth_peer.write_input(path,value)
            for name,item in (('username','ADMIN'),('password','x'*16),('fixtureId','not-a-nonce')):
                with self.assertRaises(ValueError):auth_peer.write_input(path.with_name('new'),dict(value,**{name:item}))

    def test_receipt_requires_all_real_checks_fresh_nonce_and_narrow_scope(self):
        fixture='12'*16
        good=dict(schemaVersion=1,scope='executable-grunt-authentication-only',architecture='ARM64',protocolBuild=12340,
            fixtureId=fixture,checks={name:True for name in auth.CHECKS},authentications=1,realms=1,realmId=1,
            worldEndpoint='127.0.0.1:8085',realmFlags=6,realmBuildPresent=True,privateSessionKeyBytes=40,
            versionChecksumQualified=False,commonMainStarted=False,assetsLoaded=False,compiledServerQualified=False,
            worldEntryQualified=False,gameplayQualified=False)
        with tempfile.TemporaryDirectory() as raw:
            path=Path(raw)/'receipt';path.write_text(json.dumps(good));auth.read_receipt(path,'ARM64',fixture)
            for key,value in [('schemaVersion',True),('fixtureId','ab'*16),('worldEndpoint','203.0.113.1:8085'),
                ('authentications',False),('realmFlags',True),('realmBuildPresent',False),('privateSessionKeyBytes',0),
                ('versionChecksumQualified',True),('compiledServerQualified',True),('worldEntryQualified',True),('gameplayQualified',True)]:
                bad=copy.deepcopy(good);bad[key]=value;path.write_text(json.dumps(bad))
                with self.subTest(key=key),self.assertRaises(ValueError):auth.read_receipt(path,'ARM64',fixture)
            for check in auth.CHECKS:
                bad=copy.deepcopy(good);bad['checks'][check]=False;path.write_text(json.dumps(bad))
                with self.subTest(check=check),self.assertRaises(ValueError):auth.read_receipt(path,'ARM64',fixture)

    def test_export_omits_arbitrary_private_client_and_crash_output(self):
        with tempfile.TemporaryDirectory() as raw:
            root=Path(raw);source=root/'private';output=root/'public'
            source.write_text('private-account-password\nRegister value: raw-session-key\n'
                              'FAIL: Esteria Grunt authentication check: actual Grunt/SRP proof and callbacks\n')
            auth.export_log(source,output)
            self.assertNotIn('password',output.read_text());self.assertNotIn('session-key',output.read_text())
            self.assertIn('actual Grunt/SRP proof',output.read_text())


if __name__=='__main__':unittest.main()
