"""Real DPF/Boost workers share only a synthetic loopback HSFZ gateway.

Exercise the brief overlap possible during an app handoff and OS cleanup after
normal/abnormal exit. This does not establish vehicle or external-tester safety.
"""
import errno
import os
from pathlib import Path
import select
import socket
import struct
import subprocess
import threading
import time
import unittest

from test_dpf_transport import PAYLOADS, exact, frame, parse
from udp_test_socket import telemetry_socket

BUILD = Path(__file__).resolve().parents[1]/'build'


class SharedGateway:
    def __init__(self,regen=False,reply_barrier=None):
        self.regen=regen
        self.listener=socket.socket()
        self.listener.bind(('127.0.0.1',0));self.listener.listen(8);self.listener.settimeout(.1)
        self.port=self.listener.getsockname()[1]
        self.stopping=threading.Event();self.lock=threading.Lock()
        self.active=self.maximum=0
        self.reply_barrier=reply_barrier
        self.requests,self.errors,self.clients=[],[],[]
        self.thread=threading.Thread(target=self.serve,daemon=True)

    def __enter__(self): self.thread.start();return self

    def __exit__(self,*args):
        self.stopping.set();self.thread.join(3);self.listener.close()
        for thread in self.clients: thread.join(3)
        assert not self.thread.is_alive() and not any(t.is_alive() for t in self.clients)
        assert not self.errors,self.errors

    def serve(self):
        while not self.stopping.is_set():
            try: connection,_=self.listener.accept()
            except socket.timeout: continue
            thread=threading.Thread(target=self.respond,args=(connection,),daemon=True)
            self.clients.append(thread);thread.start()

    def respond(self,connection):
        with connection:
            connection.settimeout(2)
            while not self.stopping.is_set():
                try:
                    size,control=struct.unpack('>IH',exact(connection,6))
                    request=exact(connection,size)
                except socket.timeout: continue
                except (EOFError,ConnectionResetError): break
                try:
                    assert control==1 and request[:3]==b'\xf4\x12\x01' and len(request)==4,request
                    pid=request[3]
                    assert pid in (0,0x20,0x40,0x60,0x80,5,0x7a,0x7b,0x7c,0x8b),request
                    assert self.regen or pid not in (0x80,0x8b),request
                    if pid in (0,0x20,0x40,0x60,0x80):
                        mask={0:0x08000001,0x20:1,0x40:1,0x60:0x71 if self.regen else 0x70,0x80:0x00200000}[pid]
                        answer=bytes((0x41,pid))+mask.to_bytes(4,'big')
                    else: answer=bytes((0x41,pid))+PAYLOADS[pid]
                    with self.lock:
                        self.active+=1;self.maximum=max(self.maximum,self.active)
                        self.requests.append((time.monotonic(),pid))
                    if self.reply_barrier is not None: self.reply_barrier.wait(2)
                    # Keep a reply pending so independent connections can overlap.
                    time.sleep(.03)
                    # Finish the reply and its observation together. A waiter
                    # may send immediately after receiving the completed reply.
                    with self.lock:
                        try: connection.sendall(frame(1,b'\x12\xf4'+answer))
                        finally: self.active-=1
                except (BrokenPipeError,ConnectionResetError): break
                except Exception as error:
                    self.errors.append(error);break


def busy(port):
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:
        try: probe.bind(('127.0.0.1',port))
        except OSError as error:
            if error.errno==errno.EADDRINUSE: return True
            raise
    return False


class DpfCoexistenceTest(unittest.TestCase):
    def test_gateway_detects_uncoordinated_requests(self):
        # Negative control: the observer must detect genuine overlap rather
        # than accidentally serializing every request with its own lock.
        with SharedGateway(reply_barrier=threading.Barrier(2)) as gateway:
            with socket.create_connection(('127.0.0.1',gateway.port)) as left, \
                    socket.create_connection(('127.0.0.1',gateway.port)) as right:
                for client in (left,right):
                    client.settimeout(3);client.sendall(frame(1,b'\xf4\x12\x01\x05'))
                for client in (left,right):
                    size,control=struct.unpack('>IH',exact(client,6))
                    self.assertEqual(control,1)
                    self.assertEqual(exact(client,size),b'\x12\xf4\x41\x05'+PAYLOADS[5])
                self.assertEqual(gateway.maximum,2)

    def handoff(self,abnormal,regen=False):
        with SharedGateway(regen) as gateway,telemetry_socket() as dpf_udp,telemetry_socket() as boost_udp:
            environment=dict(os.environ);environment.pop('ID6_FAKE_MOST',None)
            common=['--host','127.0.0.1','--port',str(gateway.port),'--transport','hsfz','--cycles','10000']
            dpf=boost=None
            packets={'dpf':False,'boost':False}
            if regen: packets['regen']=False
            def running(process):
                if process.poll() is not None:
                    self.fail('worker exited early: '+process.communicate()[1].decode('utf-8','replace'))
            def receive(receiver):
                data=receiver.recv(2048)
                if receiver is dpf_udp:
                    sample=parse(data)
                    self.assertEqual(sample['pid'],dpf.pid)
                    self.assertEqual(sample['token'],42)
                    if sample['valid'] & (1<<10):
                        self.assertEqual(sample['values'][10],82)
                        packets['dpf']=True
                    if regen and sample['valid'] & (3<<11)==3<<11:
                        self.assertEqual(sample['values'][11:],[1,1])
                        packets['regen']=True
                else:
                    fields=data.decode('ascii').split()
                    self.assertEqual(fields[0],'NBG7')
                    if int(fields[3]) & (1<<7):
                        self.assertEqual(len(fields),5+2*31)
                        self.assertEqual(fields[2],'0')  # live, not demo
                        self.assertGreater(int(fields[4]),0)
                        self.assertEqual(float(fields[5+2*7]),82)
                        packets['boost']=True
            try:
                command=[str(BUILD/'dpfdeck-collector'),*common,'--token','42',
                         '--udp-port',str(dpf_udp.getsockname()[1])]
                if regen: command.append('--regen-status')
                dpf=subprocess.Popen(command,stderr=subprocess.PIPE,env=environment)
                deadline=time.monotonic()+6
                while not packets['dpf'] and time.monotonic()<deadline:
                    running(dpf)
                    for ready in select.select([dpf_udp],[],[],.1)[0]: receive(ready)
                self.assertTrue(packets['dpf'],'DPF worker did not establish its initial measurement')
                # Match BoostDeck::startVisibleCollector: HSFZ arbitration is
                # explicitly enabled by the production plugin's --session flag.
                boost=subprocess.Popen([str(BUILD/'boost-deck'),*common,'--session','--diesel','--no-trans','--signals','128',
                                        '--udp-port',str(boost_udp.getsockname()[1])],stderr=subprocess.PIPE,env=environment)
                packets['dpf']=False
                deadline=time.monotonic()+8
                while not all(packets.values()) and time.monotonic()<deadline:
                    running(dpf);running(boost)
                    for ready in select.select([dpf_udp,boost_udp],[],[],.1)[0]: receive(ready)
                self.assertTrue(all(packets.values()),'both workers must make progress during the overlap')
                self.assertIsNone(dpf.poll());self.assertIsNone(boost.poll())
                self.assertTrue(busy(42868));self.assertTrue(busy(42871))
                self.assertEqual(gateway.maximum,1,'diagnostic requests overlapped instead of sharing the lease')
                if regen:
                    self.assertTrue(all(any(pid==wanted for _,pid in gateway.requests) for wanted in (0x80,0x8b)))
                if abnormal: dpf.kill()
                else: dpf.terminate()
                error=dpf.communicate(timeout=3)[1]
                self.assertEqual(dpf.returncode,-9 if abnormal else 0,error)
                stopped=time.monotonic()
                self.assertFalse(busy(42868));self.assertTrue(busy(42871))
                deadline=stopped+5
                while not any(at>=stopped and pid==5 for at,pid in gateway.requests) and time.monotonic()<deadline:
                    for ready in select.select([boost_udp],[],[],.1)[0]: receive(ready)
                self.assertTrue(any(at>=stopped and pid==5 for at,pid in gateway.requests),
                                'surviving worker did not read again after the handoff')
            finally:
                for process in (dpf,boost):
                    if process is not None:
                        if process.poll() is None: process.terminate()
                        try: process.communicate(timeout=3)
                        except subprocess.TimeoutExpired: process.kill();process.communicate()
            for port in (42861,42868,42869,42871,42875):
                self.assertFalse(busy(port),'worker resource remained owned after exit: '+str(port))

    def test_handoff_after_normal_dpf_exit(self): self.handoff(False)
    def test_handoff_after_abnormal_dpf_exit(self): self.handoff(True)
    def test_optional_regeneration_reads_share_the_lease_and_handoff(self): self.handoff(False,True)


if __name__=='__main__': unittest.main()
