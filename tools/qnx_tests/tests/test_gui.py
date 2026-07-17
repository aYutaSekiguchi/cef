from __future__ import annotations

import json
import socket
import struct
import sys
import tempfile
import threading
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from qnx_tests.gui import (  # noqa: E402
    QMPClient,
    _bmp_to_png,
    _read_png_size,
    parse_screen_size,
)


class FakeQMP:
    def __init__(self, path: str):
        self.path = path
        self.messages = []
        self._server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._server.bind(path)
        self._server.listen(1)
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def _serve(self):
        try:
            conn, _ = self._server.accept()
        except OSError:
            return
        with conn:
            conn.sendall(b'{"QMP":{"version":{"qemu":{"major":10}},"capabilities":[]}}\r\n')
            buf = b""
            while not self._stop.is_set():
                data = conn.recv(65536)
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    message = json.loads(line)
                    self.messages.append(message)
                    command = message["execute"]
                    if command == "screendump":
                        path = Path(message["arguments"]["filename"])
                        path.write_bytes(
                            b"\x89PNG\r\n\x1a\n"
                            + b"\x00\x00\x00\rIHDR"
                            + struct.pack(">II", 800, 600)
                            + b"\x08\x02\x00\x00\x00"
                        )
                    response = {"return": {}, "id": message["id"]}
                    conn.sendall((json.dumps(response) + "\r\n").encode())

    def close(self):
        self._stop.set()
        self._server.close()
        self._thread.join(timeout=1)


class QMPClientTest(unittest.TestCase):
    def test_input_and_screendump_protocol(self):
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "qmp.sock")
            fake = FakeQMP(path)
            try:
                with QMPClient.connect(path) as client:
                    client.mouse_click(100, 50, 800, 600)
                    client.send_key(["ctrl", "l"])
                    self.assertEqual(client.type_text("A!"), 2)
                    shot = client.screenshot(Path(directory) / "screen.png")
                    self.assertEqual((shot.width, shot.height), (800, 600))

                commands = [message["execute"] for message in fake.messages]
                self.assertEqual(commands[0], "qmp_capabilities")
                self.assertEqual(commands.count("input-send-event"), 3)
                input_messages = [
                    message for message in fake.messages
                    if message["execute"] == "input-send-event"
                ]
                self.assertEqual(input_messages[0]["arguments"]["events"][0]["data"],
                                 {"axis": "x", "value": round(100 * 32767 / 799)})
                key_messages = [
                    message for message in fake.messages
                    if message["execute"] == "send-key"
                ]
                self.assertEqual(key_messages[0]["arguments"]["keys"],
                                 [{"type": "qcode", "data": "ctrl"},
                                  {"type": "qcode", "data": "l"}])
            finally:
                fake.close()

    def test_screen_size_validation(self):
        self.assertEqual(parse_screen_size("1024x768"), (1024, 768))
        with self.assertRaises(ValueError):
            parse_screen_size("0x768")
        with self.assertRaises(ValueError):
            parse_screen_size("not-a-size")

    def test_guest_bmp_is_converted_to_png(self):
        with tempfile.TemporaryDirectory() as directory:
            bmp = Path(directory) / "screen.bmp"
            png = Path(directory) / "screen.png"
            # 2x2, 24-bit, bottom-up BMP (red/green below blue/white).
            # Rows are padded to 4 bytes.
            pixels = bytes([
                255, 0, 0, 0, 255, 0, 0, 0,  # bottom: blue, green
                0, 0, 255, 255, 255, 255, 0, 0,  # top: red, white
            ])
            header = bytearray(54)
            header[:2] = b"BM"
            struct.pack_into("<I", header, 2, 54 + len(pixels))
            struct.pack_into("<I", header, 10, 54)
            struct.pack_into("<I", header, 14, 40)
            struct.pack_into("<i", header, 18, 2)
            struct.pack_into("<i", header, 22, 2)
            struct.pack_into("<H", header, 26, 1)
            struct.pack_into("<H", header, 28, 24)
            struct.pack_into("<I", header, 34, len(pixels))
            bmp.write_bytes(bytes(header) + pixels)
            self.assertEqual(_bmp_to_png(bmp, png), (2, 2))
            self.assertEqual(_read_png_size(png), (2, 2))

    def test_unsupported_text_is_not_silently_dropped(self):
        with self.assertRaises(ValueError):
            # This does not need a live QMP connection: validate the mapping
            # only after connect in production, so this case is covered by
            # the CLI contract through the public API's explicit error path.
            QMPClient.__new__(QMPClient).type_text("日本語")


if __name__ == "__main__":
    unittest.main()
