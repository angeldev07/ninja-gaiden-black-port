"""ASCII strings of the sections and their heuristic classification.

A stripped executable still talks: source-tree paths, asserts,
error messages, asset names, middleware version stamps,
engine identifiers and MSVC RTTI class names. The heuristics are
cheap and explainable; losing some real string is preferred to filling the
report with noise from x86 code or graphics.

Bounded cost against a hostile XBE: all regular expressions are linear in
the string length (the path and asset ones only start trying at the beginning of
a token, see `PATH_RE`), and in addition runs longer than `MAX_CLASSIFIED_LEN`
characters are counted but not classified.
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
# The longest real NGB strings are around 1700 characters (the legal text of the
# WARNING screen); a longer run is a table, padding or an attempt to exhaust the
# heuristics, and it is counted but not classified.
MAX_CLASSIFIED_LEN: Final = 4096

SOURCE_FILE_RE = re.compile(r"[\w\-]\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|inl|asm)\b", re.IGNORECASE)
# An attempt that starts in the middle of a token (a path character preceded by another) never
# gives the first match of `finditer`: the attempt from the start of the token reaches the same
# separator. The exception is the drive letter (`xc:\`), because ':' cuts the token. So
# it is only tried at the start of a token, at a separator or at a drive letter: the search
# is linear and the matches are the same. Without this, a long run without separators
# costs O(n^2).
PATH_RE = re.compile(
    r"(?:(?<![\w.\-~$%])|(?=[\\/]|[A-Za-z]:))"
    r"(?:[A-Za-z]:)?[\\/]{0,2}(?:[\w.\-~$%]{2,}[\\/])+[\w.\-~$%]*"
)
ASSERT_RE = re.compile(r"\bassert|\b\w+\s+(?:==|!=|<=|>=|&&|\|\|)\s+\w+", re.IGNORECASE)
# "Strong" debug words (almost always developer messages) and "weak" ones
# (they also appear in UI or dialogue text; they additionally require a technical hint).
DEBUG_STRONG_RE = re.compile(
    r"\b(?:error|warning|fail(?:ed|ure)?|invalid|illegal|overflow|out of memory|fatal|abort|"
    r"exception|unsupported|timeout|leak|parameter|assert)\b|status=",
    re.IGNORECASE,
)
DEBUG_WEAK_RE = re.compile(
    r"\b(?:cannot|can't|couldn't|unable|not found|unknown|missing|bad|debug)\b", re.IGNORECASE
)
TECH_PRINTF_RE = re.compile(r"%[-+ 0#]*\d*l?[xXp]")  # %x, %08x, %p: technical dumps
# Technical hint: printf, error code (E1234), identifier_with_underscores, Camel/call().
DEV_HINT_RE = re.compile(r"%[-+ 0#]*\d*[sdiuxXp]|\bE?\d{4,}\b|\w_\w|[a-z][A-Z]\w*|\w\(\)|::")
# NGB UI button markup ('@BA' = button A): gives away interface text.
UI_MARKUP_RE = re.compile(r"@B[A-Z]")
REPEAT_OK = frozenset(" -=_.*0#~")  # characters that do repeat in real text
ASSET_EXTENSIONS = (
    "afs|xpr|xbr|xwb|xsb|xap|wav|wma|adx|aix|sfd|bin|dat|tex|tmc|mdl|dds|bmp|tga|png|jpg|"
    "jpeg|xmv|wmv|def|ins|ini|txt|xbe|ttf|fnt|cfg|lst|tbl|mot|anm|sav|xbx|xsv|xmp|fx|vsh|psh|xvu"
)
# Same trick as in PATH_RE: it is only tried at the start of a token.
ASSET_RE = re.compile(rf"(?<![\w\-%])[\w\-%]+\.(?:{ASSET_EXTENSIONS})\b", re.IGNORECASE)
# MSVC RTTI type descriptor: '.?AV' (class), '.?AU' (struct), '.?AW' (enum).
RTTI_RE = re.compile(r"\.\?A[VUW]([A-Za-z_]\w*(?:@[A-Za-z_]\w*)*)@@")
VERSION_STAMP_RE = re.compile(r"\bVer(?:sion)?\.?\s*\d+\.\d+|\bBuild\s*:", re.IGNORECASE)
IDENTIFIER_RE = re.compile(r"^(?:[A-Z][A-Z0-9]*(?:_[A-Z0-9]+)+|[a-z][a-z0-9]*(?:_[a-z0-9]+)+)$")
REPEAT_RUN_RE = re.compile(r"(.)\1{3,}")
TEXT_CHARS = frozenset(" _.-/\\:%()[],'=<>!&|*+?\"~$")
MAX_DISTINCT_PUNCTUATION = 8  # more distinct marks: table or machine code
MIN_TEXT_RATIO = 0.85


class StringCategory(StrEnum):
    """Report categories, in the order they are presented."""

    SOURCE_PATHS = "source_paths"
    ASSERTS = "asserts"
    DEBUG_MESSAGES = "debug_messages"
    ASSET_NAMES = "asset_names"
    VERSION_STAMPS = "version_stamps"
    ENGINE_IDENTIFIERS = "engine_identifiers"
    RTTI_TYPES = "rtti_types"


@dataclass(frozen=True, slots=True)
class FoundString:
    """Printable run found in a section."""

    section: str
    va: int
    text: str
    terminated: bool  # ends in NUL, like any real C string


@dataclass(frozen=True, slots=True)
class CategorizedString:
    """Relevant string of a category (RTTI includes the demangled name)."""

    section: str
    va: int
    text: str
    demangled: str | None = None


@dataclass(frozen=True, slots=True)
class StringSummary:
    """Totals and relevant strings per category."""

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
        """Prefix histogram ('STG_TOWER_05' -> 'STG'): hints at engine subsystems."""
        identifiers = self.categories.get(StringCategory.ENGINE_IDENTIFIERS, ())
        return dict(Counter(item.text.split("_", 1)[0] for item in identifiers).most_common())


def extract_strings(image: XbeImage, min_len: int = DEFAULT_MIN_LEN) -> list[FoundString]:
    """Printable ASCII runs (allows \\t \\n \\r) of `min_len`+ characters.

    Only the bytes the file backs (`backed_size`) are scanned. INSERTED_FILE
    sections are skipped: they are embedded binaries (icons, IDs) and only
    add noise. Runs that do not end in a NUL inside the section (including
    those reaching its end) tend to be chunks of code or graphics; they are
    counted, but not classified.
    """
    if min_len < 1:
        raise ValueError(f"min_len must be >= 1 (got {min_len})")
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
    """Anti-garbage filter for printable runs coming from code, tables or graphics.

    1. At least 3 letters and 85 % or more "normal" characters.
    2. Few distinct punctuation marks (ASCII tables, x86 code).
    3. Diversity: pixel data repeats few characters ('UUU\\UeU\\').
    4. No runs of 4+ equal characters (except spaces, dashes, zeros...).
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
    """Plausible path: 2+ segments, one with 3+ consecutive letters, and not prose.

    With '\\' it is enough; with '/' the path must take up almost the whole string (avoids
    'On/Off').
    It must be anchored: root ('c:\\', '\\', '/', '%s') or extension/placeholder at the end
    ('x.afs', 'movie\\%s'); this discards graphics chunks such as 'gwv\\gwv_'.
    Discards x86 garbage: a segment ending in '$' is the SIB byte of `[esp+disp]`
    (e.g. 'D$\\PQR'), and a last segment of 1 character is not a real name.
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
    """'CPlayer@Game' (from '.?AVCPlayer@Game@@') -> 'Game::CPlayer'."""
    return "::".join(reversed(mangled.split("@")))


def rtti_type_name(text: str) -> str | None:
    """Demangled name if `text` contains an MSVC RTTI descriptor."""
    match = RTTI_RE.search(text)
    return demangle_rtti(match.group(1)) if match else None


def is_debug_message(text: str) -> bool:
    """Developer message, not UI text.

    One strong word is enough; a weak one also needs a technical hint; or a
    technical printf format (%x, %08x, %p...) inside a sentence.
    """
    if UI_MARKUP_RE.search(text):
        return False
    if DEBUG_STRONG_RE.search(text):
        return True
    if DEBUG_WEAK_RE.search(text) and DEV_HINT_RE.search(text):
        return True
    return bool(TECH_PRINTF_RE.search(text)) and " " in text.strip()


def classify_string(text: str) -> list[StringCategory]:
    """Categories a string belongs to (may be more than one).

    Those longer than `MAX_CLASSIFIED_LEN` characters belong to none.
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
    """Totals and strings per category, deduplicated by text (or by type, for RTTI).

    Only NUL-terminated strings are classified (see `extract_strings`).
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
