#!/usr/bin/env python3
import importlib.util
import os
from pathlib import Path
import struct
import unittest

spec=importlib.util.spec_from_file_location('fdt',Path(__file__).with_name('gpu-fdt-config.py'))
fdt=importlib.util.module_from_spec(spec);spec.loader.exec_module(fdt)


def blob(tokens,table=b'compatible\0'):
    # minimal valid v17 header + terminated reserve map + structure + strings
    off=56;total=off+len(tokens)+len(table)
    return struct.pack('>10I',0xd00dfeed,total,off,off+len(tokens),40,17,16,0,len(table),len(tokens))+bytes(16)+tokens+table


def word(n): return struct.pack('>I',n)


class FdtTests(unittest.TestCase):
    def test_minimal_and_properties(self):
        tokens=word(1)+bytes(4)+word(3)+word(5)+word(0)+b'test\0'+bytes(3)+word(2)+word(9)
        self.assertEqual(fdt.parse(blob(tokens))['/']['compatible'],b'test\0')
        self.assertEqual(fdt.cells(word(1)+word(2)),[1,2])
        self.assertEqual(fdt.strings(b'a\0b\0'),['a','b'])

    def test_truncations_and_bad_structure(self):
        valid=blob(word(1)+bytes(4)+word(2)+word(9))
        for size in range(len(valid)):
            with self.subTest(size=size),self.assertRaises(ValueError):fdt.parse(valid[:size])
        for tokens in (word(2)+word(9),word(1)+bytes(4)+word(9),word(8),word(1)+b'bad\0'+word(2)+word(9)):
            with self.assertRaises(ValueError):fdt.parse(blob(tokens))
        data=bytearray(valid);struct.pack_into('>I',data,12,56)
        with self.assertRaises(ValueError):fdt.parse(data)

    def test_duplicate_and_invalid_property(self):
        prop=word(3)+word(4)+word(0)+word(0)
        with self.assertRaises(ValueError):fdt.parse(blob(word(1)+bytes(4)+prop+prop+word(2)+word(9)))
        with self.assertRaises(ValueError):fdt.parse(blob(word(1)+bytes(4)+word(3)+word(99)+word(0)+word(2)+word(9)))
        with self.assertRaises(ValueError):fdt.strings(b'no terminator')
        with self.assertRaises(ValueError):fdt.cells(b'abc')

    def test_live_capture_when_supplied(self):
        path=os.environ.get('CEPHEUS_LIVE_FDT')
        if not path:self.skipTest('supply explicit CEPHEUS_LIVE_FDT for recorded hardware fixture')
        data=Path(path).read_bytes();r=fdt.report(data)
        self.assertEqual(r['fdt_sha256'],'bdd364a16ceabe9960e128103162e5280b150303624a123dfd71e871f9a43274')
        self.assertEqual(r['opp_path'],'/soc/gpu_opp_table_v2')
        self.assertEqual(r['gmu_registers']['kgsl_gmu_pdc_cfg']['address'],0xb290000)
        self.assertEqual(r['gmu_registers']['kgsl_gmu_pdc_seq']['address'],0xb490000)
        self.assertEqual(r['gmu_hz'],[0,200000000])
        self.assertEqual(r['opp_levels'][0],{'hz':257000000,'raw_vlvl':64})
        self.assertEqual(r['streams']['gmu_kernel'],{'sid':5,'mask':0x400,'actlr':0x303})
        self.assertEqual([b['throttle_mask'] for b in r['speed_bins']],[0x80,0x40])
        self.assertEqual([b['lm_enabled_by_source'] for b in r['speed_bins']],[False,True])
        self.assertEqual([b['acd_mailbox_by_source'] for b in r['speed_bins']],[False,True])
        self.assertEqual([b['name'] for n in r['bus_routes']['ddr'] for b in n['bcms']],['SH0','MC0','ACV'])
        self.assertEqual([b['name'] for n in r['bus_routes']['cnoc'] for b in n['bcms']],['SH2','SN15','SN3'])
        self.assertIsNone(r['selected_bin'])
        self.assertEqual(fdt.report(data,1)['selected_bin']['bin'],1)
        with self.assertRaises(ValueError):fdt.report(data,2)


if __name__=='__main__':unittest.main()
