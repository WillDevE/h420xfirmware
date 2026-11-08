#!/usr/bin/env python3
"""
Independent offline verifier for the H420X clean-room application image.

This tool deliberately does not use the firmware sources or linker map. It
parses the ELF file directly, reconstructs the loadable image, and checks the
boot contract, USB descriptors, HID report descriptors and DFU padding.

Usage:
    python verify_image.py [--elf build/h420x_m0.elf]
                           [--bin build/h420x_m0_app.bin]

Exit code 0 = all checks passed, 1 = one or more checks failed.

SPDX-License-Identifier: GPL-3.0-or-later
"""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

APP_BASE = 0x08004000
APP_END = 0x0800BD00
APP_SIZE = APP_END - APP_BASE
SRAM_BASE = 0x20000000
STACK_TOP = 0x20000EF0
IRQ_COUNT = 68
VECTOR_COUNT = 16 + IRQ_COUNT

VID = 0x256C
PID = 0x0064

EXPECTED_REPORT_BITS = {
    "vendor": {8: 88},
    "digitizer": {10: 72},
    "keyboard": {3: 56},
}

# Canonical vendor-defined report descriptor: usage page 0xFF00, usage 1,
# application collection, report ID 8, one 88-bit input field.
VENDOR_REPORT_DESCRIPTOR = bytes.fromhex(
    "06 00 FF 09 01 A1 01 85 08 75 58 95 01 09 01 81 02 C0"
)

DIGITIZER_REPORT_DESCRIPTOR = bytes.fromhex(
    "05 0D 09 02 A1 01 85 0A 09 20 A1 01 09 42 09 44 09 45 09 3C "
    "15 00 25 01 75 01 95 06 81 02 09 32 75 01 95 01 81 02 81 03 "
    "05 01 09 30 09 31 55 0D 65 33 26 FF 7F 35 00 46 00 08 75 10 "
    "95 02 81 02 05 0D 09 30 26 FF 1F 75 10 95 01 81 02 09 3D 09 3E "
    "15 81 25 7F 75 08 95 02 81 02 C0 C0"
)

KEYBOARD_REPORT_DESCRIPTOR = bytes.fromhex(
    "05 01 09 06 A1 01 85 03 05 07 19 E0 29 E7 15 00 25 01 75 01 "
    "95 08 81 02 05 07 19 00 29 FF 26 FF 00 75 08 95 06 81 00 C0"
)

# Drop-in descriptor contract. These are ordinary USB descriptor structures;
# every field is pinned to the stock H420X/T223 values so that host-side
# enumeration is indistinguishable. The report descriptors are embedded in
# this configuration through their wDescriptorLength values.
DEVICE_DESCRIPTOR = bytes.fromhex(
    "12 01 00 02 00 00 00 08 6C 25 64 00 12 01 01 02 03 01"
)

CONFIG_DESCRIPTOR = bytes.fromhex(
    "09 02 54 00 03 01 00 A0 F0 "
    "09 04 00 00 01 03 01 02 00 09 21 10 01 00 01 22 12 00 07 05 81 03 40 00 01 "
    "09 04 01 00 01 03 01 02 00 09 21 10 01 00 01 22 5D 00 07 05 82 03 10 00 01 "
    "09 04 02 00 01 03 00 00 00 09 21 10 01 00 01 22 28 00 07 05 83 03 40 00 08"
)

# OTD's upstream Huion H420X configuration matches string descriptor 201
# against this pattern and reads string 200 during initialization.
OTD_DEVICE_STRING_201 = r"HUION_(T210|T223)_\d{6}"


class CheckFailed(Exception):
    pass


class Report:
    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0

    def ok(self, message: str) -> None:
        self.passed += 1
        print(f"  PASS  {message}")

    def fail(self, message: str) -> None:
        self.failed += 1
        print(f"  FAIL  {message}")

    def check(self, condition: bool, message: str) -> None:
        if condition:
            self.ok(message)
        else:
            self.fail(message)


class Elf32:
    """Minimal little-endian ELF32 reader (sections + symbols only)."""

    SHT_SYMTAB = 2
    SHT_NOBITS = 8
    SHF_ALLOC = 0x2

    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        if len(self.data) < 52 or self.data[:4] != b"\x7fELF":
            raise CheckFailed(f"{path} is not an ELF file")
        if self.data[4] != 1 or self.data[5] != 1:
            raise CheckFailed("only ELF32 little-endian images are supported")

        fields = struct.unpack_from("<HHIIIIIHHHHHH", self.data, 16)
        (self.e_type, self.e_machine, _version, self.entry, _phoff,
         self.e_shoff, _flags, _ehsize, _phentsize, _phnum,
         self.e_shentsize, self.e_shnum, self.e_shstrndx) = fields

        if self.e_type != 2:
            raise CheckFailed("ELF is not an executable (ET_EXEC)")
        if self.e_machine != 40:
            raise CheckFailed("ELF is not ARM (EM_ARM)")

        self.sections = []
        for index in range(self.e_shnum):
            offset = self.e_shoff + index * self.e_shentsize
            sh = struct.unpack_from("<IIIIIIIIII", self.data, offset)
            self.sections.append({
                "name_offset": sh[0],
                "type": sh[1],
                "flags": sh[2],
                "addr": sh[3],
                "offset": sh[4],
                "size": sh[5],
                "link": sh[6],
                "info": sh[7],
                "addralign": sh[8],
                "entsize": sh[9],
            })

        shstr = self.sections[self.e_shstrndx]
        shstr_data = self.data[shstr["offset"]:shstr["offset"] + shstr["size"]]
        for section in self.sections:
            section["name"] = self._string(shstr_data, section["name_offset"])

        self.symbols: dict[str, dict] = {}
        for section in self.sections:
            if section["type"] != self.SHT_SYMTAB:
                continue
            strtab = self.sections[section["link"]]
            strtab_data = self.data[strtab["offset"]:strtab["offset"] + strtab["size"]]
            count = section["size"] // 16
            for index in range(count):
                offset = section["offset"] + index * 16
                st_name, st_value, st_size, st_info, _other, st_shndx = \
                    struct.unpack_from("<IIIBBH", self.data, offset)
                if st_name == 0:
                    continue
                name = self._string(strtab_data, st_name)
                self.symbols[name] = {
                    "value": st_value,
                    "size": st_size,
                    "type": st_info & 0xF,
                    "bind": st_info >> 4,
                    "shndx": st_shndx,
                }

        # Program headers carry the load address (LMA), which differs from
        # the virtual address for .data copied from flash into SRAM.
        self.segments = []
        for index in range(_phnum):
            offset = _phoff + index * _phentsize
            p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
                struct.unpack_from("<IIIIIIII", self.data, offset)
            self.segments.append({
                "type": p_type,
                "offset": p_offset,
                "vaddr": p_vaddr,
                "paddr": p_paddr,
                "filesz": p_filesz,
                "memsz": p_memsz,
                "flags": p_flags,
                "align": p_align,
            })

    @staticmethod
    def _string(table: bytes, offset: int) -> str:
        end = table.find(b"\0", offset)
        if end < 0:
            end = len(table)
        return table[offset:end].decode("ascii", errors="replace")

    def section(self, name: str) -> dict:
        for section in self.sections:
            if section["name"] == name:
                return section
        raise CheckFailed(f"missing ELF section {name!r}")

    def symbol(self, name: str) -> int:
        if name not in self.symbols:
            raise CheckFailed(f"missing ELF symbol {name!r}")
        return self.symbols[name]["value"]

    def read(self, address: int, size: int) -> bytes:
        for section in self.sections:
            start = section["addr"]
            end = start + section["size"]
            if section["type"] == self.SHT_NOBITS:
                continue
            if start <= address and address + size <= end:
                offset = section["offset"] + (address - start)
                return self.data[offset:offset + size]
        raise CheckFailed(f"address 0x{address:08X} (+{size}) is not in a loadable section")


def hid_items(data: bytes):
    """Yield (type, tag, value, size_bytes, offset) for HID short items."""
    index = 0
    while index < len(data):
        prefix = data[index]
        size = {0: 0, 1: 1, 2: 2, 3: 4}[prefix & 0x3]
        item_type = (prefix >> 2) & 0x3
        tag = (prefix >> 4) & 0xF
        if index + 1 + size > len(data):
            raise CheckFailed(f"truncated HID item at offset {index}")
        raw = data[index + 1:index + 1 + size]
        if size == 0:
            value = 0
        elif size == 1:
            value = raw[0]
        elif size == 2:
            value = struct.unpack("<H", raw)[0]
        else:
            value = struct.unpack("<I", raw)[0]
        yield item_type, tag, value, size, index
        index += 1 + size


def report_bits(data: bytes) -> dict[int, int]:
    """Compute input-report bit length per report ID from a HID descriptor."""
    bits: dict[int, int] = {}
    report_id = 0
    report_size = 0
    report_count = 0
    for item_type, tag, value, _size, _offset in hid_items(data):
        if item_type == 1:  # Global
            if tag == 7:    # Report Size
                report_size = value
            elif tag == 8:  # Report ID
                report_id = value
            elif tag == 9:  # Report Count
                report_count = value
        elif item_type == 0 and tag == 8:  # Main: Input
            bits[report_id] = bits.get(report_id, 0) + report_size * report_count
    return bits


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_descriptor_tlv(data: bytes):
    descriptors = []
    index = 0
    while index < len(data):
        length = data[index]
        if length < 2 or index + length > len(data):
            raise CheckFailed(f"malformed descriptor length {length} at offset {index}")
        descriptors.append((data[index + 1], data[index:index + length], index))
        index += length
    return descriptors


def verify(elf_path: Path, bin_path: Path, expected_bcd: int = 0x0112) -> int:
    report = Report()
    elf = Elf32(elf_path)
    image = elf_path.parent.joinpath(elf_path.stem + "_raw.bin")
    if not image.exists():
        image = None

    print(f"ELF: {elf_path}")
    print(f"BIN: {bin_path}")

    # ------------------------------------------------------------------
    # Boot contract
    # ------------------------------------------------------------------
    print("\nBoot contract")
    vector_section = elf.section(".isr_vector")
    report.check(vector_section["addr"] == APP_BASE,
                 f"vector table linked at 0x{APP_BASE:08X}")
    report.check(vector_section["size"] >= VECTOR_COUNT * 4,
                 f"vector table holds {VECTOR_COUNT} entries")

    table = elf.read(APP_BASE, VECTOR_COUNT * 4)
    sp, reset = struct.unpack_from("<II", table, 0)
    report.check(sp == STACK_TOP, f"initial SP is 0x{sp:08X} (expected 0x{STACK_TOP:08X})")
    report.check(sp % 8 == 0, "initial SP is 8-byte aligned")
    report.check(SRAM_BASE < sp <= SRAM_BASE + 0x1000,
                 "initial SP lies inside the 4 KiB SRAM window")
    report.check(reset & 1 == 1, f"reset vector 0x{reset:08X} is a Thumb address")
    report.check(APP_BASE <= (reset & ~1) < APP_END,
                 "reset vector points inside the application slot")

    for index in range(2, VECTOR_COUNT):
        handler = struct.unpack_from("<I", table, index * 4)[0]
        if handler == 0:
            continue
        if not (APP_BASE <= (handler & ~1) < APP_END):
            report.fail(f"IRQ {index - 16} handler 0x{handler:08X} is outside the app")
            break
    else:
        report.ok("all non-zero exception/IRQ handlers point into the app")

    report.check(elf.symbol("_estack") == STACK_TOP, "_estack matches the stock SRAM top")
    stack_bottom = elf.symbol("_stack_bottom")
    stack_top = elf.symbol("_stack_top")
    report.check(stack_bottom <= stack_top <= STACK_TOP, "stack reservation is inside SRAM")

    # ------------------------------------------------------------------
    # Loadable image bounds
    # ------------------------------------------------------------------
    print("\nImage layout")
    high_water = APP_BASE
    for segment in elf.segments:
        if segment["type"] != 1 or segment["filesz"] == 0:  # PT_LOAD
            continue
        start = segment["paddr"]
        end = start + segment["filesz"]
        report.check(APP_BASE <= start < APP_END and end <= APP_END,
                     f"load segment 0x{start:08X}..0x{end:08X} fits the app slot")
        high_water = max(high_water, end)

    for section in elf.sections:
        if not (section["flags"] & elf.SHF_ALLOC) or section["size"] == 0:
            continue
        start = section["addr"]
        end = start + section["size"]
        if section["type"] == elf.SHT_NOBITS or start >= SRAM_BASE:
            report.check(SRAM_BASE <= start < STACK_TOP and end <= STACK_TOP,
                         f"{section['name']} is inside SRAM")
        else:
            report.check(APP_BASE <= start < APP_END and end <= APP_END,
                         f"{section['name']} is inside the app slot")

    report.check(high_water - APP_BASE <= APP_SIZE,
                 f"loadable image is {high_water - APP_BASE} bytes (limit {APP_SIZE})")

    # ------------------------------------------------------------------
    # Device descriptor
    # ------------------------------------------------------------------
    print("\nDevice descriptor")
    device_addr = elf.symbol("h420x_device_descriptor")
    device = elf.read(device_addr, 18)
    (b_length, b_type, bcd_usb, dev_class, dev_subclass, dev_protocol,
     ep0_size, vendor, product, bcd_device, i_man, i_prod, i_serial,
     num_configs) = struct.unpack("<BBHBBBBHHHBBBB", device)

    report.check(b_length == 18 and b_type == 1, "device descriptor length/type")
    report.check(bcd_usb == 0x0200, "bcdUSB is 2.00")
    report.check(dev_class == 0 and dev_subclass == 0 and dev_protocol == 0,
                 "device class is defined per interface")
    report.check(ep0_size == 8, "EP0 max packet size is 8 (stock value)")
    report.check(vendor == VID and product == PID,
                 f"VID:PID is {vendor:04X}:{product:04X} (OTD-compatible)")
    report.check(bcd_device == expected_bcd,
                 f"bcdDevice is {bcd_device >> 8}.{bcd_device & 0xFF:02d} (expected 0x{expected_bcd:04X})")
    report.check(num_configs == 1, "exactly one configuration")
    report.check(all(1 <= value <= 3 for value in (i_man, i_prod, i_serial)),
                 "string descriptor indices are populated")
    expected_device = DEVICE_DESCRIPTOR[:12] + struct.pack("<H", expected_bcd) + DEVICE_DESCRIPTOR[14:]
    report.check(device == expected_device,
                 "device descriptor matches the stock contract at the expected bcdDevice")

    # ------------------------------------------------------------------
    # Configuration descriptor
    # ------------------------------------------------------------------
    print("\nConfiguration descriptor")
    config_addr = elf.symbol("h420x_config_descriptor")
    config = elf.read(config_addr, 9)
    (cfg_len, cfg_type, total_len, num_interfaces, cfg_value, i_config,
     attributes, max_power) = struct.unpack("<BBHBBBBB", config)
    report.check(cfg_len == 9 and cfg_type == 2, "configuration descriptor header")
    report.check(total_len == 84, f"wTotalLength is {total_len} (expected 84)")
    report.check(num_interfaces == 3, "three HID interfaces")
    report.check(cfg_value == 1, "bConfigurationValue is 1")
    report.check(attributes == 0xA0,
                 "bmAttributes is 0xA0 (bus-powered, remote wakeup; stock value)")
    report.check(max_power == 0xF0,
                 f"bMaxPower is {max_power * 2} mA (stock value)")

    full_config = elf.read(config_addr, total_len)
    report.check(full_config == CONFIG_DESCRIPTOR,
                 "configuration descriptor is byte-identical to the stock contract")
    descriptors = parse_descriptor_tlv(full_config)
    report.check(descriptors[0][0] == 2, "configuration descriptor comes first")

    interface_descs = [(raw, off) for kind, raw, off in descriptors if kind == 4]
    endpoint_descs = [(raw, off) for kind, raw, off in descriptors if kind == 5]
    report.check(len(interface_descs) == 3, "three interface descriptors")
    report.check(len(endpoint_descs) == 3, "three endpoint descriptors")

    expected = [
        # (interface, endpoint, packet size, interval, report length symbol)
        (0, 0x81, 64, 1, "h420x_report_vendor", 18),
        (1, 0x82, 16, 1, "h420x_report_digitizer", 93),
        (2, 0x83, 64, 8, "h420x_report_keyboard", 40),
    ]

    for (itf, off), (itf_num, ep_addr, packet, interval, symbol, report_len) in zip(
            interface_descs, expected):
        itf = full_config[off:off + itf[0]]
        (length, desc_type, number, alt, num_eps, itf_class, itf_subclass,
         itf_protocol, itf_string) = struct.unpack("<BBBBBBBBB", itf)
        report.check(length == 9 and desc_type == 4, f"interface {number} header")
        report.check(number == itf_num, f"interface number {itf_num}")
        report.check(alt == 0, f"interface {itf_num} alternate setting 0")
        report.check(num_eps == 1, f"interface {itf_num} has one endpoint")
        report.check(itf_class == 3, f"interface {itf_num} is HID")

        # The HID descriptor and endpoint follow the interface descriptor.
        hid = full_config[off + 9:off + 18]
        (hid_len, hid_type, hid_ver, country, num_hid, hid_desc_type,
         hid_desc_len) = struct.unpack("<BBHBBBH", hid)
        report.check(hid_len == 9 and hid_type == 0x21, f"interface {itf_num} HID descriptor")
        report.check(hid_ver == 0x0110, f"interface {itf_num} HID 1.10 (stock value)")
        report.check(num_hid == 1 and hid_desc_type == 0x22,
                     f"interface {itf_num} exposes one report descriptor")
        report.check(hid_desc_len == report_len,
                     f"interface {itf_num} report descriptor length is {hid_desc_len}")

        ep = full_config[off + 18:off + 25]
        (ep_len, ep_type, address, ep_attributes, ep_packet,
         ep_interval) = struct.unpack("<BBBBHB", ep)
        report.check(ep_len == 7 and ep_type == 5, f"interface {itf_num} endpoint descriptor")
        report.check(address == ep_addr, f"interface {itf_num} endpoint 0x{address:02X}")
        report.check(ep_attributes == 3, f"interface {itf_num} interrupt endpoint")
        report.check(ep_packet == packet, f"interface {itf_num} packet size {ep_packet}")
        report.check(ep_interval == interval, f"interface {itf_num} interval {ep_interval} ms")

    # ------------------------------------------------------------------
    # HID report descriptors
    # ------------------------------------------------------------------
    print("\nHID report descriptors")
    report_symbols = {
        "vendor": ("h420x_report_vendor", VENDOR_REPORT_DESCRIPTOR),
        "digitizer": ("h420x_report_digitizer", DIGITIZER_REPORT_DESCRIPTOR),
        "keyboard": ("h420x_report_keyboard", KEYBOARD_REPORT_DESCRIPTOR),
    }
    for name, (symbol, canonical) in report_symbols.items():
        address = elf.symbol(symbol)
        size = elf.symbols[symbol]["size"]
        data = elf.read(address, size)
        report.ok(f"{name}: {size} bytes at 0x{address:08X}")
        report.check(data == canonical,
                     f"{name}: byte-identical to the stock report descriptor")
        bits = report_bits(data)
        report.check(bits == EXPECTED_REPORT_BITS[name],
                     f"{name}: input bits per report ID {bits}")

    # ------------------------------------------------------------------
    # OTD detection strings
    # ------------------------------------------------------------------
    print("\nOTD detection strings")
    for symbol, label in (("h420x_string_200", "200"),
                          ("h420x_string_201", "201")):
        address = elf.symbol(symbol)
        size = elf.symbols[symbol]["size"]
        raw = elf.read(address, size)
        text = raw.split(b"\0", 1)[0].decode("ascii", errors="replace")
        report.check(bool(text), f"string {label} is present")
        if label == "201":
            report.check(re.fullmatch(OTD_DEVICE_STRING_201, text) is not None,
                         f"string 201 matches OTD pattern: {text!r}")

    # ------------------------------------------------------------------
    # DFU payload
    # ------------------------------------------------------------------
    print("\nDFU payload")
    payload = bin_path.read_bytes()
    report.check(len(payload) == APP_SIZE,
                 f"payload is exactly {APP_SIZE} bytes (0x{APP_SIZE:X})")

    if len(payload) == APP_SIZE:
        payload_sp, payload_reset = struct.unpack_from("<II", payload, 0)
        report.check(payload_sp == sp, "payload SP matches the ELF vector table")
        report.check(payload_reset == reset, "payload reset vector matches the ELF")

        if image is not None:
            raw = image.read_bytes()
            report.check(raw == payload[:len(raw)],
                         "raw binary is the prefix of the padded DFU payload")
            report.check(all(byte == 0xFF for byte in payload[len(raw):]),
                         "unused tail of the DFU payload is 0xFF")

    print(f"\nSHA-256: {sha256(payload)}")
    print(f"Payload size: {len(payload)} bytes")

    print(f"\n{report.passed} passed, {report.failed} failed")
    return 0 if report.failed == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--elf",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "firmware" / "build" / "h420x_m0.elf",
    )
    parser.add_argument(
        "--bin",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "firmware" / "build" / "h420x_m0_app.bin",
    )
    parser.add_argument(
        "--bcd-device",
        type=lambda value: int(value, 0),
        default=0x0112,
        help="expected bcdDevice (default 0x0112; use 0x0113 for the cache-bust image)",
    )
    args = parser.parse_args()

    try:
        return verify(args.elf.resolve(), args.bin.resolve(), args.bcd_device)
    except CheckFailed as error:
        print(f"FAIL  {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
