"""Exportación de las texturas de un XPR0 a PNG, con un `_manifest.json` para trazarlas.

Se exporta el nivel 0 de cada textura exportable, un PNG por textura. El manifiesto lista
todas, también las que se saltan y el motivo, con lo necesario para volver a los bytes de
origen: offset del campo `Data`, offset absoluto en el archivo, niveles de mip y el sha256
de los bytes de la textura (todos sus mips) y de su paleta.

Las texturas son datos del juego (con copyright): la salida pasa por
`ngb_tools._paths.ensure_outside_repos` sin excepciones (ni `build/`) y por defecto va a
`$NGB_DATA/extracted/textures/<archivo sin extensión>`.

Orden de trabajo: la guarda de "fuera del repo" va primero y después se valida el
catálogo entero sin decodificar. Luego cada textura se decodifica y se escribe de forma
atómica, de una en una (en memoria solo hay una), y el manifiesto va el último, con fsync:
es lo que da por buena la exportación. Un fallo a medias no deja PNG truncados ni un
manifiesto que describa archivos que no existen.

Cada textura exportada lleva `encoding`: `color`, o `signed-bump` si el PNG es un mapa de
relieve con signo (V8U8, L6V5U5) pasado a RGBA con el mapeo de `ngb_tools.xpr.bump`. En
L6V5U5, `alpha` vale `luminance`: el canal A es la luminancia, no transparencia.

Varias cabeceras pueden apuntar a los mismos datos, así que un archivo pequeño podría pedir
miles de texturas de 4096x4096. `pixel_budget` (por defecto `MAX_EXPORT_PIXELS`) acota los
píxeles decodificados por exportación; las texturas que no caben se saltan con su motivo.

Los nombres de los PNG salen solo del número de orden, de la tabla de formatos y de las
dimensiones (`0004_DXT1_256x256.png`): ningún byte del archivo llega a una ruta.
"""

from __future__ import annotations

import hashlib
import json
from collections.abc import Mapping
from dataclasses import replace
from pathlib import Path
from typing import Final, Literal, TypedDict

from ngb_tools._paths import EXTRACTED_SUBDIR, atomic_write_bytes, data_root, ensure_outside_repos
from ngb_tools.xpr.catalog import CatalogEntry, catalog
from ngb_tools.xpr.errors import UnsupportedTextureError, XprFormatError
from ngb_tools.xpr.formats import TextureFormat, lookup_format
from ngb_tools.xpr.png import encode_png
from ngb_tools.xpr.source import XprSource
from ngb_tools.xpr.texture import AlphaMode, alpha_mode, decode_texture

MANIFEST_NAME: Final = "_manifest.json"
TEXTURES_SUBDIR: Final = "textures"
TOOL: Final = "ngb_tools.xpr"
UNNAMED: Final = "sin_nombre"
COLOR: Final = "color"
SIGNED_BUMP: Final = "signed-bump"
# Píxeles de nivel 0 decodificados por exportación: una textura de 4096x4096. Coste medido
# (Python 3.13, un núcleo): DXT1 2,8 Mpx/s, DXT5 1,6, ARGB 6,5, P8 y A8 9; el PNG nivel 9
# cuesta de 0,15 a 4 s por Mpx según los datos. 16 Mpx son 10 s de DXT5 y, en el peor PNG,
# poco más de un minuto (64 Mpx serían unos 4 min). Lo real: 873 728 px en ryu.kmd,
# 1,57 Mpx en dlcustom.xpr y 5,9 Mpx en el XPR0 mayor del juego (suiro_21.afs #0).
MAX_EXPORT_PIXELS: Final = 4096 * 4096


class LevelRecord(TypedDict):
    level: int
    width: int
    height: int
    offset: int
    size: int
    present: bool


class PaletteRecord(TypedDict):
    resource_index: int
    entries: int
    data_offset: int
    file_offset: int
    sha256: str
    source: str


class TextureRecord(TypedDict):
    index: int
    resource_index: int
    status: Literal["exported", "skipped"]
    file: str | None
    issue: str | None
    format_code: int
    format: str
    storage: str | None
    width: int
    height: int
    depth: int
    mips: int
    cubemap: bool
    data_offset: int
    file_offset: int
    byte_length: int | None
    sha256: str | None
    levels: list[LevelRecord]
    palette: PaletteRecord | None
    alpha: str | None
    encoding: str | None
    warnings: list[str]


class ExportManifest(TypedDict):
    tool: str
    source: str
    container: str
    kmd_slot: int | None
    xpr_offset: int
    xpr_total_size: int
    xpr_header_size: int
    exported: int
    skipped: int
    textures: list[TextureRecord]


def default_output_dir(source: Path, environ: Mapping[str, str] | None = None) -> Path:
    """`<datos>/extracted/textures/<nombre del origen sin extensión>`.

    Un nombre hecho solo de puntos o espacios (`..`, `..kmd`) se sustituye: con él la
    carpeta subiría un nivel o se colapsaría en `textures/`.
    """
    stem = source.stem if source.stem.strip(" .") else UNNAMED
    return data_root(environ) / EXTRACTED_SUBDIR / TEXTURES_SUBDIR / stem


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _file_offset(source: XprSource, data_offset: int) -> int:
    """De un campo `Data` (relativo a header_size) a un offset absoluto en el archivo."""
    return source.offset + source.xpr.header_size + data_offset


def _palette_record(source: XprSource, entry: CatalogEntry) -> PaletteRecord | None:
    link = entry.check.palette if entry.check is not None else None
    if link is None:
        return None
    palette = link.palette
    return PaletteRecord(
        resource_index=palette.index,
        entries=palette.entries,
        data_offset=palette.data,
        file_offset=_file_offset(source, palette.data),
        sha256=_sha256(source.xpr.data_bytes(palette.data, palette.byte_size)),
        source=link.source.value,
    )


def _levels(entry: CatalogEntry) -> list[LevelRecord]:
    if entry.layout is None:
        return []
    present = entry.check.present_levels if entry.check is not None else 0
    return [
        LevelRecord(
            level=m.level,
            width=m.width,
            height=m.height,
            offset=m.offset,
            size=m.size,
            present=m.level < present,
        )
        for m in entry.layout.levels
    ]


def _encoding(fmt: TextureFormat | None) -> str:
    """Qué significa el PNG: color, o un mapa de relieve con signo (`ngb_tools.xpr.bump`)."""
    return SIGNED_BUMP if fmt is not None and fmt.is_signed_bump else COLOR


def _record(
    source: XprSource, entry: CatalogEntry, issue: str | None, alpha: AlphaMode | None = None
) -> TextureRecord:
    """Registro de una textura; `issue` es el motivo por el que no se exporta, si lo hay."""
    texture, check = entry.texture, entry.check
    fmt = lookup_format(texture.format.code)
    exported = issue is None and check is not None
    byte_length = check.present_bytes if exported and check is not None else None
    digest = None
    if byte_length is not None:
        digest = _sha256(source.xpr.data_bytes(texture.data, byte_length))
    return TextureRecord(
        index=entry.ordinal,
        resource_index=texture.index,
        status="exported" if exported else "skipped",
        file=entry.filename if exported else None,
        issue=issue,
        format_code=texture.format.code,
        format=entry.format_name,
        storage=fmt.storage.value if fmt is not None else None,
        width=entry.width,
        height=entry.height,
        depth=texture.format.depth if texture.format.dimensions == 3 else 1,
        mips=texture.format.levels,
        cubemap=texture.format.cubemap,
        data_offset=texture.data,
        file_offset=_file_offset(source, texture.data),
        byte_length=byte_length,
        sha256=digest,
        levels=_levels(entry),
        palette=_palette_record(source, entry),
        alpha=alpha.value if alpha is not None and exported else None,
        encoding=_encoding(fmt) if exported else None,
        warnings=entry.warnings if exported else [],
    )


def _render(source: XprSource, entry: CatalogEntry) -> tuple[TextureRecord, bytes | None]:
    """Registro del manifiesto y PNG (si se exporta) de una entrada del catálogo.

    Un fallo al decodificar o al codificar el PNG salta solo esa textura, con su motivo.
    """
    if entry.issue is not None or entry.layout is None:
        return _record(source, entry, entry.issue), None
    try:
        image = decode_texture(source.xpr, entry.texture)
        png = encode_png(image.width, image.height, image.pixels)
    except (UnsupportedTextureError, XprFormatError) as error:
        return _record(source, entry, str(error)), None
    except ValueError as error:  # encode_png: no debería ocurrir con una imagen validada
        return _record(source, entry, f"PNG: {error}"), None
    mode = alpha_mode(entry.layout.format, image.pixels)
    return _record(source, entry, None, mode), png


def export_textures(
    source: XprSource, out_dir: Path, *, pixel_budget: int = MAX_EXPORT_PIXELS
) -> ExportManifest:
    """Escribe un PNG por textura exportable y el manifiesto en `out_dir`.

    Lanza `UnsafeOutputError` (de `_paths`) antes de crear nada si `out_dir` cae dentro de
    un repo git.
    """
    out = ensure_outside_repos(out_dir)
    entries = catalog(source.xpr)
    out.mkdir(parents=True, exist_ok=True)
    records: list[TextureRecord] = []
    remaining = pixel_budget
    for listed in entries:
        entry = listed
        pixels = entry.width * entry.height
        if entry.issue is None and pixels > remaining:
            issue = f"presupuesto de {pixel_budget} píxeles por exportación agotado"
            entry = replace(entry, issue=issue)
        record, png = _render(source, entry)
        records.append(record)
        if png is not None and record["file"] is not None:
            atomic_write_bytes(out / record["file"], png)
            remaining -= pixels
    exported = sum(1 for record in records if record["status"] == "exported")
    manifest = ExportManifest(
        tool=TOOL,
        source=str(source.path.resolve()),
        container=source.container.value,
        kmd_slot=source.slot,
        xpr_offset=source.offset,
        xpr_total_size=source.xpr.total_size,
        xpr_header_size=source.xpr.header_size,
        exported=exported,
        skipped=len(records) - exported,
        textures=records,
    )
    text = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
    # El manifiesto da por buena la exportación: va el último y con fsync.
    atomic_write_bytes(out / MANIFEST_NAME, text.encode("utf-8"), fsync=True)
    return manifest
