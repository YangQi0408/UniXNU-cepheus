#!/usr/bin/env python3
"""RAM-only tests of the production saved-source audit parser and route walk."""
import importlib.util
from pathlib import Path
import struct
import unittest


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


audit = module('audit', 'audit-pas13-source.py')
fdt = module('fdt', 'gpu-fdt-config.py')


def cells(*values):
    return struct.pack('>' + 'I' * len(values), *values)


class AuditTests(unittest.TestCase):
    def test_function_boundary_ignores_comments_strings_and_prototypes(self):
        text = ('/* int f(void) { wrong(); } */ int f(void);\n'
                'int f(void) { const char *s = "} {"; /* } */ if (1) { real(); } }\n'
                'int g(void) { unrelated(); }')
        result = audit.body(text, 'f')
        self.assertIn('real();', result)
        self.assertNotIn('wrong();', result)
        self.assertNotIn('unrelated();', result)

    def test_missing_unclosed_and_comment_only_definition_rejected(self):
        for text in ('int f(void);', '/* int f(void) {} */', 'int f(void) {'):
            with self.assertRaises(ValueError):
                audit.body(text, 'f')

    def test_negative_call_assertion_masks_only_noncode(self):
        self.assertNotIn('qcom_scm_pas_supported', audit.code(
            '// qcom_scm_pas_supported(13)\n const char *s = "qcom_scm_pas_supported";'))
        self.assertIn('qcom_scm_pas_supported', audit.code('return qcom_scm_pas_supported(13);'))

    def graph(self):
        return {
            '/soc/ad-hoc-bus/src': {'cell-id': cells(125), 'phandle': cells(1),
                'qcom,connections': cells(2), 'qcom,buswidth': cells(8),
                'qcom,agg-ports': cells(1), 'qcom,bcms': cells(3)},
            '/soc/ad-hoc-bus/dst': {'cell-id': cells(512), 'phandle': cells(2),
                'qcom,buswidth': cells(4), 'qcom,agg-ports': cells(4)},
            '/bcm': {'phandle': cells(3), 'qcom,bcm-name': b'CE0\0', 'qcom,rscs': cells(4)},
            '/rsc': {'phandle': cells(4), 'cell-id': cells(0)},
        }

    def test_unique_route_and_cycle(self):
        nodes = self.graph()
        nodes['/soc/ad-hoc-bus/src']['qcom,connections'] = cells(1, 2)
        route = audit.crypto_route(fdt, nodes, 125, 512)
        self.assertEqual([n['node'].rsplit('/', 1)[1] for n in route], ['src', 'dst'])
        self.assertEqual(route[0]['bcms'][0]['name'], 'CE0')
        self.assertEqual(route[0]['bcms'][0]['rsc_id'], 0)

    def test_missing_endpoint_edge_route_and_ambiguous_route_rejected(self):
        with self.assertRaises(ValueError):
            audit.crypto_route(fdt, self.graph(), 126, 512)
        nodes = self.graph()
        nodes['/soc/ad-hoc-bus/src']['qcom,connections'] = cells(1)
        with self.assertRaises(ValueError):
            audit.crypto_route(fdt, nodes, 125, 512)
        nodes = self.graph()
        nodes['/soc/ad-hoc-bus/src']['qcom,connections'] = cells(2, 2)
        with self.assertRaises(ValueError):
            audit.crypto_route(fdt, nodes, 125, 512)
        nodes = self.graph()
        nodes['/soc/ad-hoc-bus/src']['qcom,connections'] = cells(999)
        with self.assertRaises(KeyError):
            audit.crypto_route(fdt, nodes, 125, 512)

    def test_duplicate_ids_and_ambiguous_rsc_rejected(self):
        nodes = self.graph()
        nodes['/soc/ad-hoc-bus/dst']['cell-id'] = cells(125)
        with self.assertRaises(ValueError):
            audit.crypto_route(fdt, nodes, 125, 512)
        nodes = self.graph()
        nodes['/bcm']['qcom,rscs'] = cells(4, 4)
        with self.assertRaises(ValueError):
            audit.crypto_route(fdt, nodes, 125, 512)


if __name__ == '__main__':
    unittest.main()
