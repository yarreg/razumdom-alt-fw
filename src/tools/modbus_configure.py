#!/usr/bin/env python3
"""Configure DDM845R / DDL84R alternative firmware over Modbus RTU."""

from __future__ import annotations

import argparse
import time
from pathlib import Path
from typing import Dict, List, Optional

try:
    import yaml
except ImportError as exc:  # pragma: no cover
    raise SystemExit("PyYAML is required: pip install pyyaml") from exc

from modbus_client import ModbusClient, decode_port_cfg, port_cfg

TYPE_CODES = {"rocker": 1, "latching": 2, "momentary": 3}
CODE_TYPES = {v: k for k, v in TYPE_CODES.items()}
TARGET_CHANNEL = 1
TARGET_GROUP = 2
GROUP_ALL = 1

# Types that use a secondary DI (down key)
ROCKER_TYPES = {"rocker"}
PROFILE_BASE = 5010
PROFILE_STRIDE = 16
BINDING_BASE = 5100
BINDING_STRIDE = 32


def di_value(text: object) -> int:
    if isinstance(text, int):
        value = text
    else:
        s = str(text).upper()
        if not s.startswith("DI"):
            raise ValueError(f"DI value expected, got {text!r}")
        value = int(s[2:])
    if value < 0 or value > 8:
        raise ValueError(f"DI out of range: {value}")
    return value


def normalize_config(data: Dict[str, object]) -> Dict[int, int]:
    modbus = data.get("modbus", {}) or {}
    inputs = data.get("inputs", {}) or {}
    auto_light = data.get("auto_light", {}) or {}
    bindings = data.get("bindings", []) or []
    if "adc_active_threshold" in inputs:
        raise ValueError("inputs.adc_active_threshold is legacy; use inputs.di_adc_active_threshold")
    if len(bindings) > 8:
        raise ValueError("at most 8 bindings are supported")
    legacy_binding_keys = {
        "output",
        "min_level",
        "max_level",
        "default_level",
        "night_level",
        "fade_on_ms",
        "fade_off_ms",
        "output_curve",
        "ramp_step",
        "ramp_period_ms",
    }

    regs: Dict[int, int] = {
        0: int(modbus.get("address", 34)),
        1: port_cfg(int(modbus.get("baud", 57600)), str(modbus.get("parity", "N")), int(modbus.get("stop_bits", 2))),
        5002: len(bindings),
        5003: int(inputs.get("di_adc_active_threshold", 1000)),
        5004: int(inputs.get("debounce_ms", 30)),
        5005: int(inputs.get("short_press_min_ms", 50)),
        5006: int(inputs.get("short_press_max_ms", 700)),
        5007: int(inputs.get("long_press_ms", 800)),
        5008: int(auto_light.get("suppress_s", 15)),
    }

    channels = data.get("channels")
    if not isinstance(channels, dict):
        raise ValueError("channels section is required")
    for channel in range(1, 5):
        item = channels.get(channel) or channels.get(str(channel)) or {}
        if not isinstance(item, dict):
            raise ValueError(f"channel {channel} must be a mapping")
        base = PROFILE_BASE + (channel - 1) * PROFILE_STRIDE
        regs.update({
            base + 0: int(item.get("min_level", 0)),
            base + 1: int(item.get("max_level", 1023)),
            base + 2: int(item.get("default_level", item.get("max_level", 1023))),
            base + 3: int(item.get("night_level", 0)),
            base + 4: int(item.get("fade_on_ms", 0)),
            base + 5: int(item.get("fade_off_ms", 0)),
            base + 6: int(item.get("output_curve", 0)),
        })
        for off in range(7, PROFILE_STRIDE):
            regs[base + off] = 0

    used_di = set()
    for index, item in enumerate(bindings):
        if not isinstance(item, dict):
            raise ValueError(f"binding {index} must be a mapping")
        legacy_keys = sorted(legacy_binding_keys & set(item))
        if legacy_keys:
            raise ValueError(f"binding {index} uses legacy keys: {', '.join(legacy_keys)}")
        typ = str(item["type"]).lower()
        if typ not in TYPE_CODES:
            raise ValueError(f"unsupported binding type: {typ}")
        is_rocker = typ in ROCKER_TYPES
        primary = di_value(item.get("up") if is_rocker else item.get("input"))
        secondary = di_value(item["down"]) if is_rocker else 0xFFFF
        for di in [primary] + ([] if secondary == 0xFFFF else [secondary]):
            if di in used_di:
                raise ValueError(f"DI{di} is used by more than one active binding")
            used_di.add(di)
        target = str(item.get("target", "")).lower()
        if not target:
            raise ValueError(f"binding {index} target is required")
        if target in ("rgbw", "all", "group1"):
            target_type = TARGET_GROUP
            target_id = GROUP_ALL
        else:
            if not target.startswith("channel"):
                raise ValueError(f"binding {index} target must be channel1..channel4 or rgbw")
            output = int(target.removeprefix("channel"))
            target_type = TARGET_CHANNEL
            target_id = output
        if target_type == TARGET_CHANNEL and not (1 <= target_id <= 4):
            raise ValueError(f"binding {index} target channel out of range: {target_id}")

        base = BINDING_BASE + index * BINDING_STRIDE
        row = [
            int(item.get("enable", 1)),
            TYPE_CODES[typ],
            target_type,
            target_id,
            primary,
            secondary,
            int(item.get("hold_step", 8)),
            int(item.get("hold_period_ms", 20)),
            int(item.get("flags", 0)),
        ] + [0] * 23
        regs.update({base + off: value for off, value in enumerate(row)})

    for index in range(len(bindings), 8):
        base = BINDING_BASE + index * BINDING_STRIDE
        regs.update({base + off: 0 for off in range(BINDING_STRIDE)})
    return regs


def regs_to_yaml(regs: Dict[int, int]) -> Dict[str, object]:
    port = decode_port_cfg(regs[1])
    result: Dict[str, object] = {
        "modbus": {"address": regs[0], **port},
        "inputs": {
            "di_adc_active_threshold": regs[5003],
            "debounce_ms": regs[5004],
            "short_press_min_ms": regs[5005],
            "short_press_max_ms": regs[5006],
            "long_press_ms": regs[5007],
        },
        "auto_light": {
            "suppress_s": regs[5008],
        },
        "channels": {},
        "bindings": [],
    }
    for channel in range(1, 5):
        base = PROFILE_BASE + (channel - 1) * PROFILE_STRIDE
        result["channels"][channel] = {
            "min_level": regs[base + 0],
            "max_level": regs[base + 1],
            "default_level": regs[base + 2],
            "night_level": regs[base + 3],
            "fade_on_ms": regs[base + 4],
            "fade_off_ms": regs[base + 5],
            "output_curve": regs[base + 6],
        }
    for index in range(regs[5002]):
        base = BINDING_BASE + index * BINDING_STRIDE
        typ = CODE_TYPES.get(regs[base + 1], "disabled")
        is_rocker = typ in ROCKER_TYPES
        item = {
            "type": typ,
            "hold_step": regs[base + 6],
            "hold_period_ms": regs[base + 7],
        }
        if regs[base + 2] == TARGET_GROUP:
            item["target"] = "rgbw"
        else:
            item["target"] = f"channel{regs[base + 3]}"
        if is_rocker:
            item["up"] = f"DI{regs[base + 4]}"
            item["down"] = f"DI{regs[base + 5]}"
        else:
            item["input"] = f"DI{regs[base + 4]}"
        result["bindings"].append(item)
    return result


def read_device(client: ModbusClient) -> Dict[int, int]:
    regs: Dict[int, int] = {}
    for address, count in [(0, 2), (5002, 7)]:
        values = client.read_holding(address, count)
        regs.update({address + i: value for i, value in enumerate(values)})
    for channel in range(4):
        base = PROFILE_BASE + channel * PROFILE_STRIDE
        values = client.read_holding(base, PROFILE_STRIDE)
        regs.update({base + i: value for i, value in enumerate(values)})
    count = regs.get(5002, 0)
    for index in range(min(count, 8)):
        base = BINDING_BASE + index * BINDING_STRIDE
        for offset in range(0, BINDING_STRIDE, 8):
            values = client.read_holding(base + offset, 8)
            regs.update({base + offset + i: value for i, value in enumerate(values)})
    return regs


def write_device(client: ModbusClient, regs: Dict[int, int], save: bool = True) -> None:
    # Step 1: Temporarily set binding count to 0 to bypass validation
    client.write_single(5002, 0)
    # Step 2: Write channel profiles while bindings are disabled.
    for channel in range(4):
        base = PROFILE_BASE + channel * PROFILE_STRIDE
        client.write_multiple(base, [regs[base + off] for off in range(PROFILE_STRIDE)])
    # Step 3: Disable all 8 binding slots
    for index in range(8):
        base = BINDING_BASE + index * BINDING_STRIDE
        client.write_single(base, 0)
    # Step 4: Write configuration for all 8 binding slots
    for index in range(8):
        base = BINDING_BASE + index * BINDING_STRIDE
        client.write_multiple(base + 1, [regs[base + off] for off in range(1, 16)])
        client.write_multiple(base + 16, [regs[base + off] for off in range(16, 32)])
    # Step 5: Write correct enabled flag for each binding
    for index in range(8):
        base = BINDING_BASE + index * BINDING_STRIDE
        enabled = regs[base]
        client.write_single(base, enabled)
    # Step 6: Write the actual binding count and other validation-sensitive input parameters
    client.write_multiple(5002, [regs[a] for a in range(5002, 5009)])
    # Step 7: Write Modbus address and serial configuration
    client.write_single(0, regs[0])
    client.slave = regs[0]
    client.write_single(1, regs[1])
    cfg = decode_port_cfg(regs[1])
    client.reconfigure(int(cfg["baud"]), str(cfg["parity"]), int(cfg["stop_bits"]))
    if save:
        client.write_single(5000, 1)
        time.sleep(0.2)


def open_client(args: argparse.Namespace, slave: Optional[int] = None) -> ModbusClient:
    return ModbusClient(args.port, slave if slave is not None else args.slave,
                        args.baud, args.timeout, args.parity, args.stop_bits)


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("write", "verify"):
        p = sub.add_parser(name)
        p.add_argument("--port", required=True)
        p.add_argument("--slave", type=int, default=34)
        p.add_argument("--baud", type=int, default=57600)
        p.add_argument("--parity", choices=["N", "E", "O"], default="N")
        p.add_argument("--stop-bits", type=int, choices=[1, 2], default=2)
        p.add_argument("--timeout", type=float, default=1.0)
        p.add_argument("--config", required=True)
    for name in ("read", "dump"):
        p = sub.add_parser(name)
        p.add_argument("--port", required=True)
        p.add_argument("--slave", type=int, default=34)
        p.add_argument("--baud", type=int, default=57600)
        p.add_argument("--parity", choices=["N", "E", "O"], default="N")
        p.add_argument("--stop-bits", type=int, choices=[1, 2], default=2)
        p.add_argument("--timeout", type=float, default=1.0)
        p.add_argument("--output")
    args = parser.parse_args(argv)

    if args.command in ("write", "verify"):
        wanted = normalize_config(yaml.safe_load(Path(args.config).read_text()) or {})
        client = open_client(args)
        try:
            if args.command == "write":
                write_device(client, wanted)
            got = read_device(client)
        finally:
            client.close()
        comparable = {k: wanted[k] for k in got.keys() if k in wanted}
        if got != comparable:
            missing = {k: (wanted.get(k), got.get(k)) for k in sorted(set(wanted) | set(got)) if wanted.get(k) != got.get(k)}
            raise SystemExit(f"verification failed: {missing}")
        print("verification ok")
        return 0

    client = open_client(args)
    try:
        regs = read_device(client)
    finally:
        client.close()
    text = yaml.safe_dump(regs_to_yaml(regs), sort_keys=False)
    if args.output:
        Path(args.output).write_text(text)
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
