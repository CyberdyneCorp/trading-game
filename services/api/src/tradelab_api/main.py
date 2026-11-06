"""Production entry point: `uvicorn tradelab_api.main:app`."""

from .adapters.engine_replayer import ACTION_KINDS, EngineCsvValidator, EngineReplayer
from .adapters.oidc import OidcVerifier
from .config import Settings
from .http.app import create_app

settings = Settings()
app = create_app(settings, OidcVerifier(settings.oidc_discovery_url, settings.audience), EngineReplayer(), ACTION_KINDS,
                 csv_validator=EngineCsvValidator())
