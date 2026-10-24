"""The networking patch applies safely to cached dependency sources."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.patch_gamenetworkingsockets import apply


class NetworkingPatchTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.source = self.root / "src/steamnetworkingsockets/clientlib/steamnetworkingsockets_lowlevel.cpp"
        self.source.parent.mkdir(parents=True)
        self.source.write_text('''SOCKET BindSocket()
{
\tif ( nBindInterface >= 0 )
\t{
\t\t#ifdef _WIN32
\t\t// Interface setup precedes the bind.
\t\t#endif
\t}

\t// Bind it to specific desired local port/IP
\tif ( bind( sock, (struct sockaddr *)pSockaddr, (socklen_t)len ) == -1 )
\t{
\t\tclosesocket( sock );
\t\treturn INVALID_SOCKET;
\t}
\treturn sock;
}
''')

    def test_exclusive_binding_precedes_bind_and_repeat_is_unchanged(self):
        apply(self.root)
        result = self.source.read_bytes()
        text = result.decode()
        self.assertEqual(text.count("SO_EXCLUSIVEADDRUSE"), 1)
        self.assertLess(text.index("SO_EXCLUSIVEADDRUSE"), text.index("if ( bind("))
        self.assertIn("Failed to set exclusive socket binding", text)
        modified = self.source.stat().st_mtime_ns
        apply(self.root)
        self.assertEqual(self.source.read_bytes(), result)
        self.assertEqual(self.source.stat().st_mtime_ns, modified)

    def test_mismatched_source_is_rejected_without_modification(self):
        self.source.write_text("unrelated upstream implementation\n")
        original = self.source.read_bytes()
        helper = Path(__file__).resolve().parents[1] / "tools/patch_gamenetworkingsockets.py"
        result = subprocess.run([sys.executable, str(helper), str(self.root)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.source.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
