#!/usr/bin/env python3
"""Synthetic verifier rejection tests; not native codegen or hardware evidence."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('scope_linked', Path(__file__).with_name('verify-rpmh-scope-linked.py'))
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)
SYMBOLS = {'_ceph_rpmh_scope_phase': 0x1004, '_rh_lock': 0x1000}


def rows(instructions):
    return [(index*4, operation, operands) for index, (operation, operands) in enumerate(instructions)]


def legacy():
    return rows([
        ('adrp', 'x20, 0x1000 <data>'), ('swpa', 'w8, w8, [x20]'),
        ('adrp', 'x8, 0x1000 <data>'), ('add', 'x8, x8, #0x4'),
        ('ldar', 'w8, [x8]'), ('cmp', 'w8, #0x3'),
        ('b.eq', '0x20 <legacy+0x20>'), ('cbnz', 'w8, 0x28 <legacy+0x28>'),
        ('bl', '0x400 <_rh_map>'), ('b', '0x2c <legacy+0x2c>'),
        ('mov', 'w19, #0x10'), ('stlr', 'wzr, [x20]'), ('ret', '')])


def active():
    return rows([
        ('bl', '0x400 <_cepheus_gpu_idle_check>'), ('cbnz', 'w0, 0x38 <active+0x38>'),
        ('mov', 'w1, #0x1'), ('bl', '0x500 <_cepheus_gpu_idle_finish>'),
        ('cbz', 'w0, 0x18 <active+0x18>'), ('b', '0x38 <active+0x38>'),
        ('adrp', 'x23, 0x1000 <data>'), ('add', 'x23, x23, #0x4'),
        ('mov', 'w9, #0x2'), ('stlr', 'w9, [x23]'),
        ('dmb', 'oshst'), ('str', 'w19, [x10, x21]'), ('dsb', 'osh'),
        ('ret', ''), ('mov', 'w0, #0x10'), ('ret', '')])


class ScopeLinkedTests(unittest.TestCase):
    def test_legacy_accept(self):
        self.assertTrue(verify.legacy(legacy(), SYMBOLS)['open_released_only'])

    def test_active_accept(self):
        self.assertTrue(verify.active_write(active(), SYMBOLS)['all_store_paths_pass_keep'])

    def test_xnu_legacy_acquire_and_ccmp(self):
        graph = legacy()
        graph[4] = (16, 'ldapr', 'w8, [x8]')
        graph[6] = (24, 'ccmp', 'w8, #0x0, #0x4, ne')
        graph[7] = (28, 'b.ne', '0x28 <legacy+0x28>')
        self.assertTrue(verify.legacy(graph, SYMBOLS)['open_released_only'])
        for operand in ('w8, #0x1, #0x4, ne', 'w8, #0x0, #0x0, ne',
                        'w8, #0x0, #0x4, eq'):
            with self.subTest(operand=operand):
                graph[6] = (24, 'ccmp', operand)
                with self.assertRaises(AssertionError):
                    verify.legacy(graph, SYMBOLS)

    def test_xnu_store_register_and_offset_widening(self):
        graph = active()
        graph[8] = (32, 'mov', 'w8, #0x2')
        graph[9] = (36, 'stlr', 'w8, [x23]')
        self.assertTrue(verify.active_write(graph, SYMBOLS)['all_store_paths_pass_keep'])
        graph[8] = (32, 'mov', 'w9, #0x2')
        with self.assertRaises(AssertionError):
            verify.active_write(graph, SYMBOLS)
        instructions = [(row[1], row[2]) for row in active()]
        instructions.insert(11, ('mov', 'w21, w21'))
        instructions[1] = ('cbnz', 'w0, 0x3c <active+0x3c>')
        instructions[5] = ('b', '0x3c <active+0x3c>')
        self.assertTrue(verify.active_write(rows(instructions), SYMBOLS)['all_store_paths_pass_keep'])
        instructions[11] = ('mov', 'w20, w20')
        with self.assertRaises(AssertionError):
            verify.active_write(rows(instructions), SYMBOLS)

    def test_legacy_mutations(self):
        mutations = [(1, 'nop', ''), (4, 'ldr', 'w8, [x8]'),
                     (5, 'cmp', 'w8, #0x2'), (7, 'cbz', 'w8, 0x28 <legacy+0x28>'),
                     (7, 'cbnz', 'w8, 0x20 <legacy+0x20>'),
                     (11, 'nop', ''), (0, 'adrp', 'x20, 0x2000 <wrong>')]
        for index, operation, operands in mutations:
            with self.subTest(index=index, operation=operation):
                changed = legacy()
                changed[index] = (index*4, operation, operands)
                with self.assertRaises((AssertionError, StopIteration)):
                    verify.legacy(changed, SYMBOLS)

    def test_active_mutations(self):
        mutations = [(0, 'nop', ''), (2, 'mov', 'w1, #0x0'),
                     (3, 'nop', ''), (4, 'cbz', 'w0, 0x28 <active+0x28>'),
                     (5, 'b', '0x28 <active+0x28>'), (8, 'mov', 'w9, #0x3'),
                     (9, 'nop', ''), (10, 'nop', ''), (12, 'nop', ''),
                     (7, 'add', 'x23, x23, #0x8')]
        for index, operation, operands in mutations:
            with self.subTest(index=index, operation=operation):
                changed = active()
                changed[index] = (index*4, operation, operands)
                with self.assertRaises((AssertionError, StopIteration)):
                    verify.active_write(changed, SYMBOLS)

    def test_cfg_out_of_bounds_rejected(self):
        with self.assertRaises(AssertionError):
            verify.reachable(rows([('nop', '')]), 0)

    def test_cfg_dominance(self):
        graph = rows([('cbz', 'w0, 0x8 <f+0x8>'), ('nop', ''), ('ret', '')])
        self.assertEqual(verify.reachable(graph, 0), {0, 1, 2})
        self.assertEqual(verify.reachable(graph, 0, (1,)), {0, 2})

    def test_global_clobber(self):
        graph = rows([('adrp', 'x8, 0x1000 <data>'), ('bl', '0x500 <callee>'),
                      ('ldar', 'w9, [x8]'), ('ret', '')])
        self.assertNotIn(2, verify.memory(graph))

    def test_parse(self):
        self.assertEqual(verify.parse('0000 <f>:\n0000: ret\n'), {'f': [(0, 'ret', '')]})


if __name__ == '__main__':
    unittest.main()
