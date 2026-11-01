#!/usr/bin/env python3
"""Generate 0083 against pinned ios-sweet's four introduced driver files.

Fail on changed source anchors; never includes the untested GPI/touch lane.
Usage: prepare-performance-patch.py IOS_SWEET_PATCH_DIR OUTPUT.patch
"""
import difflib
from pathlib import Path
import subprocess
import sys
import tempfile

FB = 'iokit/Kernel/arm/SweetFramebuffer.cpp'
HID = 'iokit/Kernel/arm/hid/SweetHID.cpp'
KEYS = 'bsd/dev/sweet_keys.c'
USB = 'bsd/dev/sweet_usb.c'
PATHS = (FB, HID, KEYS, USB)

FADE_BLOCK = '''	if (fb->fPowerOff) {
		/* Fade out: halve every channel each vsync, then black, then idle. */
		uint32_t step = fb->fFadeStep;
		if (step <= 6) {
			fb->fFadeStep = step + 1;
		}
		IOLockUnlock(fb->fLock);
		if (step <= 6) {
			for (uint32_t y = 0; y < fb->fHeight; y++) {
				volatile uint32_t *row = (volatile uint32_t *)(fb->fPixels + (uint64_t)y * fb->fRowBytes);
				for (uint32_t x = 0; x < fb->fWidth; x++) {
					row[x] = step < 6 ? (row[x] >> 1) & 0x7f7f7f7f : 0;
				}
			}
		}
		sender->setTimeoutUS(SWEET_FB_VSYNC_US);
		return;
	}'''

BLANK_BLOCK = '''	if (fb->fPowerOff) {
		bool blank = !fb->fBlanked;
		uint64_t begin = 0;
		if (blank) {
			begin = mach_absolute_time();
			// Write-only clear: never read WC panel RAM for a fade effect.
			// Keep fLock held through this single clear so a concurrent on
			// request cannot be followed by a stale, delayed black write.
			bzero((void *)fb->fPixels, (size_t)fb->fRowBytes * fb->fHeight);
			fb->fBlanked = true;
		}
		IOLockUnlock(fb->fLock);
		if (blank) {
			uint64_t ns;
			absolutetime_to_nanoseconds(mach_absolute_time() - begin, &ns);
			IOLog("SWEET BLANK: clear_us=%llu bytes=%llu\\n", ns / 1000,
			    (uint64_t)fb->fRowBytes * fb->fHeight);
		}
		fb->finishTick(sender);
		return;
	}'''


def replace(text, old, new, count=1):
    if text.count(old) != count:
        raise ValueError(f'upstream anchor count != {count}: {old[:100]!r}')
    return text.replace(old, new)


def transform(path, text):
    if path == HID:
        text = replace(text, '\tUInt32 fTestCount;',
                       '\tUInt32 fTestCount;\n\tUInt32 fTestOnce;\n\tUInt32 fTestWaits;')
        text = replace(text, '\tif (PE_parse_boot_argn("sweethidtest",',
                       '\tPE_parse_boot_argn("sweethidonce", &fTestOnce, sizeof(fTestOnce));\n'
                       '\tif (PE_parse_boot_argn("sweethidtest",')
        text = replace(text, '\tbool press = (self->fTestCount & 1) == 0;', '''
	// In once mode, wait for an HID consumer before the first press, bounded
	// to five minutes. A release is always sent even if the consumer closes.
	if (self->fTestOnce && self->fTestCount == 0 &&
	    (!self->_clients || self->_clients->getCount() == 0)) {
		if (++self->fTestWaits < 300) { sender->setTimeoutMS(1000); }
		return;
	}
	bool press = (self->fTestCount & 1) == 0;''')
        text = replace(text, '\tif (++self->fTestCount < 60) {',
                       '\tif (++self->fTestCount < (self->fTestOnce ? 2u : 60u)) {')
        text = replace(text, '\t\tsender->setTimeoutMS(press ? 100 : self->fTestSecs * 1000);\n\t}',
                       '\t\tsender->setTimeoutMS(press ? 100 : self->fTestSecs * 1000);\n'
                       '\t} else if (self->fTestOnce) {\n'
                       '\t\tIOLog("SWEET HID: one-shot Home complete\\n");\n\t}')
    elif path == KEYS:
        # The reference SID4 GPIO2 is not Mi 9's volume-up. Do not emit its
        # permanently-down event or read it in the performance control.
        text = replace(text, 'static bool keys_started;', 'static bool keys_started;\nstatic int keys_pon_only;')
        text = replace(text, '\tkeys_gpio_apid = keys_find_apid(0x4c1, count);',
                       '\tPE_parse_boot_argn("sweetkeyspononly", &keys_pon_only, sizeof(keys_pon_only));\n'
                       '\tkeys_gpio_apid = keys_pon_only ? KEYS_NO_APID : keys_find_apid(0x4c1, count);')
        text = replace(text, '\t\tif (!pok || !gok) {', '\t\tif (!pok || (!gok && !keys_pon_only)) {')
    elif path == USB:
        text = replace(text, '\tprintf("SWEET USB: setup %02x %02x %04x %04x len %u\\n", type, req, value, index, length);', '''
	// Keep handling each request; only suppress repeated filter trace lines.
	static unsigned filter_traces;
	if (type != 0x21 || req != 0x43 || filter_traces++ < 4) {
		printf("SWEET USB: setup %02x %02x %04x %04x len %u\\n", type, req, value, index, length);
	}''')
    elif path == FB:
        text = replace(text, '\tuint32_t      fFadeStep;      /* fade-out steps done while off */', '''
	bool          fBlanked;
	uint8_t       fPresentRecord[SWEET_FB_SWAP_REC_SIZE]; // workloop-owned
	uint64_t      fPeriod, fDeadline, fStatsPeriod, fStatsAt;
	uint64_t      fSubmitted, fOverwritten, fPresented, fCopyTicks, fCopyMax;
	uint64_t      fCopiedBytes, fMissed;
	uint32_t      fFixedTimer, fPerfStats;
	void finishTick(IOTimerEventSource *sender);''')
        text = replace(text, FADE_BLOCK, BLANK_BLOCK)
        text = replace(text, '\t\t\tfFB->fFadeStep = 0;', '\t\t\tfFB->fBlanked = false;')
        text = replace(text, 'Off fades the panel out over a few\n *      vsyncs (halving each pixel)',
                       'Off clears the panel once with stores only\n *      (no slow read/modify/write of WC memory)')
        text = replace(text, '\tfVsync->setTimeoutUS(SWEET_FB_VSYNC_US);', '''
	PE_parse_boot_argn("sweetfb_fixedtimer", &fFixedTimer, sizeof(fFixedTimer));
	PE_parse_boot_argn("sweetfb_stats", &fPerfStats, sizeof(fPerfStats));
	clock_interval_to_absolutetime_interval(SWEET_FB_VSYNC_US, NSEC_PER_USEC, &fPeriod);
	clock_interval_to_absolutetime_interval(10, NSEC_PER_SEC, &fStatsPeriod);
	fStatsAt = mach_absolute_time();
	fDeadline = fStatsAt + fPeriod;
	fVsync->wakeAtTime(fDeadline);''')
        text = replace(text, '''	if (flush) {
		rec = (uint8_t *)IOMallocData(SWEET_FB_SWAP_REC_SIZE);
		if (rec != NULL) {
			memcpy(rec, fb->fPending, SWEET_FB_SWAP_REC_SIZE);
			fb->fDirty = false;
		}
	}''', '''	if (flush) {
		rec = fb->fPresentRecord;
		memcpy(rec, fb->fPending, SWEET_FB_SWAP_REC_SIZE);
		fb->fDirty = false;
	}''')
        text = replace(text, '''	if (rec != NULL) {
		fb->present(rec);
		IOFreeData(rec, SWEET_FB_SWAP_REC_SIZE);
	}
	sender->setTimeoutUS(SWEET_FB_VSYNC_US);
}''', '''	if (rec != NULL) {
		uint64_t begin = mach_absolute_time();
		fb->present(rec);
		uint64_t elapsed = mach_absolute_time() - begin;
		fb->fCopyTicks += elapsed;
		if (elapsed > fb->fCopyMax) { fb->fCopyMax = elapsed; }
		++fb->fPresented;
	}
	fb->finishTick(sender);
}

void
AppleCLCD::finishTick(IOTimerEventSource *sender)
{
	uint64_t now = mach_absolute_time();
	if (fPerfStats && now - fStatsAt >= fStatsPeriod) {
		uint64_t ns, copy_ns, max_ns;
		absolutetime_to_nanoseconds(now - fStatsAt, &ns);
		absolutetime_to_nanoseconds(fCopyTicks, &copy_ns);
		absolutetime_to_nanoseconds(fCopyMax, &max_ns);
		IOLockLock(fLock);
		uint64_t submitted = fSubmitted, overwritten = fOverwritten;
		fSubmitted = fOverwritten = 0;
		bool off = fPowerOff;
		IOLockUnlock(fLock);
		IOLog("SWEET PERF: ms=%llu submitted=%llu presented=%llu overwritten=%llu copy_us=%llu max_us=%llu bytes=%llu missed=%llu off=%u\\n",
		    ns / 1000000, submitted, fPresented, overwritten, copy_ns / 1000,
		    max_ns / 1000, fCopiedBytes, fMissed, off ? 1u : 0u);
		fPresented = fCopyTicks = fCopyMax = fCopiedBytes = fMissed = 0;
		fStatsAt = now;
	}
	if (!fFixedTimer) {
		sender->setTimeoutUS(SWEET_FB_VSYNC_US);
		return;
	}
	// Absolute cadence: skip missed slots, never run a catch-up busy loop.
	now = mach_absolute_time();
	fDeadline += fPeriod;
	if (fDeadline <= now) {
		uint64_t skipped = (now - fDeadline) / fPeriod + 1;
		fDeadline += skipped * fPeriod;
		fMissed += skipped;
	}
	sender->wakeAtTime(fDeadline);
}''')
        text = replace(text, '\tmemcpy(fPending, rec, SWEET_FB_SWAP_REC_SIZE);',
                       '\t++fSubmitted;\n\tif (fDirty) { ++fOverwritten; }\n'
                       '\tmemcpy(fPending, rec, SWEET_FB_SWAP_REC_SIZE);')
        text = replace(text, '\t\tif (src != 0 && bpe == 4) {',
                       '\t\tif (src != 0 && bpe == 4) {\n'
                       '\t\t\tfCopiedBytes += (uint64_t)w * (uint64_t)h * 4;')
    return text


def main():
    patches, output = map(Path, sys.argv[1:])
    if output.exists():
        sys.exit('refusing to overwrite output')
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        subprocess.run(['git', 'init', '-q', str(root)], check=True)
        for patch in sorted(patches.glob('*.patch')):
            subprocess.run(['git', '-C', str(root), 'apply', '--whitespace=nowarn',
                            *[f'--include={p}' for p in PATHS], str(patch.resolve())], check=True)
        result = []
        for path in PATHS:
            before = (root / path).read_text()
            after = transform(path, before)
            result.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                              fromfile='a/' + path, tofile='b/' + path))
        output.write_text(''.join(result))
        subprocess.run(['git', '-C', str(root), 'apply', '--check', str(output.resolve())], check=True)
    print('generated and apply-checked', output)


if __name__ == '__main__':
    main()
