import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("mapping_tunnel", Path(__file__).resolve().parents[1] / "deploy/mapping_tunnel.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class TunnelConfigurationTests(unittest.TestCase):
    def host(self, value):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.conf"
            path.write_text("[VideoStream]\nhost=" + value + "\n", encoding="utf-8")
            return module.configured_host(path)

    def test_ip_and_hostname(self):
        self.assertEqual(self.host("192.168.2.112"), "192.168.2.112")
        self.assertEqual(self.host("onboard.local"), "onboard.local")

    def test_full_rtsp_url_keeps_only_host(self):
        self.assertEqual(self.host('"rtsp://viewer:encoded%20value@192.168.2.112:8554/a8mini"'), "192.168.2.112")

    def test_rejects_ssh_option_injection_and_invalid_host(self):
        for invalid in ("", "-oProxyCommand=anything", "host name", "ly@onboard", "127.0.0.1;anything", "http://onboard"):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                self.host(invalid)

    def test_missing_configuration_waits(self):
        with self.assertRaises(ValueError):
            module.configured_host(Path("/nonexistent/agri-settings.conf"))


if __name__ == "__main__":
    unittest.main()
