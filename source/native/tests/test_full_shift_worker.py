"""Observe the production full-screen Shift/Launch worker on loopback only."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time
import unittest

from test_transport import exact, frame
from udp_test_socket import telemetry_socket

BUILD=Path(__file__).resolve().parents[1]/'build'


class Gateway:
    def __init__(self, mode):
        self.mode=mode
        self.socket=socket.socket()
        self.socket.bind(('127.0.0.1',0));self.socket.listen()
        self.socket.settimeout(3)
        self.port=self.socket.getsockname()[1]
        self.requests=[];self.observed={};self.errors=[]
        self.thread=threading.Thread(target=self.serve)

    def serve(self):
        try:
            with self.socket.accept()[0] as connection:
                connection.settimeout(3)
                while True:
                    try: size,control=struct.unpack('>IH',exact(connection,6))
                    except EOFError: return
                    request=exact(connection,size)
                    assert control==1 and request in (b'\xf4\x12\x01\x0c',b'\xf4\x12\x01\x0d'),request
                    pid=request[-1];self.requests.append(pid)
                    value=1000+len(self.requests) if pid==0x0c else 80+self.requests.count(0x0d)
                    if self.mode=='zero': value=0
                    self.observed[pid,value]=time.monotonic_ns()//1_000_000
                    if self.mode=='delayed': time.sleep(.15)
                    if self.mode=='refused' and pid==0x0c: reply=b'\x7f\x01\x31'
                    elif self.mode=='malformed' and pid==0x0d: reply=b'\x41\x0d\x00\x01'
                    else:
                        reply=bytes((0x41,pid))+(value*4).to_bytes(2,'big') if pid==0x0c else bytes((0x41,pid,value))
                    connection.sendall(frame(1,b'\x12\xf4'+reply))
        except (ConnectionResetError,BrokenPipeError): pass
        except BaseException as error: self.errors.append(error)


class FullShiftWorker(unittest.TestCase):
    def observe(self,mode='normal',speed_only=False):
        gateway=Gateway(mode);gateway.thread.start()
        packets=[]
        try:
            with telemetry_socket() as receiver:
                receiver.settimeout(.1)
                command=[os.environ.get('FULL_SHIFT_WORKER',str(BUILD/'full-shift-deck')),
                    '--host','127.0.0.1','--port',str(gateway.port),'--transport','hsfz',
                    '--udp-port',str(receiver.getsockname()[1]),'--cycles','8']
                if speed_only: command.append('--speed-only')
                environment=dict(os.environ);environment.pop('ID6_FAKE_MOST',None)
                process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=environment)
                try:
                    deadline=time.monotonic()+8
                    while time.monotonic()<deadline:
                        try: data,peer=receiver.recvfrom(128)
                        except socket.timeout:
                            if process.poll() is not None: break
                            continue
                        self.assertEqual(peer,('127.0.0.1',42873 if speed_only else 42872))
                        self.assertEqual(len(data),24)
                        packets.append(struct.unpack('=QiiQ',data))
                    output,errors=process.communicate(timeout=2)
                    self.assertEqual(process.returncode,0,(output,errors))
                finally:
                    if process.poll() is None: process.terminate();process.communicate(timeout=3)
            gateway.thread.join(3)
            self.assertFalse(gateway.thread.is_alive())
            self.assertFalse(gateway.errors,gateway.errors)
            # --cycles counts scheduler passes, including the lease's wait/yield
            # passes. Slow replies exercise that real 500-ms lease boundary.
            if mode=='delayed': self.assertGreaterEqual(len(packets),2)
            else: self.assertEqual(len(packets),8)
            for port in (42861,42872,42873):
                with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:
                    probe.bind(('127.0.0.1',port))
            return gateway,packets
        finally:
            gateway.socket.close()
            gateway.thread.join(4)

    def test_delayed_replies_retain_independent_request_start_times(self):
        gateway,packets=self.observe('delayed')
        for sampled,rpm,kph,speed_sampled in packets:
            self.assertLessEqual(sampled,gateway.observed[0x0c,rpm])
            self.assertLess(sampled,speed_sampled)
            self.assertLessEqual(speed_sampled,gateway.observed[0x0d,kph])
        self.assertEqual(gateway.requests,[0x0c,0x0d]*len(packets))

    def test_cached_speed_keeps_its_age_while_rpm_advances(self):
        _,packets=self.observe()
        self.assertTrue(any(a[3]==b[3] and a[0]<b[0] for a,b in zip(packets,packets[1:])))
        for a,b in zip(packets,packets[1:]):
            self.assertLess(a[0],b[0])
            self.assertLessEqual(a[3],b[3])

    def test_supported_zero_has_positive_sample_times(self):
        _,packets=self.observe('zero')
        for sampled,rpm,kph,speed_sampled in packets:
            self.assertEqual((rpm,kph),(0,0));self.assertGreater(sampled,0);self.assertGreater(speed_sampled,0)

    def test_refused_rpm_and_malformed_speed_are_unavailable(self):
        _,packets=self.observe('refused')
        self.assertTrue(all(p[1]==-1 and p[2]>=81 for p in packets))
        _,packets=self.observe('malformed')
        self.assertTrue(all(p[1]>=0 and p[2]==-1 for p in packets))

    def test_launch_reads_only_speed_and_uses_its_activity_port(self):
        gateway,packets=self.observe('delayed',speed_only=True)
        self.assertEqual(gateway.requests,[0x0d]*len(packets))
        for sampled,rpm,kph,speed_sampled in packets:
            self.assertEqual(rpm,-1);self.assertGreaterEqual(kph,81);self.assertEqual(sampled,speed_sampled)
            self.assertLessEqual(sampled,gateway.observed[0x0d,kph])


if __name__=='__main__': unittest.main()
