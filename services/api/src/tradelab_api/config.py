from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Environment-driven configuration (prefix TRADELAB_)."""

    model_config = SettingsConfigDict(env_prefix="TRADELAB_")

    database_url: str = "sqlite+pysqlite:///:memory:"
    migrate_on_start: bool = True  # `alembic upgrade head` at startup; off when a release step runs it
    oidc_discovery_url: str = "https://auth.backend.coolify.cyberdynecorp.ai/.well-known/openid-configuration"
    audience: str = "tradelab-api"
    max_body_bytes: int = 4 * 1024 * 1024
    # Per-player token bucket (keyed by the verified `sub`): sustained requests per minute and burst.
    # A rate of 0 turns the limit off.
    rate_limit_per_minute: float = 120.0
    rate_limit_burst: int = 60
    # CyberdyneAuth OAuth clients whose `roles` claim entries (`<client_id>:<role>`) count as TradeLab
    # roles, comma separated.
    role_clients: str = "cyb_tradelab"
    # Standard Webhooks signing secret (`whsec_...`) of the CyberdyneAuth subscription; unset = the
    # webhook endpoint answers 503 and CyberdyneAuth keeps retrying.
    webhook_secret: str = ""
    webhook_tolerance_seconds: int = 300
    # CSV import validation: largest CSV accepted (the engine's own limit is 4 MiB).
    max_csv_bytes: int = 4 * 1024 * 1024

    def role_client_ids(self) -> frozenset[str]:
        return frozenset(c.strip() for c in self.role_clients.split(",") if c.strip())
    # Pack object store (filesystem adapter): a directory holding catalog.json and the packs it names.
    packs_dir: str | None = None
    # Older execution policies: cache built by tools/build_policy_cache.py, window = policies supported
    # (current included), processes per older policy.
    policy_cache: str | None = None
    policy_window: int = 3
    replay_workers: int = 2
