"""Checked fixed-point atoms, mirroring `include/velocita/v2/domain.hpp`.

The reducer compares integer atoms. Nothing in the authoritative path may depend
on binary floating point, host rounding mode, or locale. This module is the only
sanctioned bridge between the research side (which uses floats freely) and the
canonical side (which uses int64 atoms exclusively).

The bridge is deliberately narrow and lossy-by-refusal: a value that cannot be
represented exactly at the declared scale under the declared rounding mode is a
`ContractViolation`, never a silently truncated number.
"""

from __future__ import annotations

from decimal import Decimal, InvalidOperation, localcontext
from typing import Final

from .enums import ContractError, ContractViolation, RoundingMode

INT64_MIN: Final = -(2**63)
INT64_MAX: Final = 2**63 - 1
UINT64_MAX: Final = 2**64 - 1

PRICE_TICKS_SCALE: Final = 1
QUANTITY_LOTS_SCALE: Final = 1
MONEY_MICROS_SCALE: Final = 1_000_000
FEE_MICROS_SCALE: Final = 1_000_000
FEATURE_MICROS_SCALE: Final = 1_000_000


def check_int64(atoms: int, what: str = "value") -> int:
    if not isinstance(atoms, int) or isinstance(atoms, bool):
        raise ContractViolation(ContractError.non_canonical_value, f"{what} is not an int")
    if not (INT64_MIN <= atoms <= INT64_MAX):
        raise ContractViolation(ContractError.capacity_exceeded, f"{what} overflows int64")
    return atoms


def check_uint64(value: int, what: str = "identity") -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise ContractViolation(ContractError.invalid_identity, f"{what} is not an int")
    if not (0 <= value <= UINT64_MAX):
        raise ContractViolation(ContractError.capacity_exceeded, f"{what} overflows uint64")
    return value


def present(value: int, what: str = "identity") -> int:
    check_uint64(value, what)
    if value == 0:
        raise ContractViolation(ContractError.invalid_identity, f"{what} is zero")
    return value


def _round_scaled(scaled: Decimal, mode: RoundingMode, what: str) -> int:
    integral = int(scaled.to_integral_value(rounding="ROUND_DOWN"))
    remainder = scaled - Decimal(integral)
    if remainder == 0 or mode == RoundingMode.toward_zero:
        return integral

    step = 1 if scaled > 0 else -1
    if mode == RoundingMode.away_from_zero:
        return integral + step
    if mode == RoundingMode.nearest_ties_away_from_zero:
        return integral + step if abs(remainder) >= Decimal("0.5") else integral
    raise ContractViolation(ContractError.invalid_enum, f"{what} rounding mode")


def to_atoms(
    value: Decimal | int | str | float,
    scale: int,
    *,
    rounding: RoundingMode = RoundingMode.nearest_ties_away_from_zero,
    exact: bool = False,
    what: str = "value",
) -> int:
    if isinstance(value, float):
        value = Decimal(repr(value))
    try:
        with localcontext() as ctx:
            ctx.prec = 60
            scaled = Decimal(value) * Decimal(scale)
    except (InvalidOperation, ValueError, TypeError) as exc:
        raise ContractViolation(ContractError.non_canonical_value, f"{what}: {exc}") from exc

    if not scaled.is_finite():
        raise ContractViolation(ContractError.non_canonical_value, f"{what} is not finite")
    if exact and scaled != scaled.to_integral_value(rounding="ROUND_DOWN"):
        raise ContractViolation(
            ContractError.non_canonical_value,
            f"{what} is not exactly representable at scale {scale}",
        )
    return check_int64(_round_scaled(scaled, rounding, what), what)


def from_atoms(atoms: int, scale: int) -> Decimal:
    return Decimal(atoms) / Decimal(scale)


def feature_atoms(value, *, rounding: RoundingMode = RoundingMode.nearest_ties_away_from_zero) -> int:
    return to_atoms(value, FEATURE_MICROS_SCALE, rounding=rounding, what="feature")


def money_atoms(value, *, exact: bool = False) -> int:
    return to_atoms(value, MONEY_MICROS_SCALE, exact=exact, what="money")


def fee_atoms(value, *, exact: bool = False) -> int:
    return to_atoms(value, FEE_MICROS_SCALE, exact=exact, what="fee")
