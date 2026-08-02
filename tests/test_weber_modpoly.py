from pathlib import Path
import hashlib
import json
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import generate_weber_modpoly as weber  # noqa: E402


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

    def test_checked_in_tables_support_root_transport(self) -> None:
        directory = ROOT / "data" / "modpoly" / "weber_f"
        manifest = json.loads((directory / "MANIFEST.json").read_text())
        for filename, metadata in manifest["files"].items():
            level = metadata["level"]
            self.assertEqual(level * level % 24, 1, filename)
            path = directory / filename
            for line_number, raw_line in enumerate(
                    path.read_text().splitlines(), start=1):
                data = raw_line.split("#", 1)[0].split()
                if not data:
                    continue
                self.assertEqual(len(data), 3, f"{filename}:{line_number}")
                x_degree, y_degree, coefficient = map(int, data)
                if coefficient == 0:
                    continue
                # BLS sparsity gives ell*a+b == ell+1 (mod 24). Since
                # ell^2 == 1 (mod 24), this is equivalent to
                # a+ell*b == ell+1, which proves
                # Phi(ζX,ζ^ell Y)=ζ^(ell+1)Phi(X,Y) for ζ^24=1.
                self.assertEqual(
                    (level * x_degree + y_degree - level - 1) % 24,
                    0,
                    f"{filename}:{line_number}",
                )
                self.assertEqual(
                    (x_degree + level * y_degree - level - 1) % 24,
                    0,
                    f"{filename}:{line_number}",
                )


if __name__ == "__main__":
    unittest.main()
