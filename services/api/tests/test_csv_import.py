"""POST /api/v1/scenario-imports/validate: the engine's CSV report through the Python binding,
identical to what the clients' import wizard gets, private (auth required, nothing stored)."""

from __future__ import annotations

import tradelab_engine as te
from sqlalchemy import inspect, text
from sqlalchemy.orm import Session

PATH = "/api/v1/scenario-imports/validate"
META = {"asset": "BTC", "pair": "BTC/USDT", "venue": "test", "volumeUnit": "base", "intervalSeconds": 3600,
        "timestampUnit": "s", "leverageCap": 4}


def make_csv(rows: int = 120, start: int = 1_700_000_000, step: int = 3600) -> str:
    lines = ["timestamp,open,high,low,close,volume"]
    close = 30_000.0
    for i in range(rows):
        o = close
        close = round(o + (((i * 7919) % 401) - 200) * 0.5, 2)
        lines.append(f"{start + i * step},{o:.2f},{max(o, close) + 12:.2f},{min(o, close) - 9:.2f},{close:.2f},100.5")
    return "\n".join(lines) + "\n"


def test_valid_csv_returns_the_engines_report(client, auth):
    csv = make_csv()
    res = client.post(PATH, json={"csv": csv, "meta": META}, headers=auth())
    assert res.status_code == 200
    assert res.json() == te.csv_validate(csv, META)
    assert res.json()["ok"] is True


def test_errors_and_gaps_carry_the_same_codes_as_the_client(client, auth):
    ms = make_csv(start=1_700_000_000_000, step=3_600_000)
    out = client.post(PATH, json={"csv": ms, "meta": META}, headers=auth()).json()
    assert out == te.csv_validate(ms, META)
    assert out["errors"][0] == {"line": 2, "code": "timestamp_unit"}
    lines = make_csv().splitlines()
    gapped = "\n".join(lines[:60] + lines[63:]) + "\n"
    out = client.post(PATH, json={"csv": gapped, "meta": META}, headers=auth()).json()
    assert out == te.csv_validate(gapped, META)
    assert out["gapCount"] == 1


def test_bad_metadata_is_reported_not_raised(client, auth):
    out = client.post(PATH, json={"csv": make_csv(), "meta": {"asset": "BTC"}}, headers=auth()).json()
    assert out["ok"] is False
    assert "metaError" in out


def test_requires_a_player_and_valid_schema(client, auth):
    assert client.post(PATH, json={"csv": "x", "meta": META}).status_code == 401
    assert client.post(PATH, json={"csv": "x"}, headers=auth()).status_code == 422
    assert client.post(PATH, json={"csv": "x", "meta": META, "owner": "y"}, headers=auth()).status_code == 422


def test_size_limits(idp, client_with):
    client = client_with(max_csv_bytes=1_000)
    headers = {"Authorization": f"Bearer {idp.token()}"}
    res = client.post(PATH, json={"csv": make_csv(), "meta": META}, headers=headers)
    assert res.status_code == 413
    assert res.json() == {"detail": "too_large"}
    small_body = client_with(max_body_bytes=2_000)
    assert small_body.post(PATH, json={"csv": make_csv(), "meta": META}, headers=headers).status_code == 413


def test_nothing_is_stored(client, auth):
    client.post(PATH, json={"csv": make_csv(), "meta": META}, headers=auth())
    engine = client.app.state.engine
    with Session(engine) as s:
        for table in inspect(engine).get_table_names():
            if table != "alembic_version":
                assert s.execute(text(f"SELECT COUNT(*) FROM {table}")).scalar() == 0, table
