from pathlib import Path
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


if __name__ == "__main__":
    unittest.main()
