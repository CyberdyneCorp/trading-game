"""Per-player token bucket (spec game-api "Limits"): keyed by the verified `sub`, 429 + Retry-After."""

from __future__ import annotations

from tradelab_api.http.rate_limit import TokenBuckets, retry_after_header


class FakeClock:
    def __init__(self) -> None:
        self.t = 1000.0

    def __call__(self) -> float:
        return self.t


def test_bucket_allows_a_burst_then_refills_at_the_rate():
    clock = FakeClock()
    buckets = TokenBuckets(per_minute=60, burst=3, clock=clock)
    assert [buckets.take("a") for _ in range(3)] == [0.0, 0.0, 0.0]
    assert buckets.take("a") == 1.0  # one token a second
    assert buckets.take("b") == 0.0  # other players have their own bucket
    clock.t += 1.0
    assert buckets.take("a") == 0.0
    clock.t += 3600
    assert [buckets.take("a") for _ in range(4)][-1] > 0  # refills only up to the burst


def test_zero_rate_disables_the_limit():
    buckets = TokenBuckets(per_minute=0, burst=1)
    assert not buckets.enabled
    assert all(buckets.take("a") == 0.0 for _ in range(100))


def test_idle_buckets_are_pruned():
    clock = FakeClock()
    buckets = TokenBuckets(per_minute=60, burst=1, clock=clock)
    buckets.MAX_KEYS = 2
    buckets.take("a")
    buckets.take("b")
    clock.t += 10
    buckets.take("c")
    assert set(buckets._buckets) == {"c"}


def test_retry_after_is_whole_seconds_at_least_one():
    assert retry_after_header(0.01) == "1"
    assert retry_after_header(2.2) == "3"


def test_api_answers_429_with_retry_after_per_player(idp, client_with):
    clock = FakeClock()
    client = client_with(rate_limit_per_minute=30, rate_limit_burst=2, app_options={"rate_clock": clock})
    alice = {"Authorization": f"Bearer {idp.token('alice')}"}
    assert [client.get("/api/v1/runs", headers=alice).status_code for _ in range(2)] == [200, 200]
    limited = client.get("/api/v1/runs", headers=alice)
    assert limited.status_code == 429
    assert limited.headers["Retry-After"] == "2"  # 30 a minute: one token every 2 s
    assert client.get("/api/v1/runs", headers={"Authorization": f"Bearer {idp.token('bob')}"}).status_code == 200
    assert client.get("/api/v1/health").status_code == 200  # public endpoints are not keyed by a player
    clock.t += 2
    assert client.get("/api/v1/runs", headers=alice).status_code == 200


def test_invalid_tokens_do_not_consume_a_players_bucket(idp, client_with):
    client = client_with(rate_limit_per_minute=60, rate_limit_burst=1, app_options={"rate_clock": FakeClock()})
    forged = idp.token("alice", aud="another-product")
    for _ in range(3):
        assert client.get("/api/v1/runs", headers={"Authorization": f"Bearer {forged}"}).status_code == 401
    assert client.get("/api/v1/runs", headers={"Authorization": f"Bearer {idp.token('alice')}"}).status_code == 200
