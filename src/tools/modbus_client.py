"""Small Modbus RTU client used by local firmware tools."""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial is required: pip install pyserial") from exc


BAUD_CODES = {9600: 0, 19200: 1, 38400: 2, 57600: 3, 115200: 4}
CODE_BAUDS = {v: k for k, v in BAUD_CODES.items()}
PARITY_CODES = {"N": 0, "E": 1, "O": 2}
CODE_PARITY = {v: k for k, v in PARITY_CODES.items()}


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def port_cfg(baud: int, parity: str, stop_bits: int) -> int:
    if baud not in BAUD_CODES:
        raise ValueError(f"unsupported baud: {baud}")
    parity = parity.upper()
    if parity not in PARITY_CODES:
        raise ValueError(f"unsupported parity: {parity}")
    if stop_bits not in (1, 2):
        raise ValueError(f"unsupported stop_bits: {stop_bits}")
    return BAUD_CODES[baud] | (PARITY_CODES[parity] << 4) | ((stop_bits - 1) << 6)


def decode_port_cfg(value: int) -> Dict[str, object]:
    baud_code = value & 0x0F
    parity_code = (value >> 4) & 0x03
    stop_bits = ((value >> 6) & 0x03) + 1
    return {
        "baud": CODE_BAUDS.get(baud_code, 57600),
        "parity": CODE_PARITY.get(parity_code, "N"),
        "stop_bits": stop_bits,
    }


@dataclass
class ModbusClient:
    port: str
    slave: int
    baud: int = 57600
    timeout: float = 1.0
    parity: str = "N"
    stop_bits: int = 2

    def __post_init__(self) -> None:
        self._open_serial()

    def _open_serial(self) -> None:
        parity = self.parity.upper()
        if parity not in PARITY_CODES:
            raise ValueError(f"unsupported parity: {self.parity}")
        if self.stop_bits not in (1, 2):
            raise ValueError(f"unsupported stop_bits: {self.stop_bits}")
        self.ser = serial.Serial(
            self.port,
            self.baud,
            bytesize=8,
            parity={
                "N": serial.PARITY_NONE,
                "E": serial.PARITY_EVEN,
                "O": serial.PARITY_ODD,
            }[parity],
            stopbits=serial.STOPBITS_ONE if self.stop_bits == 1 else serial.STOPBITS_TWO,
            timeout=self.timeout,
        )

    def close(self) -> None:
        self.ser.close()

    def reconfigure(self, baud: int, parity: str, stop_bits: int) -> None:
        self.ser.close()
        self.baud = baud
        self.parity = parity
        self.stop_bits = stop_bits
        self._open_serial()

    def _request(self, pdu: bytes) -> bytes:
        frame = bytes([self.slave]) + pdu
        frame += crc16(frame).to_bytes(2, "little")
        last_timeout: Optional[TimeoutError] = None
        for attempt in range(3):
            time.sleep(0.15 if attempt else 0.02)
            self.ser.reset_input_buffer()
            self.ser.write(frame)
            self.ser.flush()
            response = self.ser.read(5)
            if len(response) < 5:
                last_timeout = TimeoutError("no Modbus response")
                continue
            if response[0] != self.slave:
                last_timeout = TimeoutError(f"unexpected Modbus slave {response[0]}")
                continue
            if response[1] & 0x80:
                if crc16(response[:-2]) != int.from_bytes(response[-2:], "little"):
                    raise RuntimeError("bad Modbus CRC in exception")
                raise RuntimeError(f"Modbus exception {response[2]}")
            if response[1] != pdu[0]:
                last_timeout = TimeoutError(f"unexpected Modbus function {response[1]}")
                continue
            if response[1] == 0x03:
                response += self.ser.read(response[2])
            elif response[1] in (0x06, 0x10):
                response += self.ser.read(3)
            if len(response) < 5 or crc16(response[:-2]) != int.from_bytes(response[-2:], "little"):
                raise RuntimeError("bad Modbus CRC")
            time.sleep(0.05)
            return response
        assert last_timeout is not None
        raise last_timeout

    def read_holding(self, address: int, count: int) -> List[int]:
        pdu = bytes([0x03]) + address.to_bytes(2, "big") + count.to_bytes(2, "big")
        response = self._request(pdu)
        return [int.from_bytes(response[3 + i * 2 : 5 + i * 2], "big") for i in range(count)]

    def write_single(self, address: int, value: int) -> None:
        pdu = bytes([0x06]) + address.to_bytes(2, "big") + (value & 0xFFFF).to_bytes(2, "big")
        self._request(pdu)

    def write_multiple(self, address: int, values: Iterable[int]) -> None:
        vals = list(values)
        payload = b"".join((v & 0xFFFF).to_bytes(2, "big") for v in vals)
        pdu = bytes([0x10]) + address.to_bytes(2, "big") + len(vals).to_bytes(2, "big") + bytes([len(payload)]) + payload
        self._request(pdu)
