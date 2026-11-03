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
