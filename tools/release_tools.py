"""Shared release/version/partition validation, also used by PlatformIO."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct

ROOT=Path(__file__).resolve().parents[1]
ENVIRONMENTS={"tdisplay_s3":"firmware.bin","tdisplay_s3_no_psram":"firmware-no-psram.bin"}

def load_version(root=ROOT):
    version=(root/"version.txt").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)",version):
        raise ValueError("version.txt must contain a stable MAJOR.MINOR.PATCH")
    if len(version)>31 or any(int(n)>0xffffffff for n in version.split('.')):
        raise ValueError("Firmware version exceeds the embedded version limits")
    return version

def parse_size(value):
    value=str(value).strip().upper()
    match=re.fullmatch(r"(\d+)(K|M)(?:B)?",value)
    return int(match[1])*(1024 if match[2]=='K' else 1024**2) if match else int(value,0)

def read_partitions(path):
    lines=(line for line in path.read_text(encoding="utf-8").splitlines() if line.strip() and not line.lstrip().startswith('#'))
    return [dict(name=row[0].strip(),type=row[1].strip(),subtype=row[2].strip(),offset=parse_size(row[3]),size=parse_size(row[4])) for row in csv.reader(lines)]

def validate_layout(parts,flash_size):
    last_end=0x9000
    for part in sorted(parts,key=lambda p:p['offset']):
        if part['offset']<last_end or part['offset']%0x1000 or part['size']<=0 or part['size']%0x1000:
            raise ValueError("Partition overlap, invalid size, or alignment")
        if part['type']=='app' and part['offset']%0x10000:
            raise ValueError("App partition must be 64 KiB aligned")
        last_end=part['offset']+part['size']
        if last_end>flash_size:
            raise ValueError("Partition table exceeds the board flash size")
    table={p['name']:p for p in parts}
    if len(table)!=len(parts): raise ValueError("Duplicate partition label")
    for name,kind,subtype in [('nvs','data','nvs'),('otadata','data','ota'),('ota_0','app','ota_0'),('ota_1','app','ota_1')]:
        if name not in table or (table[name]['type'],table[name]['subtype'])!=(kind,subtype):
            raise ValueError("Missing OTA partition: "+name)
    if (table['nvs']['offset'],table['nvs']['size'])!=(0x9000,0x6000):
        raise ValueError("NVS must retain its existing location and size")
    if table['otadata']['size']!=0x2000 or table['ota_0']['size']!=table['ota_1']['size']:
        raise ValueError("Invalid OTA data or A/B slot sizes")
    return table

def binary_slots(binary):
    slots={}
    for offset in range(0,len(binary),32):
        entry=binary[offset:offset+32]
        if len(entry)!=32 or entry[:2]!=b'\xaa\x50': break
        _,kind,subtype,address,size,label,_=struct.unpack('<HBBII16sI',entry)
        if kind==0 and subtype in (0x10,0x11):
            slots[label.split(b'\0')[0].decode('ascii')]=(address,size)
    if set(slots)!={'ota_0','ota_1'}: raise ValueError("Built partition table has no A/B slots")
    return slots

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--tag')
    parser.add_argument('--environment',choices=ENVIRONMENTS)
    parser.add_argument('--output',type=Path,default=ROOT/'.artifacts/release')
    args=parser.parse_args()
    version=load_version()
    if args.tag and args.tag!='v'+version:
        raise SystemExit(f"Tag {args.tag} does not match firmware v{version}")
    if not args.environment:
        print(version);return
    build=ROOT/'.pio/build'/args.environment
    image=(build/'firmware.bin').read_bytes()
    slots=binary_slots((build/'partitions.bin').read_bytes())
    slot_size=min(size for _,size in slots.values())
    if not 0<len(image)<slot_size: raise ValueError("Firmware must be smaller than each OTA slot")
    if image[0]!=0xe9 or struct.unpack_from('<H',image,12)[0]!=9 or struct.unpack_from('<I',image,32)[0]!=0xabcd5432:
        raise ValueError("Not an ESP32-S3 application image")
    embedded=image[48:80].split(b'\0')[0].decode('ascii')
    if embedded!=version: raise ValueError("App descriptor version does not match version.txt")
    description=json.loads((build/'project_description.json').read_text(encoding='utf-8'))
    sdk=json.loads((build/'config/sdkconfig.json').read_text(encoding='utf-8'))
    if not sdk.get('BOOTLOADER_APP_ROLLBACK_ENABLE') or not sdk.get('MBEDTLS_CERTIFICATE_BUNDLE'):
        raise ValueError("Rollback and certificate bundle must be enabled")
    if sdk.get('ESP_TLS_INSECURE') or sdk.get('ESP_TLS_SKIP_SERVER_CERT_VERIFY'):
        raise ValueError("Insecure TLS is forbidden")
    if bool(sdk.get('SPIRAM'))!=(args.environment=='tdisplay_s3'):
        raise ValueError("Wrong PSRAM variant")
    if description.get('project_version')!=version: raise ValueError("CMake project version mismatch")
    args.output.mkdir(parents=True,exist_ok=True)
    name=ENVIRONMENTS[args.environment]
    shutil.copyfile(build/'firmware.bin',args.output/name)
    digest=hashlib.sha256(image).hexdigest()
    (args.output/name.replace('.bin','.sha256')).write_text(f'{digest}  {name}\n',encoding='ascii')
    report=dict(environment=args.environment,version=version,firmware_bytes=len(image),ota_partition_bytes=slot_size,
                remaining_ota_bytes=slot_size-len(image),sha256=digest,slots=slots)
    (args.output/(args.environment+'.json')).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))

if __name__=='__main__': main()
