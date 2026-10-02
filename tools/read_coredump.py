"""Read the device's actual partition table and export its latest crash over USB.

Read-only flash operations. Entering the ROM bootloader resets/stops the app;
close PlatformIO Monitor first. No firmware, settings or crash data are erased.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def find_coredump(table):
    parts = []
    verified = False
    for pos in range(0, len(table) - 31, 32):
        entry = table[pos:pos + 32]
        if entry[:2] == b'\xeb\xeb':
            if entry[16:] != hashlib.md5(table[:pos]).digest():
                raise ValueError('Partition table checksum mismatch')
            verified = True
            break
        if entry[:2] != b'\xaa\x50':
            break
        _, kind, subtype, address, size, _, flags = struct.unpack('<HBBII16sI', entry)
        if address < 0x9000 or address % 4096 or not size or size % 4096 or address + size > 0x1000000:
            raise ValueError('Invalid partition bounds for this 16 MB board')
        parts.append((kind, subtype, address, size, flags))
    if not verified:
        raise ValueError('Missing partition table checksum')
    ordered = sorted(parts, key=lambda p: p[2])
    if any(a[2] + a[3] > b[2] for a, b in zip(ordered, ordered[1:])):
        raise ValueError('Overlapping partitions')
    dumps = [p for p in parts if p[:2] == (1, 3)]
    if len(dumps) != 1:
        raise ValueError('No unique core dump partition; install the new partition table over USB first')
    _, _, address, size, flags = dumps[0]
    if flags & 1:
        raise ValueError('Encrypted core dump requires on-device decryption; raw USB export is unsupported')
    return address, size


def extract_dump(data):
    if len(data) < 8:
        raise ValueError('Truncated crash partition')
    size, version = struct.unpack_from('<II', data)
    if size in (0, 0xffffffff):
        raise ValueError('No saved crash')
    if size < 28 or size > len(data):
        raise ValueError('Incomplete or corrupt crash record')
    dump = data[:size]
    if version != 0x00090102:  # ESP32-S3, ELF, CRC32 (ESP-IDF 5.5).
        raise ValueError(f'Unsupported core dump version: 0x{version:08x}')
    if zlib.crc32(dump[:-4]) != struct.unpack_from('<I', dump, size - 4)[0]:
        raise ValueError('Crash checksum mismatch; raw partition retained for investigation')
    return dump


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='USB serial port, e.g. COM5')
    parser.add_argument('--output', required=True, type=Path, help='New output file, e.g. .artifacts/crash.bin')
    parser.add_argument('--esptool', type=Path, default=ROOT / '.tools/platformio/packages/tool-esptoolpy/esptool.py')
    args = parser.parse_args()
    raw_path = args.output.with_suffix(args.output.suffix + '.partition.bin')
    if args.output.exists() or raw_path.exists():
        parser.error('Output already exists; choose a new name to preserve previous evidence')
    if not args.esptool.is_file():
        parser.error('esptool.py not found; build with PlatformIO first or provide --esptool')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    def read(address, size, path):
        subprocess.run([sys.executable, str(args.esptool), '--chip', 'esp32s3', '--port', args.port,
                        '--after', 'no_reset', 'read_flash', hex(address), hex(size), str(path)], check=True)
    print('Close Monitor first. The device will enter download mode; reset it manually after export.')
    try:
        with tempfile.TemporaryDirectory() as folder:
            table_path = Path(folder) / 'partitions.bin'
            read(0x8000, 0x1000, table_path)
            address, size = find_coredump(table_path.read_bytes())
            read(address, size, raw_path)
        dump = extract_dump(raw_path.read_bytes())
        with args.output.open('xb') as stream:
            stream.write(dump)
        print(f'Saved {len(dump)} CRC-checked bytes: {args.output}')
        print(f'Raw partition: {raw_path}')
        print('Decode using esp-coredump and the exact firmware.elf from the crashed build; see docs/crash-diagnostics.md.')
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\nNo flash data erased. Reset the device to leave download mode.\n')


if __name__ == '__main__':
    main()
