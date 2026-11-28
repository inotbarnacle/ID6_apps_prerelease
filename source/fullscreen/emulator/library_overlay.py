"""Opt-in bridge for the local ID6 Library test profile."""
import ctypes
from pathlib import Path
import socket
import struct
import threading
import time

class LibraryOverlay:
    def __init__(self, socket_path, renderer, can=None, data_dir=None):
        self._client = socket.socket(socket.AF_UNIX)
        self._client.connect(socket_path)
        self._renderer = ctypes.CDLL(renderer)
        self._renderer.overlay_state_size.restype = ctypes.c_uint
        self._renderer.overlay_state_magic.restype = ctypes.c_uint
        self._magic = self._renderer.overlay_state_magic()
        self._renderer.overlay_animation_size.restype = ctypes.c_uint
        self._renderer.overlay_hidden_page.restype = ctypes.c_uint
        self._renderer.overlay_frame.argtypes = (
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_uint64,
        )
        self._animation = ctypes.create_string_buffer(self._renderer.overlay_animation_size())
        self._hidden_page = self._renderer.overlay_hidden_page()
        self._renderer.overlay_render.argtypes = (
            ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
        )
        self._renderer.overlay_render.restype = None
        self._size = self._renderer.overlay_state_size()
        self._frame = ctypes.create_string_buffer(self._size)
        self._frame_tick = -1
        self._frame_packet = None
        self._rendered = None
        self._lock = threading.Lock()
        self._packet = None
        self._last = 0.0
        self._key = None
        self._sequence = 0
        self._image = None
        self._config = None if data_dir is None else (
            Path(data_dir)/"library-boost.prefs", Path(data_dir)/"library-boost.signals",
            Path(data_dir)/"library-boost.defaults"
        )
        threading.Thread(target=self._receive, daemon=True).start()
        self._stop = threading.Event()
        if can is not None:
            threading.Thread(target=self._send_telemetry, args=(can,), daemon=True).start()

    def _send_telemetry(self, can):
        fields = ("engine_rpm", "vehicle_speed", "coolant_temp", "accelerator_pos",
                  "gear_position", "steering_angle", "terminal_state")
        sent_config = False
        while not self._stop.wait(0.02):
            with self._lock:
                packet = self._packet
                active = packet is not None and time.monotonic()-self._last < 1
            if active and not sent_config and self._config:
                for kind,path in enumerate(self._config):
                    try:
                        with path.open('rb') as source:
                            data=source.read(26)
                        if not self._valid_config(kind,data):
                            continue
                        self._client.sendall(f"ID6CONF {kind} {data.hex()}\n".encode("ascii"))
                    except FileNotFoundError:
                        pass
                    except OSError:
                        return
                sent_config = True
            if not active or struct.unpack_from("<I", packet, 4)[0] not in (0, 1, 4, 6, 7):
                continue
            values = [can.get_signal(name) for name in fields]
            values[3] *= 10
            values[5] *= 100
            line = "ID6CAN " + " ".join(str(round(value)) for value in values) + "\n"
            try:
                self._client.sendall(line.encode("ascii"))
            except OSError:
                return


    def _receive(self):
        try:
            with self._client.makefile("rb") as serial:
                for line in iter(lambda: serial.readline(2048), b""):
                    if line.startswith(b"ID6CONF "):
                        self._save_config(line)
                        continue
                    if not line.startswith(b"ID6HUD "):
                        continue
                    try:
                        packet = bytes.fromhex(line[7:].decode("ascii"))
                        if len(packet) != self._size:
                            continue
                        magic, page = struct.unpack_from("<II", packet)
                        if magic != self._magic:
                            continue
                    except ValueError:
                        continue
                    now = time.monotonic()
                    with self._lock:
                        self._last = now
                        if page < self._hidden_page or 40 <= page <= 43:
                            self._packet = packet
                        else:
                            owner = struct.unpack_from("<I", packet, 8)[0]
                            active_page = struct.unpack_from("<I", self._packet, 4)[0] if self._packet else None
                            same_app = active_page == owner or (owner == 10 and active_page in (11, 12, 13))
                            if same_app:
                                self._packet = None
                                ctypes.memset(self._animation, 0, len(self._animation))
                                self._frame_packet = None
        except OSError:
            pass

    def _save_config(self, line):
        if not self._config:
            return
        try:
            _, kind_text, encoded = line.decode("ascii").split()
            kind = int(kind_text)
            if kind not in (0, 1, 2):
                return
            data = bytes.fromhex(encoded)
            if not self._valid_config(kind,data):
                return
            path = self._config[kind]
            temporary = path.with_suffix(path.suffix + ".tmp")
            temporary.write_bytes(data)
            temporary.replace(path)
        except (OSError, ValueError):
            return

    @staticmethod
    def _valid_config(kind,data):
        if kind==0:
            if len(data)==4: return data[:2]==b'G\x01' and data[2]<9 and data[3]<=1
            choices=(3,2,2,2,2,10,10,13) # legacy beta oil floors are clamped by the loader
            return len(data)==11 and data[:2]==b'G\x02' and data[2]<9 and all(
                value<limit for value,limit in zip(data[3:],choices))
        if kind==2: return len(data)==3 and data[:2]==b'D\x01' and data[2]<=1
        if kind!=1 or len(data)<3 or data[0]!=ord('B') or not 1<=data[1]<=6 or data[2]>1:
            return False
        stride=11 if data[1]==1 else 9
        limit=(0,31,31,39,47,55,58)[data[1]]
        return len(data)==3+2*stride and all(data[3+profile*stride+i]<limit
            for profile in range(2) for i in range(9))

    def close(self):
        self._stop.set()
        try:
            self._client.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self._client.close()

    def _visible(self):
        now = time.monotonic()
        if self._packet is None or now-self._last > 1:
            return None
        tick = int(now*60)
        if self._frame_packet != self._packet or self._frame_tick != tick:
            self._renderer.overlay_frame(self._animation, self._packet, self._frame, tick*1000//60)
            self._frame_tick = tick
            self._frame_packet = self._packet
        return self._frame.raw

    def _seq(self, base, packet):
        key = (0, packet) if packet is not None else (base, None)
        if key != self._key:
            self._key = key
            self._sequence += 1
        return self._sequence

    def sequence(self, base):
        with self._lock:
            return self._seq(base, self._visible())

    def compose(self, base, width, height, rgb):
        with self._lock:
            packet = self._visible()
            seq = self._seq(base, packet)
            if packet is None:
                return seq, width, height, rgb
            if self._rendered != packet or self._image is None or self._image[:2] != (width,height):
                buffer = ctypes.create_string_buffer(width*height*4)
                output = ctypes.create_string_buffer(width*height*3)
                self._renderer.overlay_render(packet, buffer, output, width, height)
                self._image = width, height, output.raw
                self._rendered = packet
            return seq, width, height, self._image[2]
