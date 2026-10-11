"""Host-native production LAN discovery, packet validation, and ENet checks."""
import ctypes
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TEMP = Path(tempfile.gettempdir()) / "gevr-lan-network-tests"


def build_native():
    TEMP.mkdir(exist_ok=True)
    if sys.platform == "darwin":
        compiler = shutil.which("clang") or "clang"
        output = TEMP / "net_lan_native.dylib"
        link = "-dynamiclib"
    else:
        compiler = shutil.which("cc") or "cc"
        output = TEMP / "net_lan_native.so"
        link = "-shared"
    command = [compiler, link, "-fPIC", "-O2", "-w", "-fms-extensions", "-D__ANDROID__=1",
               "-DGEVR=1", "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C=1", "-DVERSION=2",
               "-DVERSION_US=1", "-DLANG_US=1"]
    if sys.platform == "darwin":
        command.append("-D_DARWIN_C_SOURCE=1")
    command += ["-I" + str(ROOT / directory) for directory in
                (".", "port/include", "include", "src", "src/game", "port/src", "port/src/net")]
    command += [str(ROOT / "port/tests/net_lan_native.c"), str(ROOT / "port/external/enet.c"),
                str(ROOT / "port/src/net/netbuf.c"), "-o", str(output)]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    return ctypes.CDLL(str(output))


def build_core_packet_test():
    path = ROOT / "port/tests/test_multiplayer.py"
    spec = importlib.util.spec_from_file_location("gevr_multiplayer_tests", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module.build_core()


class LanNetworkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.native = build_native()
        cls.core = build_core_packet_test()

    def test_discovery_uses_production_udp_socket_and_filters_bad_beacons(self):
        self.assertEqual(self.native.test_discovery_udp(), 0)

    def test_two_peer_roundtrip_uses_production_enet_and_netbuf(self):
        self.assertEqual(self.native.test_enet_two_peer_roundtrip(), 0)

    def test_current_wrong_and_malformed_headers_use_production_handler(self):
        self.assertEqual(self.core.test_core_packet_validation(), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
