"""Cadenas ASCII de las secciones y su clasificación heurística.

Un ejecutable sin símbolos todavía habla: rutas del árbol de fuentes, asserts,
mensajes de error, nombres de assets, sellos de versión del middleware,
identificadores del motor y nombres de clase RTTI de MSVC. Las heurísticas son
baratas y explicables; se prefiere perder alguna cadena real a llenar el
informe de ruido salido de código x86 o de gráficos.

Coste acotado ante un XBE hostil: todas las expresiones regulares son lineales en
la longitud de la cadena (las de rutas y assets solo empiezan a probar al inicio de
un token, ver `PATH_RE`), y además las rachas de más de `MAX_CLASSIFIED_LEN`
caracteres se cuentan pero no se clasifican.
"""

from __future__ import annotations

import re
from collections import Counter
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from enum import StrEnum
from types import MappingProxyType
from typing import Final

from ngb_tools.xbe.image import XbeImage

DEFAULT_MIN_LEN = 6
# Las cadenas reales más largas de NGB rondan los 1700 caracteres (el texto legal de la
# pantalla WARNING); una racha más larga es una tabla, relleno o un intento de agotar las
# heurísticas, y se cuenta pero no se clasifica.
MAX_CLASSIFIED_LEN: Final = 4096

SOURCE_FILE_RE = re.compile(r"[\w\-]\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|inl|asm)\b", re.IGNORECASE)
# Un intento que empieza a mitad de un token (carácter de ruta precedido de otro) nunca da
# la primera coincidencia de `finditer`: el intento desde el inicio del token llega al mismo
# separador. La excepción es la letra de unidad (`xc:\`), porque ':' corta el token. Así que
# solo se prueba al inicio de un token, en un separador o en una letra de unidad: la búsqueda
# es lineal y las coincidencias, las mismas. Sin esto, una racha larga sin separadores
# cuesta O(n²).
PATH_RE = re.compile(
    r"(?:(?<![\w.\-~$%])|(?=[\\/]|[A-Za-z]:))"
    r"(?:[A-Za-z]:)?[\\/]{0,2}(?:[\w.\-~$%]{2,}[\\/])+[\w.\-~$%]*"
)
ASSERT_RE = re.compile(r"\bassert|\b\w+\s+(?:==|!=|<=|>=|&&|\|\|)\s+\w+", re.IGNORECASE)
# Palabras de depuración "fuertes" (casi siempre mensajes de desarrollador) y "débiles"
# (también aparecen en textos de UI o diálogo; exigen además una pista técnica).
DEBUG_STRONG_RE = re.compile(
    r"\b(?:error|warning|fail(?:ed|ure)?|invalid|illegal|overflow|out of memory|fatal|abort|"
    r"exception|unsupported|timeout|leak|parameter|assert)\b|status=",
    re.IGNORECASE,
)
DEBUG_WEAK_RE = re.compile(
    r"\b(?:cannot|can't|couldn't|unable|not found|unknown|missing|bad|debug)\b", re.IGNORECASE
)
TECH_PRINTF_RE = re.compile(r"%[-+ 0#]*\d*l?[xXp]")  # %x, %08x, %p: volcados técnicos
# Pista técnica: printf, código de error (E1234), identificador_con_guiones, Camel/llamada().
DEV_HINT_RE = re.compile(r"%[-+ 0#]*\d*[sdiuxXp]|\bE?\d{4,}\b|\w_\w|[a-z][A-Z]\w*|\w\(\)|::")
# Marcado de botones de la UI de NGB ('@BA' = botón A): delata texto de interfaz.
UI_MARKUP_RE = re.compile(r"@B[A-Z]")
REPEAT_OK = frozenset(" -=_.*0#~")  # caracteres que sí se repiten en texto real
ASSET_EXTENSIONS = (
    "afs|xpr|xbr|xwb|xsb|xap|wav|wma|adx|aix|sfd|bin|dat|tex|tmc|mdl|dds|bmp|tga|png|jpg|"
    "jpeg|xmv|wmv|def|ins|ini|txt|xbe|ttf|fnt|cfg|lst|tbl|mot|anm|sav|xbx|xsv|xmp|fx|vsh|psh|xvu"
)
# Mismo truco que en PATH_RE: solo se prueba al inicio de un token.
ASSET_RE = re.compile(rf"(?<![\w\-%])[\w\-%]+\.(?:{ASSET_EXTENSIONS})\b", re.IGNORECASE)
# Descriptor de tipo RTTI de MSVC: '.?AV' (class), '.?AU' (struct), '.?AW' (enum).
RTTI_RE = re.compile(r"\.\?A[VUW]([A-Za-z_]\w*(?:@[A-Za-z_]\w*)*)@@")
VERSION_STAMP_RE = re.compile(r"\bVer(?:sion)?\.?\s*\d+\.\d+|\bBuild\s*:", re.IGNORECASE)
IDENTIFIER_RE = re.compile(r"^(?:[A-Z][A-Z0-9]*(?:_[A-Z0-9]+)+|[a-z][a-z0-9]*(?:_[a-z0-9]+)+)$")
REPEAT_RUN_RE = re.compile(r"(.)\1{3,}")
TEXT_CHARS = frozenset(" _.-/\\:%()[],'=<>!&|*+?\"~$")
MAX_DISTINCT_PUNCTUATION = 8  # más signos distintos: tabla o código máquina
MIN_TEXT_RATIO = 0.85


class StringCategory(StrEnum):
    """Categorías del informe, en el orden en que se presentan."""

    SOURCE_PATHS = "source_paths"
    ASSERTS = "asserts"
    DEBUG_MESSAGES = "debug_messages"
    ASSET_NAMES = "asset_names"
    VERSION_STAMPS = "version_stamps"
    ENGINE_IDENTIFIERS = "engine_identifiers"
    RTTI_TYPES = "rtti_types"


@dataclass(frozen=True, slots=True)
class FoundString:
    """Racha imprimible encontrada en una sección."""

    section: str
    va: int
    text: str
    terminated: bool  # acaba en NUL, como toda cadena C real


@dataclass(frozen=True, slots=True)
class CategorizedString:
    """Cadena relevante de una categoría (RTTI incluye el nombre desmanglado)."""

    section: str
    va: int
    text: str
    demangled: str | None = None


@dataclass(frozen=True, slots=True)
class StringSummary:
    """Totales y cadenas relevantes por categoría."""

    total: int
    unique: int
    nul_terminated: int
    by_section: Mapping[str, int]
    categories: Mapping[StringCategory, tuple[CategorizedString, ...]]

    @property
    def category_counts(self) -> dict[StringCategory, int]:
        return {category: len(items) for category, items in self.categories.items()}

    @property
    def identifier_prefixes(self) -> dict[str, int]:
        """Histograma de prefijos ('STG_TOWER_05' -> 'STG'): sugiere subsistemas del motor."""
        identifiers = self.categories.get(StringCategory.ENGINE_IDENTIFIERS, ())
        return dict(Counter(item.text.split("_", 1)[0] for item in identifiers).most_common())


def extract_strings(image: XbeImage, min_len: int = DEFAULT_MIN_LEN) -> list[FoundString]:
    """Rachas ASCII imprimibles (admite \\t \\n \\r) de `min_len`+ caracteres.

    Solo se escanean los bytes que el archivo respalda (`backed_size`). Se omiten
    las secciones INSERTED_FILE: son binarios embebidos (iconos, IDs) y solo
    aportan ruido. Las rachas que no acaban en un NUL dentro de la sección (también
    las que llegan a su final) suelen ser trozos de código o de gráficos; se
    cuentan, pero no se clasifican.
    """
    if min_len < 1:
        raise ValueError(f"min_len debe ser >= 1 (es {min_len})")
    pattern = re.compile(rb"[\x20-\x7E\t\n\r]{%d,}" % min_len)
    found: list[FoundString] = []
    for section in image.sections:
        if section.header.is_inserted_file:
            continue
        start = section.header.raw_addr
        chunk = image.data[start : start + section.header.backed_size]
        for match in pattern.finditer(chunk):
            end = match.end()
            found.append(
                FoundString(
                    section=section.name,
                    va=section.header.virtual_addr + match.start(),
                    text=match.group().decode("ascii"),
                    terminated=end < len(chunk) and chunk[end] == 0,
                )
            )
    return found


def looks_like_text(text: str) -> bool:
    """Filtro anti-basura para rachas imprimibles que salen de código, tablas o gráficos.

    1. Al menos 3 letras y un 85 % o más de caracteres "normales".
    2. Pocos signos de puntuación distintos (tablas ASCII, código x86).
    3. Diversidad: los datos de píxeles repiten pocos caracteres ('UUU\\UeU\\').
    4. Sin rachas de 4+ caracteres iguales (salvo espacios, guiones, ceros...).
    """
    stripped = text.strip()
    if not stripped:
        return False
    letters = sum(char.isalpha() for char in stripped)
    normal = sum(char.isalnum() or char in TEXT_CHARS for char in stripped)
    punctuation = {char for char in stripped if not char.isalnum() and char != " "}
    if (
        letters < 3
        or normal / len(stripped) < MIN_TEXT_RATIO
        or len(punctuation) > MAX_DISTINCT_PUNCTUATION
    ):
        return False
    if len(set(stripped)) < min(0.4 * len(stripped), 8):
        return False
    return not any(run.group(1) not in REPEAT_OK for run in REPEAT_RUN_RE.finditer(stripped))


def is_path_like(text: str) -> bool:
    """Ruta plausible: 2+ segmentos, alguno con 3+ letras seguidas, y que no sea prosa.

    Con '\\' basta; con '/' la ruta debe ocupar casi toda la cadena (evita 'On/Off').
    Debe estar anclada: raíz ('c:\\', '\\', '/', '%s') o extensión/placeholder al final
    ('x.afs', 'movie\\%s'); así se descartan trozos de gráficos como 'gwv\\gwv_'.
    Descarta basura x86: un segmento acabado en '$' es el byte SIB de `[esp+disp]`
    (p. ej. 'D$\\PQR'), y un último segmento de 1 carácter no es un nombre real.
    """
    stripped = text.strip()
    for match in PATH_RE.finditer(stripped):
        path = match.group()
        parts = re.split(r"[\\/]", path)
        segments = [segment for segment in parts if segment]
        if len(segments) < 2 or not any(re.search(r"[A-Za-z]{3}", seg) for seg in segments):
            continue
        if any(segment.endswith("$") for segment in segments) or len(parts[-1]) == 1:
            continue
        rooted = bool(re.match(r"[A-Za-z]:[\\/]|[\\/]|%\w", path))
        anchored_tail = bool(re.search(r"\.[A-Za-z]\w{0,3}$|%\w+$", parts[-1] or parts[-2]))
        if not (rooted or anchored_tail):
            continue
        if "\\" in path or len(path) >= 0.8 * len(stripped):
            return True
    return False


def demangle_rtti(mangled: str) -> str:
    """'CPlayer@Game' (de '.?AVCPlayer@Game@@') -> 'Game::CPlayer'."""
    return "::".join(reversed(mangled.split("@")))


def rtti_type_name(text: str) -> str | None:
    """Nombre desmanglado si `text` contiene un descriptor RTTI de MSVC."""
    match = RTTI_RE.search(text)
    return demangle_rtti(match.group(1)) if match else None


def is_debug_message(text: str) -> bool:
    """Mensaje de desarrollador, no texto de UI.

    Basta una palabra fuerte; una débil necesita además una pista técnica; o un
    formato printf técnico (%x, %08x, %p...) dentro de una frase.
    """
    if UI_MARKUP_RE.search(text):
        return False
    if DEBUG_STRONG_RE.search(text):
        return True
    if DEBUG_WEAK_RE.search(text) and DEV_HINT_RE.search(text):
        return True
    return bool(TECH_PRINTF_RE.search(text)) and " " in text.strip()


def classify_string(text: str) -> list[StringCategory]:
    """Categorías a las que pertenece una cadena (puede ser más de una).

    Las de más de `MAX_CLASSIFIED_LEN` caracteres no pertenecen a ninguna.
    """
    if len(text) > MAX_CLASSIFIED_LEN:
        return []
    if rtti_type_name(text) is not None:
        return [StringCategory.RTTI_TYPES]
    if not looks_like_text(text):
        return []
    rules = (
        (StringCategory.SOURCE_PATHS, bool(SOURCE_FILE_RE.search(text)) or is_path_like(text)),
        (StringCategory.ASSERTS, bool(ASSERT_RE.search(text))),
        (StringCategory.DEBUG_MESSAGES, is_debug_message(text)),
        (StringCategory.ASSET_NAMES, bool(ASSET_RE.search(text))),
        (StringCategory.VERSION_STAMPS, bool(VERSION_STAMP_RE.search(text))),
        (StringCategory.ENGINE_IDENTIFIERS, bool(IDENTIFIER_RE.match(text))),
    )
    return [category for category, matches in rules if matches]


def summarize_strings(found: Sequence[FoundString]) -> StringSummary:
    """Totales y cadenas por categoría, deduplicadas por texto (o por tipo, en RTTI).

    Solo se clasifican las cadenas terminadas en NUL (ver `extract_strings`).
    """
    categories: dict[StringCategory, list[CategorizedString]] = {c: [] for c in StringCategory}
    seen: dict[StringCategory, set[str]] = {category: set() for category in StringCategory}
    for item in found:
        if not item.terminated:
            continue
        for category in classify_string(item.text):
            demangled = rtti_type_name(item.text) if category is StringCategory.RTTI_TYPES else None
            key = demangled if demangled is not None else item.text
            if key not in seen[category]:
                seen[category].add(key)
                categories[category].append(
                    CategorizedString(item.section, item.va, item.text, demangled)
                )
    return StringSummary(
        total=len(found),
        unique=len({item.text for item in found}),
        nul_terminated=sum(item.terminated for item in found),
        by_section=MappingProxyType(dict(Counter(item.section for item in found))),
        categories=MappingProxyType(
            {category: tuple(items) for category, items in categories.items()}
        ),
    )
