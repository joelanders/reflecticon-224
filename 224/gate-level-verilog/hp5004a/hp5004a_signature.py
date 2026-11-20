#!/usr/bin/env python3
"""Bit-exact HP 5004A signature compression and display formatting.

The update direction follows the 5004A service-manual schematic: the serial
input is XORed with the four feedback taps corresponding to raw signature bits
0, 4, 7, and 9, then shifted into bit 15.  The instrument starts each
measurement at zero.

This module deliberately accepts an already-qualified sequence of DATA samples.
START, STOP, CLOCK edge selection, and probe setup/hold belong to the caller;
keeping that boundary explicit prevents an accidental extra sample at either
gate edge.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable


FEEDBACK_MASK = 0x0291

# The 5004A does not display ordinary hexadecimal.  Each raw state nibble is
# wired to the instrument's 0..9/A/C/F/H/P/U alphabet in this order.
DISPLAY_SYMBOLS_BY_RAW_NIBBLE = "084F2A6P195H3C7U"


def update_signature(signature: int, data: int | bool) -> int:
    """Clock one qualified DATA bit into a raw 16-bit signature."""

    signature &= 0xFFFF
    data_bit = int(data) & 1
    feedback_parity = (signature & FEEDBACK_MASK).bit_count() & 1
    incoming = feedback_parity ^ data_bit
    return ((signature >> 1) | (incoming << 15)) & 0xFFFF


def compress_bits(bits: Iterable[int | bool], initial: int = 0) -> int:
    """Return the raw signature after clocking ``bits`` in iteration order."""

    signature = initial & 0xFFFF
    for bit in bits:
        signature = update_signature(signature, bit)
    return signature


def format_signature(signature: int) -> str:
    """Format a raw signature in the four-character HP 5004A display order."""

    signature &= 0xFFFF
    return "".join(
        DISPLAY_SYMBOLS_BY_RAW_NIBBLE[(signature >> shift) & 0xF]
        for shift in (0, 4, 8, 12)
    )


def signature_for_bits(bits: Iterable[int | bool], initial: int = 0) -> str:
    """Compress and format an already-qualified DATA sample stream."""

    return format_signature(compress_bits(bits, initial=initial))


@dataclass
class SignatureAccumulator:
    """Mutable convenience wrapper for a probe or trace monitor."""

    raw: int = 0
    clocks: int = 0

    def reset(self) -> None:
        self.raw = 0
        self.clocks = 0

    def sample(self, data: int | bool) -> None:
        self.raw = update_signature(self.raw, data)
        self.clocks += 1

    @property
    def display(self) -> str:
        return format_signature(self.raw)
