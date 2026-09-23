#!/usr/bin/env python3
"""Small BLE bridge for pushing agent status to AI-Code-Pager."""

from __future__ import annotations

import argparse
import asyncio
import sys

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "AI-Code-Pager"
SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0"
RX_UUID = "12345678-1234-5678-1234-56789abcdef1"
TX_UUID = "12345678-1234-5678-1234-56789abcdef2"

OP_SET_KEY = 0x01
OP_SAVE = 0x02
OP_RESET = 0x03
OP_GET = 0x04
OP_TEXT = 0x10
OP_PET = 0x11

PET_STATES = {"scratch": 0, "point": 1, "cheer": 2, "sleep": 3}
INPUT_NAMES = [
    "K1", "K2", "K3", "K4", "K5", "K6", "UP", "DOWN", "LEFT",
    "RIGHT", "PUSH", "DIAL_CCW", "DIAL_CW",
]
KEY_USAGE = {
    "DISABLED": 0x00,
    "ENTER": 0x28,
    "ESC": 0x29,
    "BACKSPACE": 0x2A,
    "TAB": 0x2B,
    "SPACE": 0x2C,
    "RIGHT": 0x4F,
    "LEFT": 0x50,
    "DOWN": 0x51,
    "UP": 0x52,
    "PAGEUP": 0x4B,
    "PAGEDOWN": 0x4E,
    **{f"F{i}": 0x3A + i - 1 for i in range(1, 13)},
    **{f"F{i}": 0x68 + i - 13 for i in range(13, 19)},
}
MODIFIER = {"CTRL": 0x01, "SHIFT": 0x02, "ALT": 0x04, "GUI": 0x08}


async def find_pager(timeout: float):
    def matches(device, advertisement):
        uuids = {item.lower() for item in (advertisement.service_uuids or [])}
        return device.name == DEVICE_NAME or SERVICE_UUID in uuids

    device = await BleakScanner.find_device_by_filter(matches, timeout=timeout)
    if device is None:
        raise RuntimeError(f"未找到 {DEVICE_NAME}；请确认设备正在广播且未连接浏览器。")
    return device


async def write(client: BleakClient, payload: bytes) -> None:
    await client.write_gatt_char(RX_UUID, payload, response=False)


async def send_text(client: BleakClient, text: str) -> None:
    payload = text.encode("utf-8")
    if not payload:
        raise ValueError("消息不能为空")
    if len(payload) > 480:
        raise ValueError("消息的 UTF-8 编码不能超过 480 bytes")

    chunk_size = 18  # Works even when the negotiated ATT MTU remains 23.
    for offset in range(0, len(payload), chunk_size):
        chunk = payload[offset:offset + chunk_size]
        flags = (0x01 if offset == 0 else 0) | (
            0x02 if offset + chunk_size >= len(payload) else 0
        )
        await write(client, bytes((OP_TEXT, flags)) + chunk)


def parse_key(spec: str) -> tuple[int, int]:
    parts = [part.strip().upper() for part in spec.split("+")]
    usage_name = parts.pop()
    if usage_name not in KEY_USAGE:
        raise ValueError(f"未知按键 {usage_name}")
    modifiers = 0
    for part in parts:
        if part not in MODIFIER:
            raise ValueError(f"未知修饰键 {part}")
        modifiers |= MODIFIER[part]
    return modifiers, KEY_USAGE[usage_name]


def notification(_sender, data: bytearray) -> None:
    if len(data) >= 3 and data[0] == 0x80:
        name = INPUT_NAMES[data[1]] if data[1] < len(INPUT_NAMES) else str(data[1])
        print(f"input {name}: {'pressed' if data[2] else 'released'}", flush=True)
    elif len(data) >= 3 and data[0] == 0x81:
        print(f"ack opcode=0x{data[1]:02x} status={data[2]}", flush=True)
    elif data and data[0] == 0x82:
        print("keymap", data.hex(" "), flush=True)
    else:
        print("event", data.hex(" "), flush=True)


async def run(args) -> None:
    device = await find_pager(args.timeout)
    async with BleakClient(device) as client:
        await client.start_notify(TX_UUID, notification)

        if args.command == "text":
            if args.pet is not None:
                await write(client, bytes((OP_PET, PET_STATES[args.pet])))
            await send_text(client, args.message)
        elif args.command == "pet":
            await write(client, bytes((OP_PET, PET_STATES[args.state])))
        elif args.command == "set-key":
            modifiers, usage = parse_key(args.key)
            await write(client, bytes((OP_SET_KEY, args.input, modifiers, usage)))
            if args.save:
                await write(client, bytes((OP_SAVE,)))
        elif args.command == "reset":
            await write(client, bytes((OP_RESET,)))
        elif args.command == "keymap":
            await write(client, bytes((OP_GET,)))
            await asyncio.sleep(1)
        elif args.command == "listen":
            print("正在监听输入；按 Ctrl-C 退出。")
            await asyncio.Event().wait()

        if args.command not in {"listen", "keymap"}:
            await asyncio.sleep(0.2)  # Let final notification/ATT write drain.


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--timeout", type=float, default=10, help="扫描超时秒数")
    sub = result.add_subparsers(dest="command", required=True)

    text = sub.add_parser("text", help="推送信息流文本")
    text.add_argument("message")
    text.add_argument("--pet", choices=PET_STATES)

    pet = sub.add_parser("pet", help="切换宠物状态")
    pet.add_argument("state", choices=PET_STATES)

    set_key = sub.add_parser("set-key", help="实时修改一个输入的 HID 键")
    set_key.add_argument("input", type=int, choices=range(len(INPUT_NAMES)))
    set_key.add_argument("key", help="例如 F13、CTRL+ENTER、DISABLED")
    set_key.add_argument("--save", action="store_true")

    sub.add_parser("reset", help="恢复并保存默认键位")
    sub.add_parser("keymap", help="读取键位表")
    sub.add_parser("listen", help="持续打印物理输入事件")
    return result


def main() -> int:
    try:
        asyncio.run(run(parser().parse_args()))
        return 0
    except KeyboardInterrupt:
        return 130
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
