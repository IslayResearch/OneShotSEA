from pathlib import Path
import hashlib
import io
import json
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import generate_weber_modpoly as weber  # noqa: E402
import fetch_weber_tables as fetch  # noqa: E402


class WeberModpolyTests(unittest.TestCase):
    def test_level_five_exact_identity_fixture(self) -> None:
        expected = """# Weber-f modular polynomial Phi_5^f(X,Y); r=q^(1/48).
6 0 1
0 6 1
5 5 -1
1 1 4
"""
        coefficients = weber.generate_coefficients(5)
        self.assertEqual(weber.format_sparse(5, coefficients), expected)
        self.assertEqual(coefficients[5, 5], -1)

    def test_level_seven_second_identity_fixture(self) -> None:
        expected = """# Weber-f modular polynomial Phi_7^f(X,Y); r=q^(1/48).
8 0 1
0 8 1
7 7 -1
4 4 7
1 1 -8
"""
        self.assertEqual(
            weber.format_sparse(7, weber.generate_coefficients(7)), expected)

    def test_specialization_and_j_map(self) -> None:
        coefficients = weber.generate_coefficients(5)
        # Phi_5^f(2,Y)=Y^6-32Y^5+8Y+64.
        self.assertEqual(weber.specialize(coefficients, 2, 101),
                         [64, 8, 0, 0, 0, 69, 1])
        value = 3
        j = weber.j_from_weber(value, 101)
        power = pow(value, 24, 101)
        self.assertEqual((power * j - pow(power - 16, 3, 101)) % 101, 0)

    def test_admissible_levels(self) -> None:
        for level in (2, 3, 4, 6):
            with self.assertRaises(ValueError):
                weber.generate_coefficients(level)

    def test_checked_in_tables_match_generator_and_manifest(self) -> None:
        directory = ROOT / "data" / "modpoly" / "weber_f"
        manifest = json.loads((directory / "MANIFEST.json").read_text())
        for level in (5, 7):
            path = directory / f"phi_{level}.txt"
            generated = subprocess.run(
                [sys.executable, ROOT / "tools" / "generate_weber_modpoly.py",
                 "--level", str(level)],
                check=True,
                text=True,
                stdout=subprocess.PIPE,
            ).stdout
            self.assertEqual(path.read_text(), generated)
            self.assertEqual(
                hashlib.sha256(path.read_bytes()).hexdigest(),
                manifest["files"][path.name]["sha256"],
            )

    def test_pinned_source_catalog_covers_current_and_future_levels(self) -> None:
        directory = ROOT / "data" / "modpoly" / "weber_f"
        payload = (directory / "SOURCE_CATALOG.txt").read_bytes()
        self.assertEqual(hashlib.sha256(payload).hexdigest(),
                         fetch.SOURCE_CATALOG_SHA256)
        catalog = fetch._parse_source_catalog(payload)
        self.assertEqual(len(catalog), 166)
        self.assertIn(401, catalog)
        self.assertIn(409, catalog)
        self.assertIn(997, catalog)
        manifest = json.loads((directory / "MANIFEST.json").read_text())
        for record in manifest["files"].values():
            self.assertEqual(
                catalog[record["level"]],
                (record["bytes"], record["sha256"]),
            )
        fetch.verify(directory)

    def test_selective_archive_materialization_is_catalog_bound(self) -> None:
        upstream = b"[6,0] 1\n[5,5] -1\n[1,1] 4\n"
        archive_buffer = io.BytesIO()
        with tarfile.open(fileobj=archive_buffer, mode="w:gz") as archive:
            member = tarfile.TarInfo("temp/phi1_5.new")
            member.size = len(upstream)
            archive.addfile(member, io.BytesIO(upstream))
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "tables"
            with mock.patch.object(
                fetch, "_load_archive", return_value=archive_buffer.getvalue()
            ):
                fetch.generate(
                    output,
                    [5],
                    None,
                    ROOT / "data" / "modpoly" / "weber_f" /
                    "SOURCE_CATALOG.txt",
                )
            fetch.verify(output)
            self.assertEqual(
                (output / "phi_5.txt").read_bytes(),
                (ROOT / "data" / "modpoly" / "weber_f" /
                 "phi_5.txt").read_bytes(),
            )
            manifest = json.loads((output / "MANIFEST.json").read_text())
            self.assertEqual(manifest["levels"], [5])
            self.assertEqual(set(manifest["files"]), {"phi_5.txt"})

    def test_level_selection_fails_closed(self) -> None:
        self.assertEqual(fetch._requested_levels(None, "5,409,997"),
                         [5, 409, 997])
        self.assertEqual(fetch._requested_levels(400, None)[-1], 397)
        for selection in ("", "5,5", "7,5", "4", "1009", "abc"):
            with self.subTest(selection=selection), self.assertRaises(ValueError):
                fetch._requested_levels(None, selection)
        for maximum in (4, 998):
            with self.assertRaises(ValueError):
                fetch._requested_levels(maximum, None)

    def test_archive_load_has_network_and_size_bounds(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "oversized.tar.gz"
            archive.write_bytes(b"1234")
            with mock.patch.object(fetch, "ARCHIVE_MAX_BYTES", 3):
                with self.assertRaisesRegex(ValueError, "byte limit"):
                    fetch._load_archive(archive)

        response = mock.MagicMock()
        response.__enter__.return_value.read.return_value = b"pinned"
        with mock.patch.object(fetch.urllib.request, "urlopen",
                               return_value=response) as opener:
            with mock.patch.object(fetch, "_sha256",
                                   return_value=fetch.ARCHIVE_SHA256):
                self.assertEqual(fetch._load_archive(None), b"pinned")
        opener.assert_called_once_with(
            fetch.ARCHIVE_URL,
            timeout=fetch.ARCHIVE_DOWNLOAD_TIMEOUT_SECONDS,
        )
        response.__enter__.return_value.read.assert_called_once_with(
            fetch.ARCHIVE_MAX_BYTES + 1
        )


if __name__ == "__main__":
    unittest.main()
