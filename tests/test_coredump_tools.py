import hashlib
from pathlib import Path
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from read_coredump import extract_dump, find_coredump


def table(entries):
    data = b''.join(struct.pack('<HBBII16sI', 0x50aa, *entry) for entry in entries)
    return data + b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(data).digest()


class CoreDumpToolsTests(unittest.TestCase):
    def test_actual_partition_not_assumed_offset(self):
        self.assertEqual(find_coredump(table([(1, 3, 0xfc0000, 0x40000, b'coredump', 0)])), (0xfc0000, 0x40000))
        self.assertEqual(find_coredump(table([(1, 3, 0x9000, 0x10000, b'coredump', 0)])), (0x9000, 0x10000))

    def test_reject_old_table_corruption_overlap_and_encryption(self):
        for data in (table([(1, 0x82, 0x820000, 0x7e0000, b'storage', 0)]),
                     table([(1, 3, 0xfc0000, 0x40000, b'coredump', 1)]),
                     table([(1, 3, 0xfc0000, 0x80000, b'coredump', 0)]),
                     table([(1, 3, 0xfc0000, 0x40000, b'coredump', 0), (1, 0, 0xfc0000, 0x1000, b'nvs', 0)]),
                     table([(1, 3, 0xfc0000, 0x40000, b'coredump', 0)])[:-1] + b'X',
                     b'\xff' * 4096):
            with self.subTest(data=data[:16]), self.assertRaises(ValueError):
                find_coredump(data)

    def test_trim_padding_and_check_crc(self):
        payload = struct.pack('<II', 36, 0x90102) + b'a' * 24
        dump = payload + struct.pack('<I', zlib.crc32(payload))
        self.assertEqual(extract_dump(dump + b'\xff' * 100), dump)
        for broken in (b'', b'\xff' * 100, bytes(100), dump[:30], dump[:-1] + b'X',
                       struct.pack('<II', 28, 0x90103) + bytes(20)):
            with self.subTest(data=broken), self.assertRaises(ValueError):
                extract_dump(broken)


if __name__ == '__main__':
    unittest.main()
