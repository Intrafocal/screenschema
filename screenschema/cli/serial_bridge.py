"""
DeviceBridge — host-side serial bridge for ScreenSchema devices (Phase 3).

Protocol: binary framing over UART at 115200 baud (configurable).

Frame format:
  [0xAA][0x55][CMD][ID_LEN][ID...][VAL_TYPE][VAL_LEN_H][VAL_LEN_L][VAL...][XOR]

Usage:
    bridge = DeviceBridge("/dev/ttyACM0")
    bridge.connect()
    bridge.set("greeting_label", "Hello from host!")
    bridge.on("tap_btn", "click", lambda e: print("tapped!", e))
    bridge.run_forever()          # blocks; Ctrl-C to exit
"""

import struct
import threading
import time
from typing import Callable, Any

try:
    import serial
except ImportError:
    serial = None  # type: ignore

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

MAGIC_HI = 0xAA
MAGIC_LO = 0x55

CMD_SET        = 0x01
CMD_GET        = 0x02
CMD_SHOW       = 0x03
CMD_HIDE       = 0x04
CMD_PING       = 0x05
CMD_LIST_ADD   = 0x06
CMD_LIST_CLEAR = 0x07
CMD_GET_RESP   = 0x81
CMD_EVENT      = 0x82
CMD_PONG       = 0x85

VAL_NONE   = 0
VAL_INT32  = 1
VAL_FLOAT  = 2
VAL_STRING = 3
VAL_BOOL   = 4

EVENT_CLICK        = 0
EVENT_LONG_PRESS   = 1
EVENT_VALUE_CHANGED = 2
EVENT_RELEASED     = 3
EVENT_SUBMIT       = 4
EVENT_SELECTED     = 5

_EVENT_NAMES = {
    EVENT_CLICK:         "click",
    EVENT_LONG_PRESS:    "long_press",
    EVENT_VALUE_CHANGED: "change",
    EVENT_RELEASED:      "release",
    EVENT_SUBMIT:        "submit",
    EVENT_SELECTED:      "select",
}


# ---------------------------------------------------------------------------
# DeviceBridge
# ---------------------------------------------------------------------------

class DeviceBridge:
    def __init__(self, port: str, baud: int = 115200):
        if serial is None:
            raise ImportError("pyserial is required: pip install pyserial")
        self.port  = port
        self.baud  = baud
        self._ser  = None
        self._lock = threading.Lock()
        self._running = False
        self._reader_thread = None

        # Pending GET responses: widget_id → Event (threading.Event) + value slot
        self._get_pending: dict[str, threading.Event] = {}
        self._get_values:  dict[str, Any] = {}

        # Event callbacks: (widget_id, event_name) → [callback, ...]
        self._callbacks: dict[tuple, list] = {}

    # ------------------------------------------------------------------
    # Connection
    # ------------------------------------------------------------------

    def connect(self):
        self._ser = serial.Serial(self.port, self.baud, timeout=0.1)
        self._running = True
        self._reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader_thread.start()
        print(f"[bridge] Connected to {self.port} @ {self.baud}")

    def disconnect(self):
        self._running = False
        if self._ser:
            self._ser.close()
            self._ser = None

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def set(self, widget_id: str, value):
        if isinstance(value, bool):
            self._send(CMD_SET, widget_id, VAL_BOOL, bytes([1 if value else 0]))
        elif isinstance(value, int):
            self._send(CMD_SET, widget_id, VAL_INT32, struct.pack(">i", value))
        elif isinstance(value, float):
            self._send(CMD_SET, widget_id, VAL_FLOAT, struct.pack(">f", value))
        elif isinstance(value, str):
            self._send(CMD_SET, widget_id, VAL_STRING, value.encode("utf-8"))
        else:
            raise TypeError(f"Unsupported value type: {type(value)}")

    def get(self, widget_id: str, timeout: float = 2.0) -> str:
        evt = threading.Event()
        self._get_pending[widget_id] = evt
        self._get_values.pop(widget_id, None)
        self._send(CMD_GET, widget_id, VAL_NONE, b"")
        if not evt.wait(timeout):
            raise TimeoutError(f"GET '{widget_id}' timed out after {timeout}s")
        return self._get_values.get(widget_id, "")

    def show(self, widget_id: str):
        self._send(CMD_SHOW, widget_id, VAL_NONE, b"")

    def hide(self, widget_id: str):
        self._send(CMD_HIDE, widget_id, VAL_NONE, b"")

    def list_add(self, widget_id: str, text: str):
        self._send(CMD_LIST_ADD, widget_id, VAL_STRING, text.encode("utf-8"))

    def list_clear(self, widget_id: str):
        self._send(CMD_LIST_CLEAR, widget_id, VAL_NONE, b"")

    def ping(self) -> bool:
        # Simplistic: send ping and wait up to 1s for pong (checked by reader thread)
        self._pong_event = threading.Event()
        self._send(CMD_PING, "", VAL_NONE, b"")
        return self._pong_event.wait(1.0)

    def on(self, widget_id: str, event_type: str, callback: Callable):
        """Register a callback for a widget event. event_type: 'click', 'change', etc."""
        key = (widget_id, event_type)
        self._callbacks.setdefault(key, []).append(callback)

    def off(self, widget_id: str, event_type: str = None):
        if event_type is None:
            # Remove all callbacks for this widget
            keys = [k for k in self._callbacks if k[0] == widget_id]
            for k in keys:
                del self._callbacks[k]
        else:
            self._callbacks.pop((widget_id, event_type), None)

    def run_forever(self):
        try:
            while self._running:
                time.sleep(0.1)
        except KeyboardInterrupt:
            pass
        finally:
            self.disconnect()

    def run_until(self, condition_fn: Callable[[], bool], poll_s: float = 0.05):
        while self._running and not condition_fn():
            time.sleep(poll_s)

    # ------------------------------------------------------------------
    # Frame building and sending
    # ------------------------------------------------------------------

    def _send(self, cmd: int, widget_id: str, val_type: int, val: bytes):
        id_bytes = widget_id.encode("utf-8")[:255]
        id_len   = len(id_bytes)
        val_len  = len(val)

        xor_calc = cmd ^ id_len
        for b in id_bytes: xor_calc ^= b
        xor_calc ^= val_type ^ ((val_len >> 8) & 0xFF) ^ (val_len & 0xFF)
        for b in val: xor_calc ^= b

        frame  = bytes([MAGIC_HI, MAGIC_LO, cmd, id_len])
        frame += id_bytes
        frame += bytes([val_type, (val_len >> 8) & 0xFF, val_len & 0xFF])
        frame += val
        frame += bytes([xor_calc])

        with self._lock:
            if self._ser and self._ser.is_open:
                self._ser.write(frame)

    # ------------------------------------------------------------------
    # Background reader loop
    # ------------------------------------------------------------------

    def _reader_loop(self):
        while self._running:
            try:
                b = self._read_byte()
                if b != MAGIC_HI:
                    continue
                b = self._read_byte()
                if b != MAGIC_LO:
                    continue
                self._parse_frame()
            except Exception:
                pass

    def _read_byte(self) -> int:
        while self._running:
            data = self._ser.read(1)
            if data:
                return data[0]
            time.sleep(0.001)
        raise StopIteration

    def _read_exact(self, n: int) -> bytes:
        buf = b""
        while len(buf) < n and self._running:
            chunk = self._ser.read(n - len(buf))
            if chunk:
                buf += chunk
        return buf

    def _parse_frame(self):
        cmd     = self._read_byte()
        id_len  = self._read_byte()
        id_bytes = self._read_exact(id_len)
        widget_id = id_bytes.decode("utf-8", errors="replace")

        val_type = self._read_byte()
        vl_h     = self._read_byte()
        vl_l     = self._read_byte()
        val_len  = (vl_h << 8) | vl_l
        val      = self._read_exact(val_len)
        checksum = self._read_byte()

        # Verify
        xor_calc = cmd ^ id_len
        for b in id_bytes: xor_calc ^= b
        xor_calc ^= val_type ^ vl_h ^ vl_l
        for b in val: xor_calc ^= b
        if xor_calc != checksum:
            return  # silently drop bad frames

        self._dispatch(cmd, widget_id, val_type, val)

    def _dispatch(self, cmd: int, widget_id: str, val_type: int, val: bytes):
        if cmd == CMD_GET_RESP:
            text = val.decode("utf-8", errors="replace")
            self._get_values[widget_id] = text
            evt = self._get_pending.pop(widget_id, None)
            if evt:
                evt.set()

        elif cmd == CMD_EVENT:
            if len(val) >= 5:
                event_type_byte = val[0]
                int_val = struct.unpack(">i", val[1:5])[0]
                event_name = _EVENT_NAMES.get(event_type_byte, f"event_{event_type_byte}")
                event_obj = {"widget_id": widget_id, "type": event_name, "value": int_val}
                for cb in self._callbacks.get((widget_id, event_name), []):
                    try:
                        cb(event_obj)
                    except Exception as e:
                        print(f"[bridge] Callback error: {e}")
                # Also fire "*" wildcard listeners
                for cb in self._callbacks.get((widget_id, "*"), []):
                    try:
                        cb(event_obj)
                    except Exception as e:
                        print(f"[bridge] Callback error: {e}")

        elif cmd == CMD_PONG:
            pong_evt = getattr(self, "_pong_event", None)
            if pong_evt:
                pong_evt.set()
