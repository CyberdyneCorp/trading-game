"""Staff roles and permissions (design D2, spec player-identity "Roles and permissions").

Every authenticated user is a player; staff capabilities come only from the CyberdyneAuth client
roles of the TradeLab client(s), read from the verified token's `roles` claim
(`<client_id>:<role_key>`) and mapped here to permissions. The resolver fails closed: a role of
another client, an unknown role or a malformed entry grants nothing, and an absent `roles` claim
means "unknown", which grants nothing unless a fallback source resolves the roles.
"""

from __future__ import annotations

from collections.abc import Callable, Iterable, Mapping
from typing import Any

SCENARIOS_EDIT = "scenarios.edit"
SCENARIOS_PUBLISH = "scenarios.publish"
SCENARIOS_RETIRE = "scenarios.retire"
IMPORTS_REVIEW = "imports.review"
RUNS_INSPECT = "runs.inspect"

ROLE_PERMISSIONS: Mapping[str, frozenset[str]] = {
    "editor": frozenset({SCENARIOS_EDIT, IMPORTS_REVIEW}),
    "admin": frozenset({SCENARIOS_EDIT, SCENARIOS_PUBLISH, SCENARIOS_RETIRE, IMPORTS_REVIEW, RUNS_INSPECT}),
}

# Looks up the TradeLab role keys of a `sub` when the token carries no `roles` claim; None = the
# lookup failed or is not configured (then nothing is granted).
RoleLookup = Callable[[str], Iterable[str] | None]


def no_lookup(_: str) -> None:
    """Default fallback: no service credential to ask CyberdyneAuth with, so roles stay unknown."""
    return None


def tradelab_roles(roles: Iterable[Any], clients: frozenset[str]) -> frozenset[str]:
    """Role keys of `roles` claim entries that belong to one of the TradeLab clients."""
    keys: set[str] = set()
    for entry in roles:
        if not isinstance(entry, str):
            continue
        client, sep, role = entry.partition(":")
        if sep and client in clients and role:
            keys.add(role)
    return frozenset(keys)


def permissions_for(role_keys: Iterable[str]) -> frozenset[str]:
    granted: set[str] = set()
    for role in role_keys:
        granted |= ROLE_PERMISSIONS.get(role, frozenset())
    return frozenset(granted)


class PermissionResolver:
    def __init__(self, clients: frozenset[str], lookup: RoleLookup = no_lookup) -> None:
        self._clients = clients
        self._lookup = lookup

    def permissions(self, claims: Mapping[str, Any]) -> frozenset[str]:
        roles = claims.get("roles")
        if roles is None:
            return self._from_lookup(str(claims.get("sub", "")))
        if not isinstance(roles, list):
            return frozenset()  # malformed claim: fail closed
        return permissions_for(tradelab_roles(roles, self._clients))

    def _from_lookup(self, sub: str) -> frozenset[str]:
        try:
            keys = self._lookup(sub) if sub else None
        except Exception:  # noqa: BLE001 - any lookup failure must deny, never raise past the check
            return frozenset()
        return permissions_for(keys) if keys is not None else frozenset()
