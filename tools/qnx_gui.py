#!/usr/bin/env python3
"""Drive the QNX QEMU display without a host GUI window.

Start a GUI-capable QEMU session first, for example:

  ./cef/tools/qnx_run.sh --virgl --with-input --keep-qemu --mount-only

Then use this command from the Chromium source root:

  ./cef/tools/qnx_gui.py --json screenshot --output out/qnx_release/boot.png
  ./cef/tools/qnx_gui.py --screen-size 1280x720 click --x 100 --y 100
  ./cef/tools/qnx_gui.py run tools/qnx_gui.example.json

The default QMP socket is /tmp/qnx-qmp.sock and can be overridden by
QNX_QMP_SOCK or --socket.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path
from typing import Any, Dict, Iterable, List, Mapping, Sequence, Tuple

_HERE = Path(__file__).resolve().parent
_TOOLS = _HERE
if str(_TOOLS) not in sys.path:
    sys.path.insert(0, str(_TOOLS))

from qnx_tests.gui import (  # noqa: E402
    QMPClient,
    QMPError,
    Screenshot,
    guest_screenshot,
    parse_screen_size,
    wait_for_change,
)


_KEY_ALIASES = {
    "return": "ret",
    "enter": "ret",
    "escape": "esc",
    "control": "ctrl",
    "option": "alt",
    "windows": "meta_l",
    "cmd": "meta_l",
    "command": "meta_l",
    "space": "spc",
    "backspace": "backspace",
    "delete": "delete",
    "left-arrow": "left",
    "right-arrow": "right",
    "up-arrow": "up",
    "down-arrow": "down",
}


def _json_mode(args: argparse.Namespace, value: Mapping[str, Any]) -> None:
    if args.json:
        print(json.dumps(value, sort_keys=True))


def _human_or_json(args: argparse.Namespace, value: Mapping[str, Any], text: str) -> None:
    if args.json:
        _json_mode(args, value)
    else:
        print(text)


def _parse_key_sequence(value: str) -> List[str]:
    keys = []
    for raw in value.split("+"):
        key = _KEY_ALIASES.get(raw.lower(), raw.lower())
        if not key:
            raise ValueError(f"empty key in {value!r}")
        keys.append(key)
    return keys


def _screen_size(args: argparse.Namespace, scenario: Mapping[str, Any] | None = None) -> Tuple[int, int]:
    value = getattr(args, "screen_size", None)
    if not value and scenario:
        value = scenario.get("screen_size")
    if isinstance(value, Mapping):
        try:
            return int(value["width"]), int(value["height"])
        except (KeyError, TypeError, ValueError) as exc:
            raise ValueError("screen_size object needs width and height") from exc
    if not value:
        value = os.environ.get("QNX_GUI_SCREEN_SIZE", "1280x720")
    return parse_screen_size(str(value))


def _resolve_path(path: str, artifact_dir: Path) -> Path:
    output = Path(path).expanduser()
    return output if output.is_absolute() else artifact_dir / output


def _screenshot(client: QMPClient, path: Path) -> Dict[str, Any]:
    shot = client.screenshot(path)
    return {
        "path": str(shot.path),
        "width": shot.width,
        "height": shot.height,
        "bytes": shot.path.stat().st_size,
    }


def _run_action(
    client: QMPClient,
    action: Mapping[str, Any],
    screen_width: int,
    screen_height: int,
    artifact_dir: Path,
    index: int,
    screenshot_source: str = "qmp",
    guest_capture=None,
) -> Dict[str, Any]:
    name = str(action.get("action", ""))
    if name == "sleep":
        seconds = float(action.get("seconds", 0))
        if seconds < 0:
            raise ValueError("sleep seconds must not be negative")
        time.sleep(seconds)
        return {"action": name, "seconds": seconds}
    if name == "screenshot":
        path = _resolve_path(
            str(action.get("path", f"step-{index:03d}.png")), artifact_dir
        )
        source = str(action.get("source", screenshot_source))
        if source == "guest":
            if guest_capture is None:
                raise ValueError("guest screenshot source is not configured")
            return {"action": name, **guest_capture(path)}
        if source != "qmp":
            raise ValueError(f"unsupported screenshot source {source!r}")
        return {"action": name, **_screenshot(client, path)}
    if name == "move":
        x, y = int(action["x"]), int(action["y"])
        client.mouse_move(x, y, screen_width, screen_height)
        return {"action": name, "x": x, "y": y}
    if name == "click":
        x, y = int(action["x"]), int(action["y"])
        button = str(action.get("button", "left"))
        double = bool(action.get("double", False))
        client.mouse_click(x, y, screen_width, screen_height, button, double)
        return {"action": name, "x": x, "y": y, "button": button, "double": double}
    if name == "key":
        keys = _parse_key_sequence(str(action["key"]))
        client.send_key(keys, hold_time_ms=int(action.get("hold_ms", 1)))
        return {"action": name, "keys": keys}
    if name == "type":
        text = str(action["text"])
        count = client.type_text(text, hold_time_ms=int(action.get("hold_ms", 1)))
        return {"action": name, "characters": count}
    if name == "wait_for_change":
        baseline = _resolve_path(str(action["from"]), artifact_dir)
        shot = wait_for_change(
            client,
            baseline,
            timeout=float(action.get("timeout", 10)),
            poll_interval=float(action.get("poll_interval", 0.25)),
        )
        return {"action": name, "path": str(shot.path), "width": shot.width, "height": shot.height}
    raise ValueError(f"unsupported scenario action {name!r}")


def run_scenario(client: QMPClient, scenario_path: Path, artifact_dir: Path, args: argparse.Namespace) -> Dict[str, Any]:
    try:
        scenario = json.loads(scenario_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"could not read scenario {scenario_path}: {exc}") from exc
    if not isinstance(scenario, Mapping) or not isinstance(scenario.get("actions"), list):
        raise ValueError("scenario must be an object with an actions array")

    artifact_dir.mkdir(parents=True, exist_ok=True)
    width, height = _screen_size(args, scenario)
    screenshot_source = getattr(args, "source", "qmp")
    guest_capture = getattr(args, "guest_capture", None)
    results: List[Dict[str, Any]] = []
    started = time.time()
    for index, action in enumerate(scenario["actions"], 1):
        if not isinstance(action, Mapping):
            raise ValueError(f"scenario action {index} is not an object")
        try:
            result = _run_action(
                client, action, width, height, artifact_dir, index,
                screenshot_source=screenshot_source,
                guest_capture=guest_capture,
            )
            result["index"] = index
            results.append(result)
        except Exception as exc:  # noqa: BLE001 - preserve failure artifacts
            failure: Dict[str, Any] = {
                "index": index,
                "action": action.get("action"),
                "error": str(exc),
            }
            failure_path = artifact_dir / f"failure-{index:03d}.png"
            try:
                if screenshot_source == "guest" and guest_capture is not None:
                    failure["screenshot"] = guest_capture(failure_path)
                else:
                    failure["screenshot"] = _screenshot(client, failure_path)
            except Exception as screenshot_exc:  # noqa: BLE001
                failure["screenshot_error"] = str(screenshot_exc)
            results.append(failure)
            return {
                "scenario": str(scenario_path),
                "artifact_dir": str(artifact_dir),
                "ok": False,
                "elapsed_seconds": round(time.time() - started, 3),
                "steps": results,
            }

    return {
        "scenario": str(scenario_path),
        "artifact_dir": str(artifact_dir),
        "ok": True,
        "elapsed_seconds": round(time.time() - started, 3),
        "steps": results,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Drive a QNX QEMU GUI through QMP (screenshots + input)."
    )
    parser.add_argument(
        "--socket", default=os.environ.get("QNX_QMP_SOCK", "/tmp/qnx-qmp.sock"),
        help="QEMU QMP Unix socket (default: %(default)s)",
    )
    parser.add_argument("--connect-timeout", type=float, default=30.0)
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    parser.add_argument(
        "--screen-size", default=None,
        help="display size WIDTHxHEIGHT for pointer scaling; defaults to QNX_GUI_SCREEN_SIZE or 1280x720",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    screenshot = sub.add_parser("screenshot", help="capture the QEMU framebuffer")
    screenshot.add_argument("--output", required=True)
    screenshot.add_argument(
        "--source", choices=["qmp", "guest"], default="qmp",
        help="capture through QMP (default) or QNX Screen's guest screenshot utility",
    )
    screenshot.add_argument("--serial-port", type=int, default=10024)
    screenshot.add_argument(
        "--guest-file",
        help="absolute /mnt/nfs path for --source guest; derived from --output when omitted",
    )
    screenshot.add_argument("--timeout", type=float, default=60.0)

    move = sub.add_parser("move", help="move the absolute pointer")
    move.add_argument("--x", type=int, required=True)
    move.add_argument("--y", type=int, required=True)

    click = sub.add_parser("click", help="move and click the absolute pointer")
    click.add_argument("--x", type=int, required=True)
    click.add_argument("--y", type=int, required=True)
    click.add_argument("--button", default="left", choices=["left", "middle", "right", "wheel-up", "wheel-down"])
    click.add_argument("--double", action="store_true")

    key = sub.add_parser("key", help="send a qcode or a + separated combination")
    key.add_argument("key")
    key.add_argument("--hold-ms", type=int, default=1)

    type_text = sub.add_parser("type", help="type ASCII text")
    type_text.add_argument("text")
    type_text.add_argument("--hold-ms", type=int, default=1)

    run = sub.add_parser("run", help="run a JSON GUI scenario")
    run.add_argument("scenario", type=Path)
    run.add_argument("--artifact-dir", type=Path, default=Path("out/qnx_release/gui-artifacts"))
    run.add_argument(
        "--source", choices=["qmp", "guest"], default="qmp",
        help="default screenshot source for actions (default: qmp)",
    )
    run.add_argument("--serial-port", type=int, default=10024)
    run.add_argument("--guest-timeout", type=float, default=60.0)

    sub.add_parser("mice", help="query QEMU input devices")
    sub.add_parser("status", help="query QEMU VM status")
    return parser


def _default_guest_file(output: Path) -> str:
    source_value = os.environ.get("CHROMIUM_SRC", "")
    source = (Path(source_value).expanduser() if source_value
              else Path(__file__).resolve().parents[2]).resolve()
    raw_output = output.expanduser().resolve()
    if raw_output.suffix.lower() != ".bmp":
        raw_output = raw_output.with_suffix(".bmp")
    try:
        relative = raw_output.relative_to(source)
    except ValueError as exc:
        raise ValueError(
            "--source guest output must be under CHROMIUM_SRC; use --guest-file "
            "for a custom NFS path"
        ) from exc
    return "/mnt/nfs/" + relative.as_posix()


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "screenshot" and args.source == "guest":
            output = Path(args.output)
            guest_file = args.guest_file or _default_guest_file(output)
            shot = guest_screenshot(
                guest_file,
                output,
                serial_port=args.serial_port,
                timeout=args.timeout,
                chromium_src=os.environ.get("CHROMIUM_SRC") or str(Path(__file__).resolve().parents[2]),
            )
            result = {
                "path": str(shot.path),
                "width": shot.width,
                "height": shot.height,
                "bytes": shot.path.stat().st_size,
                "source": "guest",
            }
            _human_or_json(args, result, f"guest screenshot -> {result['path']} ({result['width']}x{result['height']})")
            return 0

        with QMPClient.connect(args.socket, timeout=args.connect_timeout) as client:
            if args.command == "screenshot":
                result = _screenshot(client, Path(args.output))
                _human_or_json(args, result, f"screenshot -> {result['path']} ({result['width']}x{result['height']})")
            elif args.command == "move":
                width, height = _screen_size(args)
                client.mouse_move(args.x, args.y, width, height)
                _human_or_json(args, {"action": "move", "x": args.x, "y": args.y}, f"moved pointer to ({args.x}, {args.y})")
            elif args.command == "click":
                width, height = _screen_size(args)
                client.mouse_click(args.x, args.y, width, height, args.button, args.double)
                _human_or_json(args, {"action": "click", "x": args.x, "y": args.y, "button": args.button, "double": args.double}, f"clicked {args.button} at ({args.x}, {args.y})")
            elif args.command == "key":
                keys = _parse_key_sequence(args.key)
                client.send_key(keys, hold_time_ms=args.hold_ms)
                _human_or_json(args, {"action": "key", "keys": keys}, f"sent {'+'.join(keys)}")
            elif args.command == "type":
                count = client.type_text(args.text, hold_time_ms=args.hold_ms)
                _human_or_json(args, {"action": "type", "characters": count}, f"typed {count} characters")
            elif args.command == "run":
                if args.source == "guest":
                    def _capture_guest(path: Path) -> Dict[str, Any]:
                        guest_file = _default_guest_file(path)
                        shot = guest_screenshot(
                            guest_file,
                            path,
                            serial_port=args.serial_port,
                            timeout=args.guest_timeout,
                            chromium_src=os.environ.get("CHROMIUM_SRC") or str(Path(__file__).resolve().parents[2]),
                        )
                        return {
                            "path": str(shot.path),
                            "width": shot.width,
                            "height": shot.height,
                            "bytes": shot.path.stat().st_size,
                            "source": "guest",
                        }
                    args.guest_capture = _capture_guest
                result = run_scenario(client, args.scenario, args.artifact_dir, args)
                if args.json:
                    print(json.dumps(result, sort_keys=True))
                else:
                    print(json.dumps(result, indent=2, sort_keys=True))
                return 0 if result["ok"] else 1
            elif args.command == "mice":
                result = client.execute("query-mice").get("return", [])
                _human_or_json(args, {"mice": result}, json.dumps(result, indent=2))
            elif args.command == "status":
                result = client.execute("query-status").get("return", {})
                _human_or_json(args, result, json.dumps(result, indent=2, sort_keys=True))
    except (OSError, QMPError, TimeoutError, ValueError, KeyError) as exc:
        if args.json:
            print(json.dumps({"ok": False, "error": str(exc)}))
        else:
            print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
