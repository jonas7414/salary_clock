"""Release guard tests: changes that must prevent an unsafe publication."""
import copy
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from release_tools import load_version, read_partitions, validate_layout, binary_slots, parse_size
ROOT=Path(__file__).resolve().parents[1]
class ReleaseTests(unittest.TestCase):
    def test_versions(self):
        from unittest.mock import patch
        for value in ['1.2.0','1.10.0','4294967295.0.0']:
            with patch.object(Path,'read_text',return_value=value):
                self.assertEqual(load_version(),value)
        for value in ['v1.2.0','01.2.0','1.2','1.2.0-rc.1','1.2.0;echo x','4294967296.0.0']:
            with self.subTest(value=value),patch.object(Path,'read_text',return_value=value),self.assertRaises(ValueError):
                load_version()
    def test_layout(self):
        parts=read_partitions(ROOT/'partitions.csv')
        self.assertEqual(validate_layout(parts,parse_size('16MB'))['ota_0']['size'],4*1024**2)
        mutations=[('nvs','offset',0x8000),('nvs','size',0x5000),('ota_1','offset',0x410000),
                   ('ota_1','offset',0x20000),('ota_1','size',0x300000),('otadata','size',0x1000)]
        for name,key,value in mutations:
            broken=copy.deepcopy(parts)
            next(p for p in broken if p['name']==name)[key]=value
            with self.subTest(name=name,key=key),self.assertRaises(ValueError):validate_layout(broken,16*1024**2)
        with self.assertRaises(ValueError):validate_layout(parts,8*1024**2)
        with self.assertRaises(ValueError):validate_layout(parts+[parts[0]],16*1024**2)
    def test_built_table(self):
        data=b''.join(struct.pack('<HBBII16sI',0x50aa,0,0x10+i,0x20000+i*0x400000,0x400000,('ota_'+str(i)).encode(),0) for i in range(2))
        self.assertEqual(binary_slots(data)['ota_1'],(0x420000,0x400000))
        with self.assertRaises(ValueError):binary_slots(data[:32])
        with self.assertRaises(ValueError):binary_slots(b'bad')
if __name__=='__main__':unittest.main()
