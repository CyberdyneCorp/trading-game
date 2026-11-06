class DomainError(Exception):
    code = "domain_error"


class NotFound(DomainError):
    """Unknown resource or owned by someone else (both map to 404: never reveal existence)."""

    code = "not_found"


class Conflict(DomainError):
    """Same key, different payload (idempotency) or a rewritten action at an existing seq."""

    code = "conflict"


class UnsupportedPolicy(DomainError):
    """The run was recorded with an execution policy this server cannot replay."""

    code = "unsupported_policy"


class InvalidLog(DomainError):
    code = "invalid_log"


class TooLarge(DomainError):
    """An upload above its configured size limit (413)."""

    code = "too_large"
class LimitExceeded(DomainError):
    """A per-player limit (for example the number of preference keys) would be exceeded."""

    code = "limit_exceeded"


class Forbidden(DomainError):
    """Signed in, but without the entitlement the resource requires."""

    code = "forbidden"


class Unauthenticated(DomainError):
    """The resource needs a signed-in player."""

    code = "unauthenticated"


class ReplayUnavailable(DomainError):
    """The replay worker of an older execution policy failed; nothing was stored, retry later."""

    code = "replay_unavailable"
