"""Bancos de motions de Team Ninja (`.mtn` y `.mot`): parseo estricto y muestreo de pistas.

Un `.mtn` es un chunk `chr_dat` de 3 slots (`ryu.mtn` en `cmn.afs` #22, `eryun.mtn` en
`boff_cmn.afs` #3). Un `.mot` (`rm_kt.mot`, las armas de Ryu) es directamente el cuerpo del
slot 0, sin cabecera de chunk. Solo se lee el slot 0; el 1 (otra tabla de offsets) y el 2
(16 bytes) quedan sin interpretar. Todo u32/u16 little endian. Offsets relativos al cuerpo.

**Tabla de motions.** u32 por id de motion hasta un 0 (1890 ids en `ryu.mtn`, 1893 en
`rm_kt.mot`). Varios ids comparten offset: los vacíos apuntan a una motion de relleno de
4 frames. Los ids parecen un espacio global que los bancos de armas rellenan (INFERRED).

**Motion.** En su offset hay una cabecera de u32 terminada en 0, uno por pista: el offset de
cada pista. Los datos de las pistas van justo antes de la cabecera, en orden. Observado:
44 pistas en las 258 motions de `ryu.mtn` y en las 167 de `rm_kt.mot`, y todas las pistas
de una motion duran lo mismo (VERIFIED: 0 discrepancias).

**Pista.** Lista de claves; cada una empieza por un u16 de control:

    bits 0-1    tipo: 1 = valor; 2 = valor + 1 tangente; 3 = valor + 2 tangentes; 0 = fin
    bits 2-11   frames hasta la clave siguiente
    bits 12-15  bits 16-19 del valor (entero con signo de 20 bits)

Le siguen el u16 bajo del valor y 0, 1 o 2 tangentes s16. La clave de fin lleva solo el
valor. El 97 % de las claves son de tipo 1 a 1 frame de distancia: animación ya muestreada.

**Interpolación.** Tipo 1, lineal. Tipos 2 y 3, Hermite cúbica: la clave k guarda la
tangente de salida A y la de llegada B del tramo k → k+1 (tipo 2: la misma en ambos
extremos). Evidencia: B de un tramo coincide con A del siguiente (mediana de 6 % de error
relativo, frente al 26 % si fueran tangentes por tramo). Las tangentes son pendientes por
frame × 16 (INFERRED: el ajuste de mínima energía da 17,9 y la mediana por tramo, 14,3).
Unidades de los valores en `ngb_tools.kmd.rig`.
"""

from __future__ import annotations

import struct
from collections.abc import Mapping
from dataclasses import dataclass
from itertools import pairwise
from types import MappingProxyType
from typing import Final

from ngb_tools.kmd.errors import KmdError
from ngb_tools.teamninja import TeamNinjaError, parse_chunk

CHR_DAT_TAG: Final = b"chr_dat\0"
KIND_END: Final = 0
KIND_VALUE: Final = 1
KIND_ONE_SLOPE: Final = 2
KIND_TWO_SLOPES: Final = 3
SLOPE_SCALE: Final = 16.0
# Topes de cordura (observado: 1893 ids y 44 pistas).
MAX_MOTION_IDS: Final = 65536
MAX_TRACKS: Final = 256


class MotionFormatError(KmdError):
    """Los bytes no forman un banco de motions legible."""


@dataclass(frozen=True, slots=True)
class Key:
    """Una clave: frame absoluto, valor bruto y pendientes del tramo que empieza aquí."""

    frame: int
    value: int
    kind: int
    slope_out: int = 0
    slope_in_next: int = 0


@dataclass(frozen=True, slots=True)
class Track:
    keys: tuple[Key, ...]

    @property
    def frames(self) -> int:
        return self.keys[-1].frame

    def sample(self, frame: float) -> float:
        """Valor bruto en `frame` (se satura en los extremos)."""
        keys = self.keys
        if frame <= keys[0].frame or len(keys) == 1:
            return float(keys[0].value)
        if frame >= keys[-1].frame:
            return float(keys[-1].value)
        lo, hi = 0, len(keys) - 1
        while hi - lo > 1:  # búsqueda binaria del tramo
            mid = (lo + hi) // 2
            if keys[mid].frame <= frame:
                lo = mid
            else:
                hi = mid
        k0, k1 = keys[lo], keys[hi]
        span = k1.frame - k0.frame
        t = (frame - k0.frame) / span
        if k0.kind not in (KIND_ONE_SLOPE, KIND_TWO_SLOPES):
            return k0.value + (k1.value - k0.value) * t
        m0 = k0.slope_out * span / SLOPE_SCALE
        m1 = k0.slope_in_next * span / SLOPE_SCALE
        t2, t3 = t * t, t * t * t
        return (
            (2 * t3 - 3 * t2 + 1) * k0.value
            + (t3 - 2 * t2 + t) * m0
            + (-2 * t3 + 3 * t2) * k1.value
            + (t3 - t2) * m1
        )


@dataclass(frozen=True, slots=True)
class Motion:
    offset: int
    tracks: tuple[Track, ...]

    @property
    def frames(self) -> int:
        return self.tracks[0].frames if self.tracks else 0

    def sample(self, frame: float) -> tuple[float, ...]:
        return tuple(track.sample(frame) for track in self.tracks)


@dataclass(frozen=True, slots=True)
class MotionBank:
    """Tabla de ids (offset de cada uno) y motions por offset."""

    table: tuple[int, ...]
    motions: Mapping[int, Motion]

    def motion(self, motion_id: int) -> Motion:
        if not 0 <= motion_id < len(self.table):
            raise MotionFormatError(f"motion {motion_id} fuera de los {len(self.table)} ids")
        return self.motions[self.table[motion_id]]

    def distinct_ids(self) -> tuple[int, ...]:
        """El primer id de cada motion distinta, en orden de id."""
        seen: dict[int, int] = {}
        for motion_id, offset in enumerate(self.table):
            seen.setdefault(offset, motion_id)
        return tuple(sorted(seen.values()))


def _s16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def _value20(ctrl: int, low: int) -> int:
    raw = ((ctrl >> 12) << 16) | low
    return raw - (1 << 20) if raw & 0x80000 else raw


def parse_track(body: bytes, start: int, end: int) -> Track:
    """Claves de la pista que ocupa `body[start:end]` (puede sobrar relleno tras el fin)."""
    keys: list[Key] = []
    pos, frame = start, 0
    while True:
        if pos + 4 > end:
            raise MotionFormatError(f"pista en {start:#x}: sin clave de fin antes de {end:#x}")
        ctrl, low = struct.unpack_from("<HH", body, pos)
        kind = ctrl & 0x3
        size = 4 if kind in (KIND_END, KIND_VALUE) else 2 * (kind + 1)
        if pos + size > end:
            raise MotionFormatError(f"pista en {start:#x}: clave cortada en {pos:#x}")
        slopes = struct.unpack_from(f"<{(size - 4) // 2}H", body, pos + 4)
        first = _s16(slopes[0]) if slopes else 0
        second = _s16(slopes[1]) if len(slopes) > 1 else first
        keys.append(Key(frame, _value20(ctrl, low), kind, first, second))
        if kind == KIND_END:
            return Track(tuple(keys))
        frame += (ctrl >> 2) & 0x3FF
        pos += size


def _u32_run(body: bytes, start: int, limit: int, what: str) -> list[int]:
    """u32 desde `start` hasta un 0 (que no se incluye)."""
    values: list[int] = []
    pos = start
    while True:
        if pos + 4 > len(body):
            raise MotionFormatError(f"{what} en {start:#x} sin terminador 0")
        (value,) = struct.unpack_from("<I", body, pos)
        if value == 0:
            return values
        if len(values) == limit:
            raise MotionFormatError(f"{what} en {start:#x}: más de {limit} entradas")
        values.append(value)
        pos += 4


def _parse_motion(body: bytes, offset: int, data_floor: int) -> Motion:
    track_offsets = _u32_run(body, offset, MAX_TRACKS, "cabecera de motion")
    bounds = [*track_offsets, offset]
    for a, b in pairwise(bounds):
        if not data_floor <= a < b:
            raise MotionFormatError(f"motion en {offset:#x}: offsets de pista desordenados")
    tracks = tuple(parse_track(body, a, b) for a, b in pairwise(bounds))
    lengths = {track.frames for track in tracks}
    if len(lengths) > 1:
        raise MotionFormatError(f"motion en {offset:#x}: pistas de duración distinta {lengths}")
    return Motion(offset, tracks)


def motion_body(data: bytes) -> bytes:
    """El cuerpo del banco: el slot 0 de un `chr_dat`, o el archivo entero (`.mot`)."""
    if not data.startswith(CHR_DAT_TAG):
        return data
    try:
        chunk = parse_chunk(data)
    except TeamNinjaError as error:
        raise MotionFormatError(f"chr_dat: {error}") from error
    first = min(chunk.slots, key=lambda slot: slot.index, default=None)
    if first is None or first.index != 0:
        raise MotionFormatError("chr_dat sin slot 0")
    return data[first.offset : first.end]


def parse_motion_bank(data: bytes) -> MotionBank:
    """Parsea un `.mtn` (chr_dat) o un `.mot` (cuerpo suelto)."""
    body = motion_body(data)
    table = tuple(_u32_run(body, 0, MAX_MOTION_IDS, "tabla de motions"))
    if not table:
        raise MotionFormatError("tabla de motions vacía")
    table_end = 4 * (len(table) + 1)
    motions: dict[int, Motion] = {}
    for offset in sorted(set(table)):
        if offset % 4 or not table_end <= offset < len(body):
            raise MotionFormatError(f"offset de motion {offset:#x} fuera del cuerpo o no alineado")
        motions[offset] = _parse_motion(body, offset, table_end)
    return MotionBank(table, MappingProxyType(motions))
