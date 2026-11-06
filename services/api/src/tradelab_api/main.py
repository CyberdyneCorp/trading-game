"""Production entry point: `uvicorn tradelab_api.main:app`."""

from .adapters.engine_replayer import ACTION_KINDS, EngineReplayer
from .adapters.object_store import FilesystemObjectStore
from .adapters.oidc import OidcVerifier
from .adapters.policy_replayer import PolicyReplayer
from .config import Settings
from .http.app import create_app

settings = Settings()
replayer = PolicyReplayer(EngineReplayer(), settings.policy_cache, window=settings.policy_window,
                          workers=settings.replay_workers)
packs = FilesystemObjectStore(settings.packs_dir) if settings.packs_dir else None
app = create_app(settings, OidcVerifier(settings.oidc_discovery_url, settings.audience), replayer, ACTION_KINDS,
                 packs=packs)
