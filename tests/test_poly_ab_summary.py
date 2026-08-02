#!/usr/bin/env python3

from pathlib import Path
import hashlib
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import summarize_p125_poly_ab as summary  # noqa: E402


class PolyAbSummaryTests(unittest.TestCase):
    def build_bundle(self, directory: Path) -> None:
        (directory / "ENVIRONMENT.txt").write_text(
            "schema=oneshotsea.p125-poly-isolated-ab.v1\n"
            "label=fixture\n"
        )
        (directory / "COMMANDS.sh").write_text("true\n")
        projections = []
        timings = {"b1": 120, "a1": 100, "a2": 102, "b2": 118}
        rss = {"b1": 1000, "a1": 1020, "a2": 1010, "b2": 1000}
        for mode in summary.MODES:
            projection = f"projection for {mode}\n".encode()
            digest = hashlib.sha256(projection).hexdigest()
            projections.append(f"{mode} {digest}\n")
            for phase in summary.PHASES:
                stem = f"{phase}-{mode}"
                (directory / f"{stem}.stdout").write_bytes(projection)
                if mode == "sea":
                    timing = (
                        "timing.schema=oneshotsea.p125-poly-trusted-timing.v1\n"
                        "timing.mode=sea\n"
                        f"timing.sea_us={timings[phase]}\n"
                        f"timing.total_us={timings[phase] + 1}\n"
                    )
                else:
                    timing = (
                        "timing.schema=oneshotsea.p125-poly-trusted-timing.v1\n"
                        "timing.mode=frobenius\n"
                        f"timing.elapsed_us={timings[phase]}\n"
                    )
                (directory / f"{stem}.timing.stderr").write_text(timing)
                (directory / f"{stem}.resource.txt").write_text(
                    f"Maximum resident set size (kbytes): {rss[phase]}\n"
                )
        (directory / "PROJECTION_SHA256.txt").write_text("".join(projections))
        checksum_lines = []
        for path in sorted(directory.iterdir()):
            if path.name != "SHA256SUMS":
                checksum_lines.append(
                    f"{hashlib.sha256(path.read_bytes()).hexdigest()}  ./{path.name}\n"
                )
        (directory / "SHA256SUMS").write_text("".join(checksum_lines))

    def test_accepts_complete_balanced_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.build_bundle(directory)
            result = summary.summarize(directory)
            self.assertEqual(result["label"], "fixture")
            self.assertTrue(result["gates"]["accepted"])
            self.assertGreater(result["pooled_frobenius_speedup"], 1.05)

    def test_rejects_tampered_retained_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.build_bundle(directory)
            with (directory / "a1-sea.stdout").open("ab") as stream:
                stream.write(b"tampered\n")
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                summary.summarize(directory)


if __name__ == "__main__":
    unittest.main()
