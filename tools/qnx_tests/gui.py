"""Host-side QEMU GUI automation primitives for the QNX test harness.

The QNX image is driven through QEMU's Machine Protocol (QMP), rather than
through a host GUI window.  This keeps the interaction deterministic and lets
an agent collect the framebuffer as an artifact:

* ``input-send-event`` drives the absolute virtio tablet used by QNX Screen.
* ``send-key`` drives QEMU's keyboard input path.
* ``screendump`` captures the QEMU display without requiring a VNC viewer.

The module deliberately uses only the Python standard library.  It is also
usable as a small library by future test modules and as a CLI through
``tools/qnx_gui.py``.
"""

from __future__ import annotations

import json
import os
import select
import socket
import struct
import time
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, List, Mapping, Optional, Sequence, Tuple


class QMPError(RuntimeError):
    """A QMP connection, protocol, or command error."""


# QEMU qcode names.  This intentionally covers printable ASCII and the
# control keys useful for GUI test flows.  QMP's send-key command accepts
# these names, not X11 keysyms or Linux input-event numbers.
_CHAR_TO_KEYS: Dict[str, Tuple[List[str], bool]] = {
    **{c: ([c], False) for c in "abcdefghijklmnopqrstuvwxyz"},
    **{c.upper(): ([c], True) for c in "abcdefghijklmnopqrstuvwxyz"},
    **{str(n): ([str(n)], False) for n in range(10)},
    " ": (["spc"], False),
    "\n": (["ret"], False),
    "\r": (["ret"], False),
    "\t": (["tab"], False),
    "-": (["minus"], False),
    "=": (["equal"], False),
    "[": (["bracket_left"], False),
    "]": (["bracket_right"], False),
    "\\": (["backslash"], False),
    ";": (["semicolon"], False),
    "'": (["apostrophe"], False),
    "`": (["grave_accent"], False),
    ",": (["comma"], False),
    ".": (["dot"], False),
    "/": (["slash"], False),
    "!": (["1"], True),
    "@": (["2"], True),
    "#": (["3"], True),
    "$": (["4"], True),
    "%": (["5"], True),
    "^": (["6"], True),
    "&": (["7"], True),
    "*": (["8"], True),
    "(": (["9"], True),
    ")": (["0"], True),
    "_": (["minus"], True),
    "+": (["equal"], True),
    "{": (["bracket_left"], True),
    "}": (["bracket_right"], True),
    "|": (["backslash"], True),
    ":": (["semicolon"], True),
    '"': (["apostrophe"], True),
    "~": (["grave_accent"], True),
    "<": (["comma"], True),
    ">": (["dot"], True),
    "?": (["slash"], True),
}


def _read_png_size(path: Path) -> Tuple[int, int]:
    """Read the dimensions from a PNG without requiring Pillow."""
    with path.open("rb") as fp:
        header = fp.read(24)
    if len(header) < 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise QMPError(f"QEMU screendump is not a PNG: {path}")
    width, height = struct.unpack(">II", header[16:24])
    if not width or not height:
        raise QMPError(f"QEMU screendump has invalid dimensions: {path}")
    return width, height


def _read_bmp(path: Path) -> Tuple[int, int, bytes]:
    """Decode the uncompressed 24/32-bit BMP emitted by QNX ``screenshot``.

    The QNX utility writes a host-visible file through NFS.  Converting here
    avoids requiring Pillow/ImageMagick on the host and gives agents a PNG
    artifact that can be inspected by normal image tooling.
    """
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise QMPError(f"guest screenshot is not a BMP: {path}")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40:
        raise QMPError(f"unsupported BMP DIB header in {path}")
    width = struct.unpack_from("<i", data, 18)[0]
    signed_height = struct.unpack_from("<i", data, 22)[0]
    planes, bits_per_pixel = struct.unpack_from("<HH", data, 26)
    compression = struct.unpack_from("<I", data, 30)[0]
    supported = (
        bits_per_pixel == 24 and compression == 0 or
        bits_per_pixel == 32 and compression in (0, 3)
    )
    if (width <= 0 or signed_height == 0 or planes != 1 or not supported):
        raise QMPError(
            f"unsupported BMP format in {path}: {width}x{signed_height}, "
            f"bpp={bits_per_pixel}, compression={compression}"
        )
    if bits_per_pixel == 32 and compression == 3:
        # QNX SDP 8 emits a BITMAPV4 header with 32-bit RGB bitfields and an
        # unused fourth byte (the alpha mask is commonly zero).
        if dib_size < 56 or len(data) < 70:
            raise QMPError(f"truncated BMP bitfield masks: {path}")
        red_mask, green_mask, blue_mask = struct.unpack_from("<III", data, 54)
        if not red_mask or not green_mask or not blue_mask:
            raise QMPError(f"invalid BMP bitfield masks: {path}")
    else:
        red_mask = green_mask = blue_mask = 0
    height = abs(signed_height)
    bytes_per_pixel = bits_per_pixel // 8
    row_stride = ((width * bytes_per_pixel + 3) // 4) * 4
    if pixel_offset + row_stride * height > len(data):
        raise QMPError(f"truncated BMP screenshot: {path}")
    rows = []
    for output_row in range(height):
        source_row = output_row if signed_height < 0 else height - 1 - output_row
        start = pixel_offset + source_row * row_stride
        row = data[start:start + width * bytes_per_pixel]
        rgb = bytearray()
        for pixel in range(width):
            raw = row[pixel * bytes_per_pixel:pixel * bytes_per_pixel + bytes_per_pixel]
            if bits_per_pixel == 32:
                value = int.from_bytes(raw, "little")
                def _channel(mask: int) -> int:
                    shift = (mask & -mask).bit_length() - 1
                    maximum = mask >> shift
                    return (value & mask) >> shift if maximum == 255 else round(
                        ((value & mask) >> shift) * 255 / maximum
                    )
                r = _channel(red_mask or 0x00ff0000)
                g = _channel(green_mask or 0x0000ff00)
                b = _channel(blue_mask or 0x000000ff)
            else:
                b, g, r = raw[:3]
            rgb.extend((r, g, b))
        rows.append(bytes(rgb))
    return width, height, b"".join(rows)


def _png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload)) + kind + payload +
        struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff)
    )


def _bmp_to_png(source: Path, output: Path) -> Tuple[int, int]:
    width, height, rgb = _read_bmp(source)
    scanlines = b"".join(
        b"\x00" + rgb[row * width * 3:(row + 1) * width * 3]
        for row in range(height)
    )
    png = (
        b"\x89PNG\r\n\x1a\n" +
        _png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
        _png_chunk(b"IDAT", zlib.compress(scanlines, level=6)) +
        _png_chunk(b"IEND", b"")
    )
    output.write_bytes(png)
    return width, height


def _scale_coordinate(value: int, extent: int) -> int:
    if extent <= 0:
        raise ValueError(f"screen extent must be positive (got {extent})")
    if value < 0 or value >= extent:
        raise ValueError(f"coordinate {value} is outside 0..{extent - 1}")
    return round(value * 32767 / max(extent - 1, 1))


@dataclass(frozen=True)
class Screenshot:
    """Metadata for a framebuffer capture."""

    path: Path
    width: int
    height: int


class QMPClient:
    """Small synchronous QMP client for a QEMU Unix socket."""

    def __init__(self, sock: socket.socket, socket_path: str):
        self._sock = sock
        self._socket_path = socket_path
        self._buffer = b""
        self._next_id = 1
        self._closed = False

    @classmethod
    def connect(cls, socket_path: str, timeout: float = 30.0) -> "QMPClient":
        """Connect and negotiate QMP capabilities.

        ``qnx_run.sh`` creates the socket before QEMU is ready to accept it,
        so connection attempts are retried until ``timeout`` expires.
        """
        deadline = time.monotonic() + timeout
        last_error: Optional[BaseException] = None
        while time.monotonic() < deadline:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                sock.settimeout(min(2.0, max(0.1, deadline - time.monotonic())))
                sock.connect(socket_path)
                client = cls(sock, socket_path)
                greeting = client._recv_message(deadline)
                if "QMP" not in greeting:
                    raise QMPError(f"unexpected QMP greeting: {greeting}")
                client.execute("qmp_capabilities", _deadline=deadline)
                sock.settimeout(None)
                return client
            except (OSError, TimeoutError, QMPError, json.JSONDecodeError) as exc:
                last_error = exc
                sock.close()
                time.sleep(min(0.1, max(0.0, deadline - time.monotonic())))
        raise TimeoutError(
            f"could not connect to QMP socket {socket_path!r} within {timeout:g}s"
            + (f": {last_error}" if last_error else "")
        )

    def close(self) -> None:
        if not self._closed:
            self._closed = True
            self._sock.close()

    def __enter__(self) -> "QMPClient":
        return self

    def __exit__(self, _type, _value, _traceback) -> None:
        self.close()

    @property
    def socket_path(self) -> str:
        return self._socket_path

    def _send_message(self, message: Mapping[str, Any]) -> None:
        if self._closed:
            raise QMPError("QMP connection is closed")
        payload = (json.dumps(message, separators=(",", ":")) + "\r\n").encode()
        self._sock.sendall(payload)

    def _recv_message(self, deadline: Optional[float] = None) -> Dict[str, Any]:
        while b"\n" not in self._buffer:
            wait = None
            if deadline is not None:
                wait = max(0.0, deadline - time.monotonic())
                if wait == 0:
                    raise TimeoutError("timed out waiting for QMP response")
            readable, _, _ = select.select([self._sock], [], [], wait)
            if not readable:
                raise TimeoutError("timed out waiting for QMP response")
            data = self._sock.recv(65536)
            if not data:
                raise QMPError("QEMU closed the QMP connection")
            self._buffer += data
        line, self._buffer = self._buffer.split(b"\n", 1)
        try:
            message = json.loads(line.strip())
        except json.JSONDecodeError as exc:
            raise QMPError(f"invalid JSON from QMP: {line!r}") from exc
        if not isinstance(message, dict):
            raise QMPError(f"invalid QMP message: {message!r}")
        return message

    def execute(
        self,
        command: str,
        arguments: Optional[Mapping[str, Any]] = None,
        *,
        _deadline: Optional[float] = None,
    ) -> Dict[str, Any]:
        """Execute a command and return its complete QMP response."""
        request_id = self._next_id
        self._next_id += 1
        message: Dict[str, Any] = {"execute": command, "id": request_id}
        if arguments:
            message["arguments"] = dict(arguments)
        self._send_message(message)
        while True:
            response = self._recv_message(_deadline)
            # QMP events can arrive between a request and its response.
            if "event" in response and "id" not in response:
                continue
            if response.get("id") != request_id:
                raise QMPError(
                    f"unexpected QMP response id for {command!r}: {response}"
                )
            if "error" in response:
                error = response["error"]
                raise QMPError(f"QMP command {command!r} failed: {error}")
            return response

    def send_key(self, keys: Sequence[str], hold_time_ms: int = 1) -> None:
        if not keys:
            raise ValueError("at least one key is required")
        if hold_time_ms < 0:
            raise ValueError("hold time must not be negative")
        self.execute(
            "send-key",
            {"keys": [{"type": "qcode", "data": key} for key in keys],
             "hold-time": hold_time_ms},
        )

    def key(self, key: str, hold_time_ms: int = 1) -> None:
        self.send_key([key], hold_time_ms=hold_time_ms)

    def type_text(self, text: str, hold_time_ms: int = 1) -> int:
        """Type printable ASCII text and return the number of characters.

        Unicode and characters outside the explicit qcode table are rejected
        instead of being silently dropped.  Silent loss is particularly hard
        for an autonomous debugger to diagnose.
        """
        sent = 0
        for char in text:
            mapping = _CHAR_TO_KEYS.get(char)
            if mapping is None:
                raise ValueError(
                    f"unsupported character {char!r}; type ASCII or send a qcode"
                )
            key_codes, needs_shift = mapping
            keys = (["shift"] + key_codes) if needs_shift else key_codes
            self.send_key(keys, hold_time_ms=hold_time_ms)
            sent += 1
        return sent

    def input_send_event(
        self,
        events: Sequence[Mapping[str, Any]],
        device: Optional[str] = None,
        head: Optional[int] = None,
    ) -> None:
        if not events:
            raise ValueError("at least one input event is required")
        args: Dict[str, Any] = {"events": [dict(event) for event in events]}
        if device is not None:
            args["device"] = device
        if head is not None:
            args["head"] = head
        self.execute("input-send-event", args)

    def mouse_move(self, x: int, y: int, width: int, height: int) -> None:
        self.input_send_event([
            {"type": "abs", "data": {"axis": "x", "value": _scale_coordinate(x, width)}},
            {"type": "abs", "data": {"axis": "y", "value": _scale_coordinate(y, height)}},
        ])

    def mouse_click(
        self,
        x: int,
        y: int,
        width: int,
        height: int,
        button: str = "left",
        double: bool = False,
        delay: float = 0.05,
    ) -> None:
        self.mouse_move(x, y, width, height)
        down = {"type": "btn", "data": {"button": button, "down": True}}
        up = {"type": "btn", "data": {"button": button, "down": False}}
        for index in range(2 if double else 1):
            self.input_send_event([down])
            self.input_send_event([up])
            if double and index == 0:
                time.sleep(delay)

    def screenshot(self, path: os.PathLike[str] | str) -> Screenshot:
        output = Path(path).expanduser().resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        try:
            output.unlink()
        except FileNotFoundError:
            pass
        fmt = "ppm" if output.suffix.lower() == ".ppm" else "png"
        self.execute("screendump", {"filename": str(output), "format": fmt})
        if not output.is_file() or output.stat().st_size == 0:
            raise QMPError(f"QEMU did not create screendump {output}")
        if fmt == "png":
            width, height = _read_png_size(output)
        else:
            width, height = _read_ppm_size(output)
        return Screenshot(output, width, height)


def guest_screenshot(
    guest_file: str,
    output: os.PathLike[str] | str,
    serial_port: int = 10024,
    timeout: float = 60.0,
    serial_log: Optional[os.PathLike[str] | str] = None,
    chromium_src: Optional[os.PathLike[str] | str] = None,
) -> Screenshot:
    """Capture QNX Screen through the guest ``screenshot`` utility.

    This is a fallback for virgl/display combinations where QMP screendump
    cannot read the accelerated scanout.  ``guest_file`` must normally be an
    NFS-visible path such as ``/mnt/nfs/out/qnx_release/frame.bmp``.
    """
    from .common import QNXSerial, q

    output_path = Path(output).expanduser().resolve()
    guest_path = Path(guest_file)
    if not guest_path.is_absolute():
        raise ValueError("guest screenshot path must be absolute")
    if not guest_path.as_posix().startswith("/mnt/nfs/"):
        raise ValueError("guest screenshot path must be under /mnt/nfs")
    if serial_log is None:
        serial_log = output_path.with_suffix(output_path.suffix + ".serial.log")
    serial = QNXSerial(serial_port, boot_timeout=min(60, int(timeout)),
                       log_path=str(serial_log))
    try:
        serial.connect()
        exit_code, _ = serial.run_command(
            f"screenshot -file={q(guest_path.as_posix())}",
            timeout=timeout,
            stream_output=False,
        )
    finally:
        serial.close()
    if exit_code != 0:
        raise QMPError(f"guest screenshot command failed with exit {exit_code}")

    # The guest path is on the host's NFS export.  Derive its host mirror
    # unless the caller is already writing to that mirror.
    source_value = chromium_src or os.environ.get("CHROMIUM_SRC", "")
    source = (Path(source_value).expanduser() if source_value
              else Path(__file__).resolve().parents[2])
    source = source.resolve()
    host_source = source / guest_path.relative_to("/mnt/nfs")
    if not host_source.is_file():
        raise QMPError(f"guest screenshot did not appear on NFS: {host_source}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if output_path.suffix.lower() == ".bmp":
        if output_path != host_source:
            output_path.write_bytes(host_source.read_bytes())
        width, height, _ = _read_bmp(output_path)
    else:
        width, height = _bmp_to_png(host_source, output_path)
    return Screenshot(output_path, width, height)


def _read_ppm_size(path: Path) -> Tuple[int, int]:
    """Read a PPM header, tolerating comments and arbitrary whitespace."""
    tokens: List[bytes] = []
    with path.open("rb") as fp:
        while len(tokens) < 3:
            line = fp.readline()
            if not line:
                break
            line = line.split(b"#", 1)[0]
            tokens.extend(line.split())
    if len(tokens) < 3 or tokens[0] not in (b"P6", b"P3"):
        raise QMPError(f"QEMU screendump is not a supported PPM: {path}")
    try:
        return int(tokens[1]), int(tokens[2])
    except ValueError as exc:
        raise QMPError(f"QEMU screendump has invalid dimensions: {path}") from exc


def wait_for_change(
    client: QMPClient,
    baseline: os.PathLike[str] | str,
    timeout: float = 10.0,
    poll_interval: float = 0.25,
) -> Screenshot:
    """Poll QMP screendumps until the framebuffer differs from ``baseline``."""
    baseline_bytes = Path(baseline).read_bytes()
    deadline = time.monotonic() + timeout
    candidate = Path(baseline).with_name(Path(baseline).stem + ".current.png")
    while time.monotonic() < deadline:
        shot = client.screenshot(candidate)
        if candidate.read_bytes() != baseline_bytes:
            return shot
        time.sleep(poll_interval)
    raise TimeoutError(f"screen did not change within {timeout:g}s")


def parse_screen_size(value: str) -> Tuple[int, int]:
    try:
        width, height = (int(part) for part in value.lower().split("x", 1))
    except (TypeError, ValueError) as exc:
        raise ValueError(f"screen size must be WIDTHxHEIGHT (got {value!r})") from exc
    if width <= 0 or height <= 0:
        raise ValueError(f"screen size must be positive (got {value!r})")
    return width, height
