"""tools/calibration_report.py (spec market-generator "Calibration report", task 3.4)."""

import sys
from pathlib import Path

import tradelab_engine as te

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import calibration_report  # noqa: E402


def test_report_covers_every_mission_and_states_its_limits(tmp_path: Path):
    report = calibration_report.build_report(3, None)
    assert "educational plausibility model, not a predictive one" in report
    for mission in te.missions():
        assert f"| {mission['id']} |" in report
    assert "No authorized reference series" in report
    assert report == calibration_report.build_report(3, None)  # deterministic


def test_reference_series_adds_its_row(tmp_path: Path):
    csv = tmp_path / "ref.csv"
    closes = [100 * (1 + 0.01 * ((i * 37) % 11 - 5) / 5) ** (i % 3) for i in range(450)]
    csv.write_text("close\n" + "\n".join(f"{c:.4f}" for c in closes) + "\n")
    report = calibration_report.build_report(2, csv)
    assert "| Reference |" in report
    assert "ref.csv" in report


def test_committed_report_matches_the_generator_version():
    _, version = calibration_report.mission_bars("m01-false-breakout", 1)
    committed = Path(__file__).resolve().parents[3] / "docs" / "calibration" / f"generator-v{version}.md"
    assert committed.exists()
    assert committed.read_text().startswith(f"# Generator calibration report — generator v{version}")
