from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Environment-driven configuration (prefix TRADELAB_)."""

    model_config = SettingsConfigDict(env_prefix="TRADELAB_")

    database_url: str = "sqlite+pysqlite:///:memory:"
    oidc_discovery_url: str = "https://auth.backend.coolify.cyberdynecorp.ai/.well-known/openid-configuration"
    audience: str = "tradelab-api"
    max_body_bytes: int = 4 * 1024 * 1024
    # Pack object store (filesystem adapter): a directory holding catalog.json and the packs it names.
    packs_dir: str | None = None
    # Older execution policies: cache built by tools/build_policy_cache.py, window = policies supported
    # (current included), processes per older policy.
    policy_cache: str | None = None
    policy_window: int = 3
    replay_workers: int = 2
