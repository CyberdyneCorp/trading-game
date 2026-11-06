"""Roles → permissions resolver (design D2, spec player-identity "Roles and permissions"): fail closed.

There are no staff endpoints yet; the HTTP check is exercised on a route added by the test only."""

# No `from __future__ import annotations`: FastAPI resolves the test route's dependency at runtime.
from typing import Annotated

from fastapi import Depends

from tradelab_api.domain.access import (
    IMPORTS_REVIEW,
    SCENARIOS_EDIT,
    SCENARIOS_PUBLISH,
    PermissionResolver,
    permissions_for,
    tradelab_roles,
)

CLIENTS = frozenset({"cyb_tradelab"})


def test_roles_of_the_tradelab_client_map_to_permissions():
    r = PermissionResolver(CLIENTS)
    assert r.permissions({"sub": "s", "roles": ["cyb_tradelab:editor"]}) == {SCENARIOS_EDIT, IMPORTS_REVIEW}
    assert SCENARIOS_PUBLISH in r.permissions({"sub": "s", "roles": ["cyb_tradelab:admin"]})


def test_foreign_unknown_and_malformed_roles_grant_nothing():
    r = PermissionResolver(CLIENTS)
    for roles in (["cyb_market4me:admin"], ["cyb_tradelab:superuser"], ["admin"], ["cyb_tradelab:"], [42],
                  ["cyb_tradelab_evil:admin"], "cyb_tradelab:admin", {"cyb_tradelab": "admin"}, []):
        assert r.permissions({"sub": "s", "roles": roles}) == frozenset(), roles


def test_a_permissions_claim_alone_grants_nothing():
    r = PermissionResolver(CLIENTS)
    assert r.permissions({"sub": "s", "roles": [], "permissions": [SCENARIOS_PUBLISH]}) == frozenset()


def test_missing_roles_claim_is_unknown_and_fails_closed():
    assert PermissionResolver(CLIENTS).permissions({"sub": "s"}) == frozenset()  # no lookup configured

    def broken(_: str) -> list[str]:
        raise TimeoutError("CyberdyneAuth unreachable")

    assert PermissionResolver(CLIENTS, broken).permissions({"sub": "s"}) == frozenset()
    assert PermissionResolver(CLIENTS, lambda _: None).permissions({"sub": "s"}) == frozenset()
    resolved = PermissionResolver(CLIENTS, lambda sub: ["admin"] if sub == "s" else [])
    assert SCENARIOS_PUBLISH in resolved.permissions({"sub": "s"})


def test_helpers():
    assert tradelab_roles(["cyb_tradelab:editor", "other:admin"], CLIENTS) == {"editor"}
    assert permissions_for(["nope"]) == frozenset()


def test_staff_dependency_answers_403_without_the_permission(idp, client):
    app = client.app
    require = app.state.auth.require(SCENARIOS_PUBLISH)

    @app.post("/test-only/publish")
    def publish(staff: Annotated[str, Depends(require)]) -> dict[str, str]:
        return {"by": staff}

    def call(**claims):
        return client.post("/test-only/publish", headers={"Authorization": f"Bearer {idp.token('s', **claims)}"})

    assert client.post("/test-only/publish").status_code == 401
    assert call().status_code == 403  # no roles claim, no lookup: denied
    assert call(roles=["cyb_tradelab:editor"]).status_code == 403
    assert call(roles=["cyb_other:admin"]).status_code == 403
    assert call(roles=["cyb_tradelab:admin"]).json() == {"by": "s"}
