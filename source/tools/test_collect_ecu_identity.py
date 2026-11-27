from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import socket
import struct
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

import collect_ecu_identity as identity

TOOL=Path(identity.__file__)


def records():
    return [{"ecu":f"{ecu:02x}","request":request.hex(" "),
             "reply":(b"\x62"+request[1:]+b"PRIVATE-ID-123456789").hex(" "),
             "vin":"PRIVATEVIN", "error":"PRIVATE EXCEPTION"}
            for ecu,request in identity.REQUESTS]


class IdentityEvidenceTest(unittest.TestCase):
    def invoke(self,args):
        with patch.object(sys,'argv',[str(TOOL),*map(str,args)]),redirect_stdout(io.StringIO()):
            identity.main()

    def test_plan_is_exactly_six_single_did_reads_without_identifying_dids_or_writes(self):
        expected={(ecu,bytes.fromhex(did)) for ecu in (0x12,0x18) for did in ('22f191','22f188','22f189')}
        plan=identity.plan()
        self.assertEqual(set(identity.REQUESTS),expected)
        self.assertEqual(identity.ALLOWLIST,expected)
        self.assertEqual(len(identity.REQUESTS),6)
        self.assertEqual(plan['max_requests'],6)
        self.assertEqual(plan['max_duration_seconds'],45)
        self.assertEqual(plan['minimum_gap_seconds'],.2)
        self.assertEqual(plan['transaction_reserve_seconds'],6.2)
        self.assertEqual(plan['retries'],0)
        self.assertFalse(plan['writes_or_session_changes'])
        self.assertFalse(plan['vin_serial_or_programming_fingerprint_reads'])
        self.assertFalse(plan['ssh_or_most'])

    def test_summary_exports_only_status_and_lengths_never_identity_values_or_hashes(self):
        result=identity.public_report(records())
        self.assertEqual(result['format'],'idrive-ecu-identity-evidence-v1')
        self.assertFalse(result['raw_values_exported'])
        self.assertFalse(result['raw_value_hashes_exported'])
        self.assertFalse(result['hardware_software_identified'])
        self.assertEqual(len(result['fields']),6)
        for field in result['fields']:
            self.assertEqual(field['record_status'],'positive_uninterpreted')
            self.assertEqual(field['data_bytes'],20)
        text=json.dumps(result)
        for forbidden in ('PRIVATE','50 52 49 56 41 54 45','hash_value','reply','request','VIN-OR'):
            self.assertNotIn(forbidden,text)
        self.assertEqual(result['engine_family'],'not_inferred')
        self.assertEqual(result['firmware_approval'],'not_established')

    def test_positive_shape_boundaries_do_not_supply_a_manufacturer_codec(self):
        for length in (0,1,2,16,17,255,256,257,8189):
            row=records()[0]
            row['reply']=(bytes.fromhex('62f191')+b'X'*length).hex()
            field=identity.public_report([row])['fields'][0]
            self.assertEqual(field['record_status'],'positive_uninterpreted' if 1<=length<=256 else 'malformed_or_out_of_bounds')
            self.assertEqual('data_bytes' in field,1<=length<=256)
            self.assertEqual(field['identifier'],'unavailable_pending_private_review')

    def test_exact_did_prefix_ecu_and_negative_response_shapes(self):
        for reply in ('62f18801','61f19101','62f191','7f223100','7f3131','7f2278','',None,'not hex'):
            row=records()[0];row['reply']=reply
            field=identity.public_report([row])['fields'][0]
            self.assertNotEqual(field['record_status'],'positive_uninterpreted')
            self.assertNotIn('data_bytes',field)
        for nrc in (0x11,0x22,0x31,0x33):
            row=records()[0];row['reply']=bytes((0x7f,0x22,nrc)).hex()
            field=identity.public_report([row])['fields'][0]
            self.assertEqual(field['record_status'],'negative_response')
            self.assertEqual(field['nrc'],f'{nrc:02X}')
            self.assertNotIn('supported',field)
        row=records()[0];row['ecu']='18'
        result=identity.public_report([row])
        self.assertEqual(result['fields'][0]['record_status'],'not_present')
        self.assertEqual(result['fields'][3]['record_status'],'positive_uninterpreted')

    def test_duplicates_remain_ambiguous_and_unknown_records_never_survive(self):
        result=identity.public_report([*records(),records()[0],
            {'ecu':'12','request':'22f190','reply':'62f19050524956415445'},
            {'ecu':'12','request':'22f18c','reply':'62f18c50524956415445'},
            {'ecu':'29','request':'22f191','reply':'62f19150524956415445'}])
        self.assertEqual(result['fields'][0]['record_status'],'ambiguous_duplicate')
        self.assertEqual(sum(f['record_status']=='positive_uninterpreted' for f in result['fields']),5)
        for item in identity.normalize(records()):
            self.assertEqual(set(item),{'ecu','request','reply'})
        self.assertEqual(identity.normalize([None,12,{}, {'ecu':True,'request':'22f191'}]),[])
        for rows in (None,{},[{}]*257):
            with self.assertRaises(ValueError): identity.public_report(rows)
        with self.assertRaises(ValueError): identity.public_report([],evidence='trusted_vehicle')

    @patch('collect_ecu_identity.time.sleep')
    @patch('collect_ecu_identity.exchange')
    @patch('collect_ecu_identity.socket.create_connection')
    def test_collector_uses_explicit_endpoint_order_gap_and_no_retry(self,connect,exchange,sleep):
        def answer(connection,ecu,request): return {'reply':(b'\x62'+request[1:]+b'\x01').hex(),'vin':'PRIVATE'}
        exchange.side_effect=answer
        rows=identity.collect('169.254.10.2','169.254.10.3')
        self.assertEqual([(int(r['ecu'],16),bytes.fromhex(r['request'])) for r in rows],list(identity.REQUESTS))
        self.assertEqual(connect.call_count,6)
        self.assertEqual(sleep.call_count,6)
        self.assertTrue(all(c.args==(.2,) for c in sleep.call_args_list))
        self.assertTrue(all(c.args==(('169.254.10.3',6801),) and c.kwargs=={'timeout':2,'source_address':('169.254.10.2',0)} for c in connect.call_args_list))
        self.assertNotIn('PRIVATE',json.dumps(rows))

    @patch('collect_ecu_identity.time.sleep')
    @patch('collect_ecu_identity.socket.create_connection')
    def test_refusal_and_transport_failure_never_start_a_session_or_security_retry(self,connect,sleep):
        calls=[]
        def answer(connection,ecu,request):
            calls.append((ecu,request))
            if len(calls)==1: raise OSError('PRIVATE ERROR')
            return {'reply':'7f2233'}
        with patch('collect_ecu_identity.exchange',side_effect=answer):
            rows=identity.collect('169.254.10.2','169.254.10.3')
        self.assertEqual(calls,list(identity.REQUESTS))
        self.assertEqual(rows[0]['error'],'no_valid_response')
        self.assertNotIn('PRIVATE',json.dumps(rows))
        self.assertEqual(sum(f['record_status']=='negative_response' for f in identity.public_report(rows)['fields']),5)

    def test_slow_transactions_and_reserve_stay_inside_the_45_second_budget(self):
        clock=[0.0]
        def advance(seconds): clock[0]+=seconds
        def answer(connection,ecu,request): advance(6);return {'reply':'7f2231'}
        with patch('collect_ecu_identity.time.monotonic',side_effect=lambda:clock[0]), \
             patch('collect_ecu_identity.time.sleep',side_effect=advance), \
             patch('collect_ecu_identity.exchange',side_effect=answer), \
             patch('collect_ecu_identity.socket.create_connection'):
            rows=identity.collect('169.254.10.2','169.254.10.3')
        self.assertEqual(len(rows),6);self.assertLessEqual(clock[0],45)
        ticks=iter([0,39.0])
        with patch('collect_ecu_identity.time.monotonic',side_effect=lambda:next(ticks)), \
             patch('collect_ecu_identity.socket.create_connection') as connect:
            self.assertEqual(identity.collect('169.254.10.2','169.254.10.3'),[])
        connect.assert_not_called()

    def test_plan_and_replay_use_no_network_and_create_an_exclusive_private_bundle(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source=root/'input.jsonl';out=root/'evidence'
            source.write_text('\n'.join(json.dumps(r) for r in records()))
            with patch('collect_ecu_identity.socket.create_connection') as connect:
                self.invoke(['--plan'])
                self.invoke(['--replay',source,'--out',out])
            connect.assert_not_called()
            self.assertEqual(out.stat().st_mode&0o777,0o700)
            for file in out.iterdir(): self.assertEqual(file.stat().st_mode&0o777,0o600)
            private=(out/'private-records.jsonl').read_text()
            public=(out/'summary.json').read_text()
            self.assertIn('50 52 49 56 41 54 45',private)
            self.assertNotIn('50 52 49 56 41 54 45',public)
            self.assertNotIn('PRIVATE',public)
            self.assertEqual(json.loads(public)['evidence'],'replay_unverified_provenance')
            original=(out/'summary.json').read_bytes()
            with self.assertRaises(SystemExit): self.invoke(['--replay',source,'--out',out])
            self.assertEqual((out/'summary.json').read_bytes(),original)

    def test_collection_requires_new_plan_approval_and_separate_link_local_endpoints(self):
        cases=[['--collect'],['--collect','--owner-approved-parked'],
               ['--collect','--local','169.254.10.2','--gateway','169.254.10.3'],
               ['--collect','--owner-approved-parked','--local','169.254.10.2','--gateway','169.254.10.2'],
               ['--collect','--owner-approved-parked','--local','192.168.1.2','--gateway','169.254.10.3'],
               ['--plan','--owner-approved-parked'],['--plan','--out','unused']]
        for args in cases:
            with patch('collect_ecu_identity.socket.create_connection') as connect,patch('sys.stderr',io.StringIO()),self.assertRaises(SystemExit):
                self.invoke(args)
            connect.assert_not_called()

    def test_approved_cli_creates_private_directory_before_collecting_and_separates_outputs(self):
        with tempfile.TemporaryDirectory() as temp:
            out=Path(temp)/'new-evidence'
            def collect(local,gateway):
                self.assertTrue(out.is_dir())
                self.assertEqual(out.stat().st_mode&0o777,0o700)
                self.assertEqual((local,gateway),('169.254.10.2','169.254.10.3'))
                return records()
            with patch('collect_ecu_identity.collect',side_effect=collect) as mocked:
                self.invoke(['--collect','--owner-approved-parked','--local','169.254.10.2',
                             '--gateway','169.254.10.3','--out',out])
            mocked.assert_called_once()
            result=json.loads((out/'summary.json').read_text())
            self.assertEqual(result['evidence'],'parked_live_read_only')
            self.assertEqual(result['hsfz'],'response_observed')
            self.assertFalse(result['hardware_software_identified'])
            self.assertNotIn('PRIVATE',json.dumps(result))
            self.assertEqual(identity.public_report(records())['hsfz'],'replay_shape_only')

    def test_existing_folder_file_or_symlink_prevents_network_before_collection(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);folder=root/'folder';folder.mkdir();file=root/'file';file.write_text('keep')
            link=root/'link';link.symlink_to(root/'missing')
            for path in (folder,file,link):
                with patch('collect_ecu_identity.collect') as collect,self.assertRaises(SystemExit):
                    self.invoke(['--collect','--owner-approved-parked','--local','169.254.10.2',
                                 '--gateway','169.254.10.3','--out',path])
                collect.assert_not_called()
            self.assertEqual(file.read_text(),'keep')

    def test_replay_input_and_reply_size_limits_are_bounded(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'oversize';path.write_bytes(b' '*(identity.MAX_REPLAY_BYTES+1))
            with self.assertRaises(ValueError): identity.load_records(path)
        row=records()[0];row['reply']='aa'*8193
        self.assertEqual(identity.normalize([row])[0]['error'],'no_valid_response')
        row['reply']='aa'*8192
        self.assertEqual(len(bytes.fromhex(identity.normalize([row])[0]['reply'])),8192)

    def test_real_hsfz_framing_filters_pending_wrong_ecu_and_wrong_did(self):
        seen,threads,errors=[],[],[]
        def exact(connection,count):
            result=b''
            while len(result)<count:
                part=connection.recv(count-len(result))
                if not part: raise ConnectionError('local peer closed')
                result+=part
            return result
        def frame(control,payload): return struct.pack('>IH',len(payload),control)+payload
        def respond(gateway):
            with gateway:
                gateway.settimeout(2)
                try:
                    count,control=struct.unpack('>IH',exact(gateway,6));payload=exact(gateway,count)
                    self.assertEqual(control,1);self.assertEqual(payload[0],0xf4)
                    ecu,request=payload[1],payload[2:];seen.append((ecu,request))
                    reply=b'\x62'+request[1:]+b'PRIVATE RAW RECORD'
                    gateway.sendall(frame(0x12,b''))
                    self.assertEqual(exact(gateway,8),frame(0x13,b'\0\xf4'))
                    gateway.sendall(frame(1,bytes((ecu^1,0xf4))+reply))
                    gateway.sendall(frame(1,bytes((ecu,0xf4))+b'\x7f\x22\x78'))
                    gateway.sendall(frame(1,bytes((ecu,0xf4))+b'\x62\xf1\x90PRIVATE VIN'))
                    packet=frame(1,bytes((ecu,0xf4))+reply)
                    for chunk in (packet[:2],packet[2:8],packet[8:]): gateway.sendall(chunk)
                except Exception as error: errors.append(error)
        def connect(endpoint,*,timeout,source_address):
            self.assertEqual(endpoint,('169.254.10.3',6801))
            client,gateway=socket.socketpair()  # Local streams cannot reach a vehicle.
            thread=threading.Thread(target=respond,args=(gateway,),daemon=True);threads.append(thread);thread.start()
            return client
        with patch('collect_ecu_identity.socket.create_connection',side_effect=connect),patch('collect_ecu_identity.time.sleep'):
            rows=identity.collect('169.254.10.2','169.254.10.3')
        for thread in threads: thread.join(3);self.assertFalse(thread.is_alive())
        self.assertFalse(errors,errors)
        self.assertEqual(seen,list(identity.REQUESTS))
        public=identity.public_report(rows)
        self.assertTrue(all(f['record_status']=='positive_uninterpreted' for f in public['fields']))
        self.assertNotIn('PRIVATE',json.dumps(public))
        self.assertNotIn('50 52 49 56 41 54 45',json.dumps(public))


if __name__=='__main__': unittest.main()
