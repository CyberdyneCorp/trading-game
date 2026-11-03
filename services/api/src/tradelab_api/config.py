from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Environment-driven configuration (prefix TRADELAB_)."""

    model_config = SettingsConfigDict(env_prefix="TRADELAB_")

    database_url: str = "sqlite+pysqlite:///:memory:"
    oidc_discovery_url: str = "https://auth.backend.coolify.cyberdynecorp.ai/.well-known/openid-configuration"
    audience: str = "tradelab-api"
    max_body_bytes: int = 4 * 1024 * 1024
