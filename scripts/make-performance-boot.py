#!/usr/bin/env python3
"""Make a performance control from boot-cepheus-keys.img; no device writes.

Preserves ADT, firmware and userdata. Touch stays disabled. Changes kernel,
bounded Home injection and debug flags only. --fixed-timer=0 is the comparison.
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import tomllib


def make_args(line, fixed):
    tokens = line.split()
    for token in ('sweettouch=0', 'sweetkeys=1', 'sweetfb_noconsole=1', 'sweetusb=2'):
        if tokens.count(token) != 1:
            raise ValueError('unexpected source config: ' + token)
    updates = {'sweethidtest': '90', 'sweethidonce': '1', 'sweetkeyspononly': '1',
               'sweetfb_hz': '30', 'sweetfb_fixedtimer': str(fixed), 'sweetfb_stats': '1',
               'sweetsample': '-', 'sweetiolog': '-', 'sweetnull': '-'}
    remove = set(updates) | {'startup_debug', '-enable_kprintf_spam', 'sweetgpu'}
    result = [t for t in tokens if t.split('=', 1)[0] not in remove]
    result.extend(f'{k}={v}' for k, v in updates.items())
    line = ' '.join(result)
    if len(line) > 1000:
        raise ValueError('args too long')
    return line


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('kernel', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--fixed-timer', type=int, choices=(0, 1), default=1)
    p.add_argument('--touch-probe', action='store_true', help='enable delayed cepheus GPI probe, only in a verified combined kernel')
    p.add_argument('--touch-gpio', action='store_true', help='enable experimental open-drain GPIO I2C; no GPI DMA')
    p.add_argument('--touch-power', action='store_true', help='with GPIO, submit ldoc1 XOB enable-only vote')
    p.add_argument('--soc-probe', action='store_true', help='read-only CPU OSM and gated GPU state probe')
    a = p.parse_args()
    if a.output.exists():
        p.error('refusing to overwrite output')
    kernel = a.kernel.read_bytes()
    for s in (b'SWEET PERF:', b'SWEET BLANK:', b'one-shot Home complete', b'sweetkeyspononly', b'sweetfb_fixedtimer'):
        if s not in kernel:
            p.error(f'performance marker missing: {s!r}')
    if len(kernel) < 2_000_000 or kernel[:4] != bytes.fromhex('cffaedfe'):
        p.error('not a Mach-O kernel')
    if a.soc_probe and (b'CEPHEUS OSM:' not in kernel or b'CEPHEUS GPU:' not in kernel):
        p.error('missing CPU/GPU diagnostic code')
    if a.touch_probe and a.touch_gpio: p.error('choose GPIO or GPI probe, not both')
    if a.touch_power and (not a.touch_gpio or b'CEPHEUS POWER: ldoc1' not in kernel):
        p.error('touch power requires GPIO and ldoc1 enable-only implementation')
    if a.touch_probe or a.touch_gpio:
        subprocess.run(['python3', str(Path(__file__).with_name('verify-touch-kernel.py')), str(a.kernel)], check=True)
        if b'SWEET GPI: SMMU' not in kernel:
            p.error('missing GPI/SMMU diagnostic marker')
        if a.touch_gpio and b'GPIO open-drain I2C' not in kernel: p.error('missing GPIO driver')
    src = a.source.read_bytes()
    if src[:8] != b'ANDROID!': p.error('not Android boot')
    k, _, r, _, second, _, _, page = struct.unpack_from('<8I', src, 8)
    if page != 4096 or second or struct.unpack_from('<I', src, 40)[0]: p.error('unsupported layout')
    off = page + ((k + page - 1) // page) * page
    sizes = struct.pack('<III', k, r, second)
    if src[576:584] != hashlib.sha1(src[page:page + k] + src[off:off + r] + sizes).digest()[:8]:
        p.error('unexpected Aloha checksum')
    with tempfile.TemporaryDirectory() as temp:
        w = Path(temp)
        fat = w / 'fat.img'
        fat.write_bytes(src[off:off + r])
        def copy(x, y):
            subprocess.run(['mcopy', '-o', '-i', str(fat), str(x), str(y)], check=True)
        copy('::/EFI/UniXNU/unixnu.toml', w / 'config')
        copy('::/EFI/UniXNU/Payloads/kernel.macho.adt', w / 'adt')
        before = (w / 'config').read_text()
        line = make_args(tomllib.loads(before)['entry'][0]['args'], a.fixed_timer)
        if a.soc_probe: line += ' cephsoc=1'
        if a.touch_probe or a.touch_gpio:
            line = line.replace('sweettouch=0', 'sweettouch=1') + ' sweettouchdelay=60'
            if a.touch_gpio: line += ' sweettouchgpio=1'
            if a.touch_power: line += ' sweettouchpower=1'
            if len(line) > 1000: p.error('args too long')
        after, n = re.subn(r'^args = "[^"\n]*"$', f'args = "{line}"', before, flags=re.M)
        if n != 1: p.error('expected one config entry')
        (w / 'config').write_text(after)
        copy(w / 'config', '::/EFI/UniXNU/unixnu.toml')
        copy(a.kernel, '::/EFI/UniXNU/Payloads/kernel.macho')
        copy('::/EFI/UniXNU/unixnu.toml', w / 'check-config')
        copy('::/EFI/UniXNU/Payloads/kernel.macho', w / 'check-kernel')
        copy('::/EFI/UniXNU/Payloads/kernel.macho.adt', w / 'check-adt')
        assert (w / 'check-config').read_text() == after
        assert (w / 'check-kernel').read_bytes() == kernel
        assert (w / 'check-adt').read_bytes() == (w / 'adt').read_bytes()
        payload = fat.read_bytes()
        assert len(payload) == r
    boot = bytearray(src)
    boot[off:off + r] = payload
    boot[576:584] = hashlib.sha1(src[page:page + k] + payload + sizes).digest()[:8]
    assert boot[:576] == src[:576] and boot[584:off] == src[584:off]
    assert boot[off + r:] == src[off + r:]
    with a.output.open('xb') as f: f.write(boot)
    print(line)
    print(hashlib.sha256(boot).hexdigest(), a.output)


if __name__ == '__main__':
    main()
