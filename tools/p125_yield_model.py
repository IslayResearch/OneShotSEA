#!/usr/bin/env python3
"""Reproduce the p125 random-curve certificate-yield model.

The probability calculation is a heuristic random-integer model, not an
empirical certificate rate.  It solves the Dickman delay equation

    rho(u) = 1                         (0 <= u <= 1)
    u rho'(u) + rho(u - 1) = 0         (u > 1)

on a fixed grid and applies the Dickman--Mertens approximation for the tail of
the exact y-smooth part of an integer.  All certificate bounds and throughput
projections are then derived deterministically from the checked-in constants.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
from typing import NoReturn


P125 = int(
    "100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237"
)
EULER_MASCHERONI = 0.5772156649015329
GRID_STEP = 0.00001
GRID_MAX_U = 16.0
RETAINED_CURVES = 12
RETAINED_TRACE_CANDIDATES = 136
RETAINED_TRACE_CANDIDATE_ORDERS = 2 * RETAINED_TRACE_CANDIDATES
BASELINE_SECONDS_PER_CURVE = (240.0, 300.0)
MEASURED_K10_WARM_SECONDS_PER_CURVE = 27.071228
FORCED_DIVISOR_SCENARIOS = (
    ("current_generator", "production full rational 2-torsion", 4),
    ("x1_11_cyclic", "X1(11) selected-side cyclic divisor", 44),
    ("x1_11_group", "X1(11) selected-side group-order divisor", 88),
    ("x1_25_cyclic", "X1(25) selected-side cyclic divisor", 100),
    ("x1_25_group", "X1(25) selected-side group-order divisor", 200),
)


def fail(message: str) -> NoReturn:
    raise SystemExit(f"error: {message}")


def rounded(value: float, significant_digits: int = 5) -> float:
    """Report no more precision than the fixed-grid convergence supports."""

    return float(f"{value:.{significant_digits}g}")


class DickmanGrid:
    """Fixed-grid Heun integration of the Dickman delay equation."""

    def __init__(self, step: float = GRID_STEP, maximum: float = GRID_MAX_U):
        reciprocal = round(1.0 / step)
        if reciprocal <= 0 or not math.isclose(
            reciprocal * step, 1.0, rel_tol=0.0, abs_tol=1e-15
        ):
            raise ValueError("Dickman grid step must divide one exactly")
        point_count = round(maximum / step)
        if maximum <= 2.0 or not math.isclose(
            point_count * step, maximum, rel_tol=0.0, abs_tol=1e-12
        ):
            raise ValueError("invalid Dickman grid maximum")

        self.step = step
        self.maximum = maximum
        self.points_per_unit = reciprocal
        values = [1.0] * (reciprocal + 1)
        for index in range(reciprocal, point_count):
            u = index * step
            derivative_here = -values[index - reciprocal] / u
            derivative_next = -values[index + 1 - reciprocal] / (u + step)
            next_value = values[index] + step * (
                derivative_here + derivative_next
            ) / 2.0
            # rho is nonnegative. Tiny negative roundoff after its tail has
            # vanished must not make a negative probability contribution.
            values.append(max(0.0, next_value))
        self.values = values

        expected_at_two = 1.0 - math.log(2.0)
        if not math.isclose(
            self.values[2 * reciprocal],
            expected_at_two,
            rel_tol=0.0,
            abs_tol=1e-9,
        ):
            raise ArithmeticError("Dickman solver failed rho(2)=1-log(2)")
        if any(
            self.values[index + 1] > self.values[index] + 1e-15
            for index in range(reciprocal, len(self.values) - 1)
        ):
            raise ArithmeticError("Dickman solution is not nonincreasing")

    def value_and_tail(self, u: float) -> tuple[float, float]:
        """Linearly interpolate rho(u) and integrate rho from u to max."""

        if not 1.0 <= u < self.maximum:
            raise ValueError("Dickman tail query lies outside the grid")
        coordinate = u / self.step
        lower = int(coordinate)
        fraction = coordinate - lower
        rho_u = (
            self.values[lower] * (1.0 - fraction)
            + self.values[lower + 1] * fraction
        )

        first_width = (1.0 - fraction) * self.step
        integral = first_width * (rho_u + self.values[lower + 1]) / 2.0
        remaining = self.values[lower + 1 :]
        integral += self.step * (
            math.fsum(remaining)
            - remaining[0] / 2.0
            - remaining[-1] / 2.0
        )
        return rho_u, integral


def smooth_tail_probability(
    grid: DickmanGrid, u: float, logarithmic_smooth_bound: float
) -> tuple[float, float, float]:
    rho_u, tail_integral = grid.value_and_tail(u)
    probability = math.exp(-EULER_MASCHERONI) * (
        tail_integral - rho_u / logarithmic_smooth_bound
    )
    if not 0.0 < probability < 1.0:
        raise ArithmeticError("Dickman--Mertens tail is not a probability")
    return probability, rho_u, tail_integral


def geometric_quantile(success_probability: float, percentile: float) -> float:
    if not 0.0 < success_probability < 1.0 or not 0.0 < percentile < 1.0:
        raise ValueError("invalid geometric quantile input")
    return math.log1p(-percentile) / math.log1p(-success_probability)


def build_result() -> dict[str, object]:
    bit_length = P125.bit_length()
    smooth_bound = bit_length**4
    square_root = math.isqrt(P125)
    lower_bound = square_root + 1 + math.isqrt(4 * square_root)
    log_y = math.log(smooth_bound)
    u = math.log(lower_bound) / log_y

    grid = DickmanGrid()
    unconditional, rho_u, tail_integral = smooth_tail_probability(grid, u, log_y)

    # A base-field Montgomery model has rational 2-torsion. Conditional on an
    # even random order, the 2-adic valuation is one plus an independent copy
    # of its unconditioned geometric tail, which shifts L to L/2 in this model.
    even_u = math.log(lower_bound / 2) / log_y
    even_probability, even_rho, even_tail = smooth_tail_probability(
        grid, even_u, log_y
    )
    # The production Weber/Montgomery prefilter is stronger: for p125's
    # congruence class every admitted curve and its twist have full rational
    # E[2], so both actual orders are divisible by four.  The algebraic argument
    # is documented in docs/weber_curve_generator.md and independently asserted
    # on target-sized samples by test_weber_curve_generator.
    generator_divisor = 4
    generator_u = math.log(lower_bound / generator_divisor) / log_y
    generator_probability, generator_rho, generator_tail = (
        smooth_tail_probability(grid, generator_u, log_y)
    )
    coarse_grid = DickmanGrid(step=2.0 * GRID_STEP, maximum=GRID_MAX_U)
    coarse_generator_probability, _, _ = smooth_tail_probability(
        coarse_grid, generator_u, log_y
    )
    optimistic_curve_probability = 1.0 - (1.0 - generator_probability) ** 2
    expected_curves = 1.0 / optimistic_curve_probability
    median_curves = geometric_quantile(optimistic_curve_probability, 0.5)
    percentile_95_curves = geometric_quantile(
        optimistic_curve_probability, 0.95
    )

    baseline_midpoint = math.fsum(BASELINE_SECONDS_PER_CURVE) / 2.0
    projections: dict[str, object] = {}
    for label, count in (
        ("median", median_curves),
        ("mean", expected_curves),
        ("p95", percentile_95_curves),
    ):
        projections[label] = {
            "curves": rounded(count),
            "wall_days_at_240_seconds_per_curve": rounded(count * 240.0 / 86400.0),
            "wall_days_at_300_seconds_per_curve": rounded(count * 300.0 / 86400.0),
        }

    targets: dict[str, object] = {}
    for days in (7, 30, 90):
        aggregate_seconds = days * 86400.0 / expected_curves
        targets[f"mean_within_{days}_days"] = {
            "aggregate_seconds_per_curve": rounded(aggregate_seconds),
            "speedup_over_270_seconds_per_curve": rounded(
                baseline_midpoint / aggregate_seconds
            ),
        }

    forced_divisors: dict[str, object] = {}
    for identifier, description, selected_divisor in FORCED_DIVISOR_SCENARIOS:
        selected_u = (math.log(lower_bound) - math.log(selected_divisor)) / log_y
        selected_probability, selected_rho, selected_tail = smooth_tail_probability(
            grid, selected_u, log_y
        )
        # If the selected order N is zero modulo D, its paired order
        # N'=2(p+1)-N is divisible by gcd(D,2(p+1)).  This is stronger than
        # the generic Montgomery factor two for every p125 X1 scenario below.
        paired_residue = (2 * (P125 + 1)) % selected_divisor
        paired_divisor = math.gcd(selected_divisor, 2 * (P125 + 1))
        paired_u = (math.log(lower_bound) - math.log(paired_divisor)) / log_y
        paired_twist_probability, paired_rho, paired_tail = (
            smooth_tail_probability(grid, paired_u, log_y)
        )
        paired_probability = 1.0 - (
            (1.0 - selected_probability) *
            (1.0 - paired_twist_probability)
        )
        scenario_expected_curves = 1.0 / paired_probability
        forced_divisors[identifier] = {
            "description": description,
            "selected_side_forced_divisor": selected_divisor,
            "paired_twist_order_residue_mod_selected_divisor": paired_residue,
            "paired_twist_forced_divisor": paired_divisor,
            "selected_side_u_log_L_over_D_log_y": rounded(selected_u),
            "selected_side_smooth_opportunity_probability": rounded(
                selected_probability
            ),
            "selected_side_rho": rounded(selected_rho),
            "selected_side_tail_integral": rounded(selected_tail),
            "paired_twist_u_log_L_over_D_log_y": rounded(paired_u),
            "paired_twist_smooth_opportunity_probability": rounded(
                paired_twist_probability
            ),
            "paired_twist_rho": rounded(paired_rho),
            "paired_twist_tail_integral": rounded(paired_tail),
            "optimistic_paired_smooth_opportunity_probability": rounded(
                paired_probability
            ),
            "optimistic_paired_yield_multiplier_over_current": rounded(
                paired_probability / optimistic_curve_probability
            ),
            "optimistic_expected_curves": rounded(scenario_expected_curves),
            "optimistic_expected_days_at_measured_k10_warm": rounded(
                scenario_expected_curves
                * MEASURED_K10_WARM_SECONDS_PER_CURVE
                / 86400.0
            ),
        }

    script_path = Path(__file__).resolve()
    script_sha256 = hashlib.sha256(script_path.read_bytes()).hexdigest()
    actual_order_opportunities = 2 * RETAINED_CURVES
    zero_probability_if_all_candidate_orders_independent = (
        1.0 - generator_probability
    ) ** RETAINED_TRACE_CANDIDATE_ORDERS

    return {
        "schema": "oneshotsea.p125-yield-model.v1",
        "date": "2026-08-01",
        "producer": {
            "script": "tools/p125_yield_model.py",
            "script_sha256": script_sha256,
            "runtime": "Python standard library only",
        },
        "target": {
            "prime": str(P125),
            "bit_length": bit_length,
            "smooth_bound_n4": smooth_bound,
            "certificate_lower_bound_L": str(lower_bound),
            "dickman_u_log_L_over_log_y": rounded(u),
        },
        "model": {
            "classification": "heuristic random-integer marginal model",
            "exact_factor_gate": (
                "A canonical-window smooth divisor can exist only when the exact "
                "n^4-smooth part S_y(N) exceeds L; exact-order point assembly is "
                "an additional requirement."
            ),
            "dickman_equation": (
                "rho(u)=1 for 0<=u<=1; u*rho'(u)+rho(u-1)=0 for u>1"
            ),
            "tail_approximation": (
                "exp(-EulerGamma)*(integral_u^infinity rho(v)dv-rho(u)/log(y))"
            ),
            "assumptions": [
                "Actual curve orders are modeled by random integers in their Hasse interval.",
            "Montgomery-compatible curve and twist orders are conditioned to be even.",
            "The production p125 Weber prefilter further forces full rational E[2], hence divisibility by four on both actual orders.",
                "Curve and twist smooth-tail events are treated as independent only for the optimistic union estimate.",
                "Curve/twist correlation, group-exponent constraints, and exact-order assembly may reduce certificate yield.",
            ],
        },
        "numerics": {
            "method": "fixed-grid Heun integration plus trapezoidal tail",
            "reported_significant_digits": 5,
            "grid_step": GRID_STEP,
            "grid_max_u": GRID_MAX_U,
            "coarse_convergence_grid_step": 2.0 * GRID_STEP,
            "production_generator_probability_coarse_grid": rounded(
                coarse_generator_probability
            ),
            "production_generator_probability_coarse_to_checked_relative_delta": rounded(
                abs(generator_probability - coarse_generator_probability)
                / generator_probability
            ),
            "rho_2_validation": rounded(grid.values[2 * grid.points_per_unit]),
            "rho_2_exact_1_minus_log_2": rounded(1.0 - math.log(2.0)),
            "rho_at_u": rounded(rho_u),
            "tail_integral_at_u": rounded(tail_integral),
            "unconditioned_probability_per_order": rounded(unconditional),
            "even_conditioned_u_log_L_over_2_log_y": rounded(even_u),
            "rho_at_even_conditioned_u": rounded(even_rho),
            "tail_integral_at_even_conditioned_u": rounded(even_tail),
            "even_conditioned_probability_per_actual_order": rounded(
                even_probability
            ),
            "production_generator_forced_divisor": generator_divisor,
            "production_generator_u_log_L_over_4_log_y": rounded(generator_u),
            "production_generator_rho": rounded(generator_rho),
            "production_generator_tail_integral": rounded(generator_tail),
            "production_generator_probability_per_actual_order": rounded(
                generator_probability
            ),
        },
        "curve_twist_projection": {
            "actual_orders_per_curve": 2,
            "optimistic_probability_per_curve": rounded(
                optimistic_curve_probability
            ),
            "expected_curves": rounded(expected_curves),
            "median_curves": rounded(median_curves),
            "p95_curves": rounded(percentile_95_curves),
            "warning": (
                "This is an optimistic smooth-factor opportunity rate, not a "
                "verified-certificate probability; exact-order assembly can fail."
            ),
        },
        "forced_divisor_scenarios": {
            "classification": (
                "optimistic smooth-group-order opportunity model; not a certificate rate"
            ),
            "conditioning": (
                "For a selected side conditioned on a y-smooth divisor D, "
                "independent geometric prime valuations are memoryless, so the "
                "Dickman threshold shifts from L to L/D. Because the paired "
                "order is 2(p+1)-N, it is conditioned on the exact guaranteed "
                "divisor gcd(D,2(p+1))."
            ),
            "limitations": [
                "The selected-side and paired-twist tail events are combined as independent marginals only for an optimistic union estimate.",
                "A group-order divisor does not by itself guarantee the same divisor in the group exponent or an exact-order point.",
                "Curve-family trace bias, curve/twist correlation, representability, and certificate assembly are not modeled.",
                "No value in this section is a certificate probability or observed certificate rate."
            ],
            "scenarios": forced_divisors,
        },
        "retained_search_interpretation": {
            "unique_admitted_curves_indices_0_through_11": RETAINED_CURVES,
            "complete_trace_candidates": RETAINED_TRACE_CANDIDATES,
            "trace_candidate_orders_exactly_screened": (
                RETAINED_TRACE_CANDIDATE_ORDERS
            ),
            "actual_curve_twist_order_opportunities": actual_order_opportunities,
            "smooth_survivors": 0,
            "distinction": (
                "Each admitted curve has one actual trace and exactly two actual "
                "curve/twist orders. Other Hasse-compatible trace candidates are "
                "sound screening alternatives, not independent certificate-yield trials."
            ),
            "expected_survivors_if_all_272_were_independent_production_orders": rounded(
                RETAINED_TRACE_CANDIDATE_ORDERS * generator_probability
            ),
            "probability_of_zero_if_all_272_were_independent_production_orders": rounded(
                zero_probability_if_all_candidate_orders_independent
            ),
        },
        "throughput": {
            "retained_warm_seconds_per_curve_range": list(
                BASELINE_SECONDS_PER_CURVE
            ),
            "measured_k10_warm_seconds_per_curve": (
                MEASURED_K10_WARM_SECONDS_PER_CURVE
            ),
            "projections": projections,
            "targets": targets,
            "interpretation": (
                "Aggregate seconds per curve includes all concurrent workers; "
                "trace-candidate orders per second must not be used as actual "
                "certificate-opportunity throughput."
            ),
        },
    }


def encode(result: dict[str, object]) -> str:
    return json.dumps(result, indent=2, sort_keys=True) + "\n"


def summary(result: dict[str, object]) -> str:
    numerics = result["numerics"]
    projection = result["curve_twist_projection"]
    throughput = result["throughput"]
    assert isinstance(numerics, dict)
    assert isinstance(projection, dict)
    assert isinstance(throughput, dict)
    targets = throughput["targets"]
    assert isinstance(targets, dict)
    thirty_days = targets["mean_within_30_days"]
    assert isinstance(thirty_days, dict)
    return (
        "p125 yield model: "
        f"P_even_order={numerics['even_conditioned_probability_per_actual_order']:.8g}, "
        f"P_optimistic_curve={projection['optimistic_probability_per_curve']:.8g}, "
        f"mean_curves={projection['expected_curves']:.8g}, "
        "30_day_target_seconds_per_curve="
        f"{thirty_days['aggregate_seconds_per_curve']:.8g}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--write", type=Path, help="write canonical JSON artifact")
    action.add_argument("--check", type=Path, help="validate canonical JSON artifact")
    arguments = parser.parse_args()

    result = build_result()
    encoded = encode(result)
    if arguments.write is not None:
        arguments.write.parent.mkdir(parents=True, exist_ok=True)
        arguments.write.write_text(encoded, encoding="utf-8")
        print(summary(result))
        print(f"wrote {arguments.write}")
        return 0
    if arguments.check is not None:
        try:
            observed = arguments.check.read_text(encoding="utf-8")
        except OSError as error:
            fail(f"cannot read {arguments.check}: {error}")
        if observed != encoded:
            fail(
                f"{arguments.check} does not match the deterministic yield model; "
                "regenerate it with --write"
            )
        digest = hashlib.sha256(observed.encode("utf-8")).hexdigest()
        print(summary(result))
        print(f"validated {arguments.check} sha256={digest}")
        return 0

    sys.stdout.write(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
