"""tools/packer (design D5): generate, mask, build with real zstd compression, publisher sign and
inspect, checked through the engine (what every client runs) and independent readers."""

import io
import json
import sys
import tarfile
from pathlib import Path

import pytest
import zstandard

from tradelab_engine import Run, device_key, pack_source_mission, pack_validate, signing_key

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "packer"))
import packer  # noqa: E402


def make_csv(rows: int = 150, start: int = 1_700_000_000) -> str:
    lines = ["timestamp,open,high,low,close,volume"]
    close = 30_000.0
    for i in range(rows):
        o = close
        close = round(o + (((i * 7919) % 401) - 200) * 0.5, 2)
        lines.append(f"{start + i * 3600},{o:.2f},{max(o, close) + 12:.2f},{min(o, close) - 9:.2f},{close:.2f},{100 + i % 37}.5")
    return "\n".join(lines) + "\n"


def run_cli(argv: list[str], capsys: pytest.CaptureFixture[str]) -> tuple[int, dict]:
    code = packer.main(argv)
    out = capsys.readouterr().out
    return code, json.loads(out)


def trusted(seed_file: Path, prefix: str = "publisher") -> list[dict[str, str]]:
    key = signing_key(seed_file.read_text().strip(), prefix)
    return [{"id": key["id"], "key": key["publicKey"]}]


def trusted_device(seed_hex: str) -> list[dict[str, str]]:
    key = device_key(seed_hex)
    return [{"id": key["id"], "key": key["publicKey"]}]


@pytest.fixture
def seed_file(tmp_path: Path) -> Path:
    path = tmp_path / "publisher.seed"
    path.write_text("5" * 64 + "\n")
    return path


def test_generate_build_compresses_and_replays_the_mission(tmp_path: Path, seed_file: Path, capsys):
    src = tmp_path / "src"
    code, result = run_cli(["generate", "m09-event-straddle", "--seed", "7", "-o", str(src)], capsys)
    assert code == 0, result
    assert result["scenarioId"] == "m09-event-straddle-s7"
    assert sorted(result["written"]) == ["bars.bin", "events.bin", "funding.bin", "manifest.json", "options.bin"]

    pack_path = tmp_path / "m09.tlpack"
    code, result = run_cli(["build", str(src), "--seed-file", str(seed_file), "--publish", "-o", str(pack_path)], capsys)
    assert code == 0, result
    assert result["validation"]["ok"]
    assert result["signingKeyId"].startswith("publisher-")
    assert result["compression"]["stored"] < result["compression"]["raw"] / 2  # real compression, not stored blocks
    pack = pack_path.read_bytes()

    # Deterministic: the same source, level and key give the same bytes.
    again = tmp_path / "again.tlpack"
    assert packer.main(["build", str(src), "--seed-file", str(seed_file), "-o", str(again)]) == 0
    capsys.readouterr()
    assert again.read_bytes() == pack

    # A run on the pack is the mission's run.
    with Run.from_pack(pack, trusted(seed_file), 7) as on_pack, Run("m09-event-straddle", 7) as on_mission:
        for run in (on_pack, on_mission):
            run.place_order(1, 0, 0, "2", None, False)
            run.advance(30)
            run.close_position(2)
        a, b = on_pack.advance(10_000), on_mission.advance(10_000)
    assert a["digest"] == b["digest"]
    assert a["eventsDigest"] == b["eventsDigest"]


def test_frames_are_real_zstd_read_by_independent_tools(tmp_path: Path, seed_file: Path, capsys):
    src = tmp_path / "src"
    assert packer.main(["generate", "m05-options-writer", "--seed", "3", "-o", str(src)]) == 0
    assert packer.main(["build", str(src), "--seed-file", str(seed_file), "-o", str(tmp_path / "p.tlpack")]) == 0
    capsys.readouterr()
    with tarfile.open(tmp_path / "p.tlpack") as tar:
        manifest = json.loads(tar.extractfile("manifest.json").read())
        stored = {name: tar.extractfile(name).read() for name in ("bars.bin", "options.bin")}
    for name, frame in stored.items():
        params = zstandard.get_frame_parameters(frame)
        assert params.content_size == (src / name).stat().st_size
        assert zstandard.ZstdDecompressor().decompress(frame) == (src / name).read_bytes()
    assert manifest["instruments"]["options"]["allowShort"] is True
    assert manifest["ivModel"]["kind"] == "file"


def test_mask_records_rights_and_publishing_checks_them(tmp_path: Path, seed_file: Path, capsys):
    csv = tmp_path / "window.csv"
    csv.write_text(make_csv())
    src = tmp_path / "src"
    code, result = run_cli(["mask", str(csv), "--pair", "BTC/USDT", "-o", str(src)], capsys)
    assert code == 0, result
    manifest = json.loads((src / "manifest.json").read_text())
    assert manifest["mode"] == "historical" and manifest["private"] is False
    assert manifest["provenance"]["csvSha256"]
    out = tmp_path / "h.tlpack"
    code, result = run_cli(["build", str(src), "--seed-file", str(seed_file), "--publish", "-o", str(out)], capsys)
    assert code == 1
    assert result["validation"]["reason"] == "license_incomplete"  # no attribution recorded
    assert not out.exists()

    cleared = tmp_path / "cleared"
    assert packer.main(["mask", str(csv), "--pair", "BTC/USDT", "--license", "CC-BY-4.0", "--attribution", "Test venue",
                        "--offline", "--downloadable", "--as-of", "2026-10-01T00:00:00Z", "-o", str(cleared)]) == 0
    capsys.readouterr()
    code, result = run_cli(["build", str(cleared), "--seed-file", str(seed_file), "--publish", "-o", str(out)], capsys)
    assert code == 0, result
    with Run.from_pack(out.read_bytes(), trusted(seed_file), 1) as run:
        debrief_state = run.advance(10_000)
        assert debrief_state["status"] == "finished"
        assert run.debrief()["reveal"]["pair"] == "BTC/USDT"


def test_branch_pack_labels_every_bar_after_the_branch_point(tmp_path: Path, seed_file: Path, capsys):
    csv = tmp_path / "window.csv"
    csv.write_text(make_csv())
    src = tmp_path / "src"
    code, result = run_cli(["mask", str(csv), "--warmup", "40", "--branch-at", "90", "--branch-mission", "m08-regime-switch",
                            "--branch-seed", "4", "-o", str(src)], capsys)
    assert code == 0, result
    out = tmp_path / "b.tlpack"
    assert packer.main(["build", str(src), "--seed-file", str(seed_file), "-o", str(out)]) == 0
    capsys.readouterr()
    with Run.from_pack(out.read_bytes(), trusted(seed_file), 2) as run:
        state = run.advance(1000)
    flags = [bar[5] for bar in state["bars"]]
    assert flags == [i > 90 for i in range(len(flags))]
    assert state["marketMode"] == "branch"


def test_sign_and_inspect(tmp_path: Path, seed_file: Path, capsys):
    src = tmp_path / "src"
    assert packer.main(["generate", "m11-news-calendar", "--seed", "2", "-o", str(src)]) == 0
    built = tmp_path / "built.tlpack"
    assert packer.main(["build", str(src), "--seed-file", str(seed_file), "-o", str(built)]) == 0
    capsys.readouterr()

    release_seed = tmp_path / "release.seed"
    code, result = run_cli(["sign", str(built), "--seed-file", str(release_seed), "--prefix", "release", "-o",
                            str(tmp_path / "release.tlpack")], capsys)
    assert code == 0, result
    released = (tmp_path / "release.tlpack").read_bytes()
    assert result["signingKeyId"].startswith("release-")
    assert pack_validate(released, trusted(release_seed, "release"))["ok"]
    assert pack_validate(released, trusted(seed_file))["reason"] == "untrusted_key"

    key = trusted(release_seed, "release")[0]
    code, result = run_cli(["inspect", str(tmp_path / "release.tlpack"), "--key", f"{key['id']}={key['key']}"], capsys)
    assert code == 0, result
    assert result["validation"]["ok"]
    assert result["manifest"]["mode"] == "synthetic"
    files = {f["name"]: f for f in result["files"]}
    assert files["bars.bin"]["hashOk"] and files["bars.bin"]["ratio"] > 1.5
    assert [t["magic"] for t in files["events.bin"]["tables"]] == ["TLE1", "TLQ1"]
    assert files["events.bin"]["tables"][0]["rows"] == result["manifest"]["data"]["events"]


def test_device_imports_are_never_published(tmp_path: Path, seed_file: Path, capsys):
    csv = tmp_path / "w.csv"
    csv.write_text(make_csv())
    device_seed = tmp_path / "device.seed"
    assert packer.main(["import-csv", str(csv), "--seed-file", str(device_seed), "-o", str(tmp_path / "d.tlpack")]) == 0
    capsys.readouterr()
    code, result = run_cli(["sign", str(tmp_path / "d.tlpack"), "--seed-file", str(seed_file), "-o", str(tmp_path / "x")], capsys)
    assert code == 1
    assert result["reason"] == "private_pack"


def test_unknown_mission_source():
    result, archive = pack_source_mission("m99", 1)
    assert result["reason"] == "unknown_mission"
    assert archive == b""
    with tarfile.open(fileobj=io.BytesIO(pack_source_mission("m01-false-breakout", 1)[1])) as tar:
        assert tar.getnames()[0] == "manifest.json"


def test_publishing_generated_bars_needs_their_calibration_report(tmp_path: Path, seed_file: Path, capsys, monkeypatch):
    src = tmp_path / "src"
    assert packer.main(["generate", "m02-funding-squeeze", "--seed", "1", "-o", str(src)]) == 0
    capsys.readouterr()
    version = json.loads((src / "manifest.json").read_text())["provenance"]["generatorVersion"]
    assert (packer.CALIBRATION_DIR / f"generator-v{version}.md").exists()  # the committed report
    monkeypatch.setattr(packer, "CALIBRATION_DIR", tmp_path / "nowhere")
    code, result = run_cli(["build", str(src), "--seed-file", str(seed_file), "--publish", "-o", str(tmp_path / "p")], capsys)
    assert code == 1
    assert result["reason"] == "calibration_report_missing"
    assert packer.main(["build", str(src), "--seed-file", str(seed_file), "-o", str(tmp_path / "p")]) == 0  # not for publishing


MISSION_TEXTS = {"texts": [
    {"locale": "pt-BR", "title": "Treino · choque de liquidez", "briefing": "O livro afina depois da manchete: confira o custo antes de entrar."},
    {"locale": "en", "title": "Practice · liquidity shock", "briefing": "The book thins after the headline: check the cost before you enter."},
]}


def test_schema_v2_texts_story_and_tiers_through_the_packer(tmp_path: Path, seed_file: Path, capsys):
    src = tmp_path / "src"
    assert packer.main(["generate", "m01-false-breakout", "--seed", "3", "-o", str(src)]) == 0
    capsys.readouterr()
    manifest = json.loads((src / "manifest.json").read_text())
    # A generated mission keeps its margin tiers and objectives, which only schema v2 can say.
    assert manifest["schemaVersion"] == 2
    assert [t["maxLeverage"] for t in manifest["marginModel"]["tiers"]] == [20, 10, 5]
    assert manifest["missionRules"]["objectives"][0]["code"] == "plan_every_entry"
    # The author adds mission texts and a story: a liquidity-outflow headline, then the shock.
    (src / "mission.json").write_text(json.dumps(MISSION_TEXTS, ensure_ascii=False))
    (src / "events.bin").unlink(missing_ok=True)
    story = {"news": [{"step": 80, "kind": "liquidity_outflow", "tone": -1, "credibility": 90, "actual": -1}],
             "liquidityShocks": [{"start": 81, "bars": 6, "spreadPct": 400, "capacityPct": 25}]}
    (src / "events.json").write_text(json.dumps(story))
    pack_path = tmp_path / "v2.tlpack"
    code, result = run_cli(["build", str(src), "--seed-file", str(seed_file), "-o", str(pack_path)], capsys)
    assert code == 0, result
    summary = result["validation"]["pack"]
    assert summary["schemaVersion"] == 2
    assert [t["locale"] for t in summary["texts"]] == ["pt-BR", "en"]

    key = trusted(seed_file)[0]
    code, result = run_cli(["inspect", str(pack_path), "--key", f"{key['id']}={key['key']}"], capsys)
    assert code == 0, result
    assert result["manifest"]["schemaVersion"] == 2
    assert result["manifest"]["data"]["liquidityShocks"] == 1
    assert len(result["tiers"]) == 3
    files = {f["name"]: f for f in result["files"]}
    assert files["mission.json"]["titles"]["en"] == "Practice · liquidity shock"
    assert [t["rows"] for t in files["events.bin"]["tables"]] == [1, 1]

    # In play, the shock shows on the ticket from the headline's bar on (the engine's liquidity view).
    with Run.from_pack(pack_path.read_bytes(), trusted(seed_file), 3) as run:
        state = run.state()
        assert state["contract"]["liquidityShocks"] is True
        while state["step"] < 80:
            state = run.advance(1)
        assert state["liquidity"] is not None
        assert float(state["liquidity"]["halfSpread"]) > float(state["contract"]["halfSpread"])


def test_a_v1_pack_built_before_schema_v2_still_loads():
    fixture = Path(__file__).resolve().parents[3] / "apps" / "web" / "e2e" / "fixtures" / "synthetic-m09.tlpack"
    data = fixture.read_bytes()
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        manifest = json.loads(tar.extractfile("manifest.json").read())
    assert manifest["schemaVersion"] == 1
    device = trusted_device("0e2e" * 16)  # the seed in apps/web/e2e/branch-pack.spec.ts
    result = pack_validate(data, device)
    assert result["ok"], result
    assert result["pack"]["schemaVersion"] == 1
    assert result["pack"]["texts"] == []
