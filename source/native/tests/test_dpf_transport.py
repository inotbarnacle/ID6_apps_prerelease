"""Production DPF worker against bounded loopback HSFZ and fake MOST peers."""
import contextlib
import errno
import os
from pathlib import Path
import socket
import struct
import subprocess
import select
import sys
import threading
import time
import unittest

from udp_test_socket import telemetry_socket

BIN = str(Path(__file__).resolve().parents[1] / 'build/dpfdeck-collector')
PARENT_FIXTURE = str(Path(__file__).resolve().parents[1] / 'build/dpf-parent-exit')
ALL = (1 << 11)-1
REGEN = 3 << 11
PAYLOADS = {5:bytes.fromhex('7a'), 0x7a:bytes.fromhex('07fff600002710'),
            0x7b:bytes.fromhex('058000ffffffff'), 0x7c:bytes.fromhex('0f0190019147e003e8'),
            0x8b:bytes.fromhex('03030000000000')}


def regen_reply(pid,count):
    return bytes.fromhex('416000000071') if pid==0x60 else None


def exact(connection,size):
    data = b''
    while len(data)<size:
        part = connection.recv(size-len(data))
        if not part:
            raise EOFError
        data += part
    return data


def frame(control,body):
    return struct.pack('>IH',len(body),control)+body


def parse(data):
    assert data.endswith(b'\n') and len(data)<512
    fields = data.decode('ascii').split()
    assert (fields[0],len(fields)) in (('DPF1',30),('DPF2',34))
    return dict(seq=int(fields[1]),pid=int(fields[2]),token=int(fields[3]),sent=int(fields[4]),
                known=int(fields[5]),supported=int(fields[6]),valid=int(fields[7]),
                values=[float(v) for v in fields[8::2]],ages=[int(v) for v in fields[9::2]])


class Gateway:
    def __init__(self,reply=None):
        self.server = socket.socket(socket.AF_INET,socket.SOCK_STREAM)
        self.server.bind(('127.0.0.1',0))
        self.server.listen(2)
        self.server.settimeout(.1)
        self.port = self.server.getsockname()[1]
        self.stop = threading.Event()
        self.seen,self.errors,self.counts = [],[],{}
        self.reply = reply
        self.thread = threading.Thread(target=self.run,daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self,*args):
        self.stop.set()
        self.thread.join(3)
        self.server.close()
        assert not self.thread.is_alive()
        if self.errors:
            raise self.errors[0]

    def run(self):
        while not self.stop.is_set():
            try:
                connection,_ = self.server.accept()
            except socket.timeout:
                continue
            with connection:
                connection.settimeout(2)
                while not self.stop.is_set():
                    try:
                        size,control = struct.unpack('>IH',exact(connection,6))
                        request = exact(connection,size)
                    except (EOFError,ConnectionResetError,socket.timeout):
                        break
                    try:
                        assert control==1 and request[:3]==b'\xf4\x12\x01' and len(request)==4,request
                        pid = request[3]
                        assert pid in (0,0x20,0x40,0x60,0x80,5,0x7a,0x7b,0x7c,0x8b),request
                        self.seen.append((time.monotonic(),pid))
                        self.counts[pid] = self.counts.get(pid,0)+1
                        answer = self.reply(pid,self.counts[pid]) if self.reply else None
                        if answer=='drop':
                            break
                        if isinstance(answer,tuple):
                            delay,answer = answer
                            time.sleep(delay)
                        if answer is None:
                            answer = bytes((0x41,pid))+(bytes.fromhex('08000001') if pid==0 else
                                      bytes.fromhex('00000001') if pid in (0x20,0x40) else
                                      bytes.fromhex('00000070') if pid==0x60 else
                                      bytes.fromhex('00200000') if pid==0x80 else PAYLOADS[pid])
                        # Correct-looking replies from another ECU, and pending replies,
                        # cannot become a measurement. Fragment the matching response.
                        connection.sendall(frame(1,b'\x18\xf4'+answer))
                        connection.sendall(frame(1,b'\x12\xf4\x7f\x01\x78'))
                        packet = frame(1,b'\x12\xf4'+answer)
                        for part in (packet[:2],packet[2:7],packet[7:]):
                            connection.sendall(part)
                    except (BrokenPipeError,ConnectionResetError):
                        break
                    except Exception as error:
                        self.errors.append(error)
                        break


class DpfTransportTest(unittest.TestCase):
    def run_worker(self,cycles=12,reply=None,most=False,observe=None,stop_when=None,regen=False):
        environment = dict(os.environ)
        environment.pop('ID6_FAKE_MOST',None)
        if most:
            environment['ID6_FAKE_MOST']='dpf-regen' if regen else 'dpf'
        with contextlib.ExitStack() as stack:
            udp = stack.enter_context(telemetry_socket())
            udp.settimeout(8)
            gateway = None if most else stack.enter_context(Gateway(reply))
            command = [BIN,'--token','42','--cycles',str(cycles),'--udp-port',str(udp.getsockname()[1])]
            if regen:
                command.append('--regen-status')
            command += ['--transport','most'] if most else ['--host','127.0.0.1','--port',str(gateway.port),'--transport','hsfz']
            process = subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=environment)
            packets = []
            try:
                for _ in range(cycles):
                    data,peer = udp.recvfrom(513)
                    self.assertEqual(peer,('127.0.0.1',42868))
                    packet = parse(data)
                    self.assertEqual(packet['pid'],process.pid)
                    self.assertEqual(packet['token'],42)
                    self.assertGreater(packet['sent'],0)
                    self.assertEqual(packet['valid'] & ~packet['supported'],0)
                    self.assertEqual(packet['supported'] & ~packet['known'],0)
                    packets.append(packet)
                    if observe:
                        observe(packet,process)
                    if stop_when and stop_when(packet):
                        process.terminate()
                        break
                _,errors = process.communicate(timeout=8)
                self.assertEqual(process.returncode,0,errors)
                self.assertEqual([p['seq'] for p in packets],list(range(1,len(packets)+1)))
                return packets,gateway.seen[:] if gateway else [],errors
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=8)

    def test_capabilities_signed_pressure_independent_fields_and_conservative_age(self):
        packets,requests,_ = self.run_worker()
        final = packets[-1]
        self.assertEqual(final['known'],ALL)
        self.assertEqual(final['supported'],ALL ^ (1 << 4))
        self.assertEqual(final['valid'],final['supported'])
        self.assertEqual(final['values'],[-.1,0,100,-327.68,0,655.35,0,.1,1800,60,82,0,0])
        self.assertTrue(any(final['ages']))
        self.assertEqual([pid for _,pid in requests[:4]],[0,0x20,0x40,0x60])
        self.assertTrue(all(b-a>=.20 for (a,_),(b,_) in zip(requests,requests[1:])))

    def test_regeneration_request_set_is_absent_by_default_even_when_advertised(self):
        packets,requests,_ = self.run_worker(reply=regen_reply)
        self.assertFalse(any(pid in (0x80,0x8b) for _,pid in requests))
        self.assertEqual(packets[-1]['known'],ALL)
        self.assertEqual(packets[-1]['values'][11:],[0,0])

    def test_opt_in_reads_five_bitmaps_and_only_advertised_regeneration_status(self):
        packets,requests,_ = self.run_worker(cycles=24,regen=True,reply=regen_reply)
        final = packets[-1]
        self.assertEqual([pid for _,pid in requests if pid%0x20==0],[0,0x20,0x40,0x60,0x80])
        self.assertEqual(final['known'],ALL | REGEN)
        self.assertEqual(final['valid'],(ALL | REGEN) ^ (1 << 4))
        self.assertEqual(final['values'][11:],[1,1])
        self.assertTrue(all(b-a>=.20 for (a,_),(b,_) in zip(requests,requests[1:])))

    def test_opt_in_stops_at_missing_continuation_or_unadvertised_status(self):
        for reply,expected_pages in ((None,[0,0x20,0x40,0x60]),
                                     (lambda pid,n:bytes.fromhex('418000000000') if pid==0x80 else regen_reply(pid,n),
                                      [0,0x20,0x40,0x60,0x80])):
            packets,requests,_ = self.run_worker(cycles=16,regen=True,reply=reply)
            self.assertEqual([pid for _,pid in requests if pid%0x20==0],expected_pages)
            self.assertFalse(any(pid==0x8b for _,pid in requests))
            self.assertEqual(packets[-1]['known'] & REGEN,REGEN)
            self.assertEqual(packets[-1]['supported'] & REGEN,0)

    def test_invalid_extension_bitmap_stays_unknown_and_backs_off(self):
        for bad in (bytes.fromhex('4180002000'),bytes.fromhex('7f0131')):
            packets,requests,_ = self.run_worker(cycles=20,regen=True,reply=lambda pid,n:
                                                bad if pid==0x80 else regen_reply(pid,n))
            self.assertEqual(sum(pid==0x80 for _,pid in requests),1)
            self.assertFalse(any(pid==0x8b for _,pid in requests))
            self.assertEqual(packets[-1]['known'] & REGEN,0)
            self.assertTrue(packets[-1]['valid'] & (1 << 6))

    def test_progress_and_type_support_are_independent_and_false_is_valid(self):
        for flags,status,mask,values in ((1,3,1,[1,0]),(2,3,2,[0,1]),(3,2,3,[0,1])):
            response=bytes((0x41,0x8b,flags,status,0,0,0,0,0))
            packets,_,_ = self.run_worker(cycles=12,regen=True,reply=lambda pid,n:
                                         response if pid==0x8b else regen_reply(pid,n))
            self.assertEqual(packets[-1]['known'] & REGEN,REGEN)
            self.assertEqual(packets[-1]['valid'] & REGEN,mask << 11)
            self.assertEqual(packets[-1]['values'][11:],values)

    def test_current_support_flags_clear_old_progress_without_using_type(self):
        packets,_,_ = self.run_worker(cycles=24,regen=True,reply=lambda pid,n:
                                     bytes.fromhex('418b02030000000000') if pid==0x8b and n>=2 else regen_reply(pid,n))
        self.assertTrue(any(p['valid'] & (1 << 11) for p in packets))
        self.assertEqual(packets[-1]['supported'] & REGEN,1 << 12)
        self.assertEqual(packets[-1]['valid'] & REGEN,1 << 12)
        self.assertEqual(packets[-1]['values'][11:],[0,1])

    def test_refused_or_extra_length_status_clears_only_regeneration_with_backoff(self):
        for bad in (bytes.fromhex('7f0131'),bytes.fromhex('418b0303000000000000')):
            packets,requests,_ = self.run_worker(cycles=28,regen=True,reply=lambda pid,n:
                                                bad if pid==0x8b and n>=2 else regen_reply(pid,n))
            self.assertTrue(any(p['valid'] & REGEN==REGEN for p in packets))
            self.assertEqual(sum(pid==0x8b for _,pid in requests),2)
            self.assertEqual(packets[-1]['valid'] & REGEN,0)
            self.assertEqual(packets[-1]['supported'] & REGEN,REGEN)
            self.assertTrue(packets[-1]['valid'] & (1 << 6))

    def test_delayed_status_uses_actual_request_period_and_conservative_age(self):
        packets,requests,_ = self.run_worker(cycles=28,regen=True,reply=lambda pid,n:
                                            (.35,bytes.fromhex('418b03030000000000')) if pid==0x8b else regen_reply(pid,n))
        stamps=[when for when,pid in requests if pid==0x8b]
        self.assertGreaterEqual(len(stamps),2)
        self.assertTrue(all(b-a>=1.98 for a,b in zip(stamps,stamps[1:])))
        self.assertTrue(all(p['ages'][11]>=300 for p in packets if p['valid'] & (1 << 11)))

    def test_most_optional_status_releases_its_diagnostic_resources(self):
        packets,_,errors = self.run_worker(cycles=100,regen=True,most=True,
                                          stop_when=lambda p:p['valid'] & REGEN==REGEN)
        self.assertEqual(packets[-1]['values'][11:],[1,1])
        self.assertIn('unregister_target=1 unregister_client=1 free=1',errors)
        for port in (42861,42868,42869):
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:
                probe.bind(('127.0.0.1',port))

    def test_duplicate_optional_status_flag_exits_before_transport(self):
        process=subprocess.run([BIN,'--token','42','--cycles','1','--transport','most',
                                '--regen-status','--regen-status'],capture_output=True,text=True,timeout=2)
        self.assertEqual(process.returncode,2)
        self.assertNotIn('FAKE-MOST',process.stderr)

    def test_unsupported_chain_never_polls_dpf_or_later_bitmap_pages(self):
        packets,requests,_ = self.run_worker(reply=lambda pid,n:bytes.fromhex('410008000000') if pid==0 else None)
        self.assertEqual({pid for _,pid in requests},{0,5})
        self.assertEqual(packets[-1]['known'],ALL)
        self.assertEqual(packets[-1]['supported'],1 << 10)
        self.assertEqual(packets[-1]['valid'],1 << 10)

    def test_bad_or_refused_capability_layout_remains_unknown_with_backoff(self):
        for bad in (bytes.fromhex('4160000070'),bytes.fromhex('7f0131')):
            packets,requests,_ = self.run_worker(reply=lambda pid,n:bad if pid==0x60 else None)
            self.assertEqual(sum(pid==0x60 for _,pid in requests),1)
            self.assertFalse(any(pid in (0x7a,0x7b,0x7c) for _,pid in requests))
            self.assertEqual(packets[-1]['known'],1 << 10)
            self.assertEqual(packets[-1]['valid'],1 << 10)

    def test_current_missing_field_clears_its_value_without_inlet_outlet_fallback(self):
        packets,_,_ = self.run_worker(cycles=20,reply=lambda pid,n:
                                    bytes.fromhex('417a06ffff27104e20') if pid==0x7a and n>=2 else None)
        self.assertTrue(any(p['valid'] & 1 for p in packets))
        self.assertEqual(packets[-1]['known'] & 1,1)
        self.assertEqual(packets[-1]['supported'] & 1,0)
        self.assertEqual(packets[-1]['valid'] & 1,0)
        self.assertEqual(packets[-1]['values'][0],0)
        self.assertEqual(packets[-1]['values'][1:3],[100,200])

    def test_refused_or_extra_length_read_clears_only_its_group_and_backs_off(self):
        for bad in (bytes.fromhex('7f0131'),bytes.fromhex('417a07fff60000271000')):
            packets,requests,_ = self.run_worker(cycles=24,reply=lambda pid,n:bad if pid==0x7a and n>=2 else None)
            self.assertTrue(any(p['valid'] & 1 for p in packets))
            self.assertEqual(sum(pid==0x7a for _,pid in requests),2)
            self.assertEqual(packets[-1]['valid'] & 7,0)
            self.assertEqual(packets[-1]['supported'] & 7,7)
            self.assertEqual(packets[-1]['valid'] & (1 << 6),1 << 6)

    def test_two_second_period_is_measured_at_actual_requests(self):
        _,requests,_ = self.run_worker(cycles=28)
        for pid in (5,0x7a,0x7b,0x7c):
            stamps = [when for when,actual in requests if actual==pid]
            self.assertGreaterEqual(len(stamps),2)
            self.assertTrue(all(b-a>=1.98 for a,b in zip(stamps,stamps[1:])))

    def test_lost_link_reconnects_and_capability_cache_stays_endpoint_local(self):
        packets,requests,_ = self.run_worker(cycles=20,reply=lambda pid,n:'drop' if pid==0x7a and n==1 else None)
        self.assertEqual([pid for _,pid in requests[:4]],[0,0x20,0x40,0x60])
        self.assertTrue(any(p['valid']==0 for p in packets))
        self.assertEqual(packets[-1]['valid'] & (1 << 6),1 << 6)
        self.assertEqual(sum(pid==0 for _,pid in requests),1)

    def test_second_worker_cannot_take_over_the_activity_port(self):
        checked = []
        def observe(packet,process):
            if checked:
                return
            other = subprocess.run([BIN,'--token','43','--cycles','1','--transport','most'],
                                   capture_output=True,text=True,timeout=3)
            self.assertEqual(other.returncode,1,other.stderr)
            self.assertNotIn('FAKE-MOST',other.stderr)
            checked.append(True)
        self.run_worker(cycles=100,observe=observe,stop_when=lambda p:p['seq']>=4)
        self.assertTrue(checked)

    def test_missing_invalid_or_duplicate_token_exits_before_transport(self):
        for extra in ([],['--token','0'],['--token','-1'],['--token','+1'],
                      ['--token','18446744073709551616'],['--token','1','--token','2']):
            process = subprocess.run([BIN,'--cycles','1','--transport','most',*extra],
                                     capture_output=True,text=True,timeout=2)
            self.assertEqual(process.returncode,2)
            self.assertNotIn('FAKE-MOST',process.stderr)

    def test_most_owns_dashboard_marker_and_unregisters_on_exit(self):
        checked = []
        def observe(packet,process):
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:
                with self.assertRaises(OSError) as raised:
                    probe.bind(('127.0.0.1',42869))
                self.assertEqual(raised.exception.errno,errno.EADDRINUSE)
            checked.append(True)
        # Keep the child alive while probing its marker. A naturally exiting
        # child's final datagram can be dequeued after teardown has released it.
        packets,_,errors = self.run_worker(cycles=100,most=True,observe=observe,
                                         stop_when=lambda p:p['seq']>=12)
        self.assertEqual(packets[-1]['valid'],ALL ^ (1 << 4))
        self.assertIn('transport: MOST',errors)
        self.assertIn('unregister_target=1 unregister_client=1 free=1',errors)
        self.assertTrue(checked)
        for port in (42861,42868,42869):
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:
                probe.bind(('127.0.0.1',port))

    def test_existing_most_marker_prevents_a_second_client(self):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as owner:
            owner.bind(('127.0.0.1',42869))
            packets,_,errors = self.run_worker(cycles=4,most=True)
            self.assertTrue(all(p['known']==0 for p in packets))
            self.assertNotIn('FAKE-MOST bind',errors)


    def test_hud_arbitration_is_requested_and_released_before_most_opens(self):
        released = []
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as owner:
            owner.bind(('127.0.0.1',42874))
            owner.settimeout(2)
            def observe(packet,process):
                if released:
                    return
                self.assertEqual(packet['known'],0)
                self.assertEqual(owner.recvfrom(1)[0],b'\1')
                owner.close()
                released.append(True)
            packets,_,errors = self.run_worker(cycles=14,most=True,observe=observe)
        self.assertTrue(released)
        self.assertEqual(packets[-1]['valid'],ALL ^ (1 << 4))
        self.assertIn('transport: MOST',errors)
        self.assertIn('unregister_target=1 unregister_client=1 free=1',errors)

    def test_sigterm_releases_hsfz_and_most_resources(self):
        for most in (False,True):
            packets,_,errors = self.run_worker(cycles=100,most=most,
                                               stop_when=lambda p:bool(p['valid'] & (1 << 6)))
            self.assertLess(len(packets),100)
            self.assertTrue(packets[-1]['valid'] & (1 << 6))
            if most:
                self.assertIn('unregister_target=1 unregister_client=1 free=1',errors)
            for port in (42861,42868,42869):
                with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:
                    probe.bind(('127.0.0.1',port))

    @unittest.skipUnless(sys.platform.startswith('linux'),'isolated guardian uses Linux subreaper support')
    def test_parent_exit_stops_the_real_worker_without_signalling_it(self):
        with telemetry_socket() as udp,Gateway() as gateway:
            udp.settimeout(4)
            environment=dict(os.environ);environment.pop('ID6_FAKE_MOST',None)
            guardian=subprocess.Popen([PARENT_FIXTURE,BIN,str(gateway.port),str(udp.getsockname()[1])],
                                      stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                                      text=True,env=environment)
            try:
                self.assertTrue(select.select([guardian.stdout],[],[],4)[0])
                worker=int(guardian.stdout.readline().strip())
                for _ in range(16):
                    packet=parse(udp.recv(513))
                    self.assertEqual(packet['pid'],worker)
                    if packet['valid'] & (1 << 6):
                        break
                self.assertTrue(packet['valid'] & (1 << 6))
                output,errors=guardian.communicate('x\n',timeout=8)
                self.assertEqual(guardian.returncode,0,errors)
                self.assertIn('exited after parent death',output)
            finally:
                if guardian.poll() is None:
                    guardian.kill()
                guardian.communicate(timeout=8)
            for port in (42861,42868,42869):
                with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:
                    probe.bind(('127.0.0.1',port))


if __name__=='__main__':
    unittest.main()
