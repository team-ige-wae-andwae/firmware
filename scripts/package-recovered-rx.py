"""Package verified ESP32-S3 images from read-only device dumps; never opens a port."""
import argparse
import hashlib
import json
from pathlib import Path

from esptool.bin_image import LoadFirmwareImage

p = argparse.ArgumentParser()
p.add_argument('--app-dump', type=Path, required=True)
p.add_argument('--boot-dump', type=Path, required=True)
a = p.parse_args()
repo = Path(__file__).resolve().parents[1]
out = repo / 'prebuilt/rx-2.0.1-recovered'

def verified_image(data):
    image = LoadFirmwareImage('esp32s3', data)
    assert image.append_digest and image.stored_digest == image.calc_digest
    assert image.checksum == image.calculate_checksum()
    return data[:image.data_length + 32]

app_dump = a.app_dump.read_bytes()
boot_dump = a.boot_dump.read_bytes()
assert len(app_dump) == 0x100000 and len(boot_dump) == 0xb000
app = verified_image(app_dump)
boot = verified_image(boot_dump[:0xa000])
desc = app[0x20:0x120]
assert int.from_bytes(desc[:4], 'little') == 0xabcd5432
version = desc[16:48].split(b'\0')[0].decode()
elf_sha = desc[144:176].hex()
assert version == '2.0.1-rx-b64'
assert elf_sha == 'f4ac3fec62a1bff580d1843c9210bedccf77943ec97ce9f26bd2a808e48db123'
partition = boot_dump[0xa000:0xb000]
assert partition[:2] == b'\xaa\x50'
files = {'bootloader/bootloader.bin': boot,
         'partition_table/partition-table.bin': partition,
         'csi_recv.bin': app}
entries = []
for name, data in files.items():
    dest = out / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)
    entries.append({'path': name, 'bytes': len(data),
                    'sha256': hashlib.sha256(data).hexdigest()})
(out / 'SHA256SUMS').write_text(''.join(
    f"{e['sha256']}  {e['path']}\n" for e in entries), encoding='ascii')
manifest = {
    'firmware_version': version,
    'provenance': 'Read-only recovery from an operating RX on 2026-11-17; not a source rebuild.',
    'project': 'csi_recv', 'target': 'esp32s3', 'idf_version': 'v6.0.1',
    'compile_time': '2026-10-15 18:50:13', 'app_elf_sha256': elf_sha,
    'source_status': 'Matching 2.0.1 source and ELF not found. csi_recv/ remains 2.0.0; do not claim correspondence.',
    'verified': ['App and bootloader image checksums and embedded SHA256 digests',
                 'App version and full ELF hash match the connected RX',
                 'NVS and RF calibration data excluded from package'],
    'hardware_validation': 'Observed boot on connected RX; no TX traffic, reflash, throughput or loss test performed.',
    'flash_settings': {'flash_mode': 'dio', 'flash_freq': '80m', 'flash_size': '2MB'},
    'flash_files': {'0x0': 'bootloader/bootloader.bin',
                    '0xa000': 'partition_table/partition-table.bin', '0x20000': 'csi_recv.bin'},
    'binary_files': entries,
    'dump_sha256': {'app_partition': hashlib.sha256(app_dump).hexdigest(),
                    'boot_and_partition': hashlib.sha256(boot_dump).hexdigest()},
}
(out / 'recovery-info.json').write_text(
    json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
(out / 'flash_args').write_text(
    '--flash-mode dio --flash-freq 80m --flash-size 2MB\n'
    '0x0 bootloader/bootloader.bin\n0xa000 partition_table/partition-table.bin\n'
    '0x20000 csi_recv.bin\n', encoding='ascii')
print(json.dumps(manifest, ensure_ascii=False, indent=2))
