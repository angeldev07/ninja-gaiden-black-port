"""Inventario por streaming de todos los AFS de un directorio: fichas y agregados.

El resultado es un documento JSON cuya forma exacta fija `records`.
Cada entrada se resume con su cabecera, su tipo y la entropía de su ventana inicial;
los AFS anidados se inventarían en su sitio, hasta `MAX_NESTING` niveles, una sola vez
cada uno y con un tope de `MAX_ENTRY_RECORDS_PER_FILE` fichas por archivo físico.
"""

from __future__ import annotations

import datetime as dt
from collections import Counter, defaultdict
from collections.abc import Iterable, Iterator
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Final

from ngb_tools.afs.archive import (
    ATTR_ENTRY_SIZE,
    ATTR_PTR_SIZE,
    HEADER_SIZE,
    TOC_ENTRY_SIZE,
    AfsArchive,
    AfsEntry,
    ByteSource,
    open_afs,
    parse_header,
    read_at,
    read_entry_prefix,
)
from ngb_tools.afs.classify import (
    ENTROPY_WINDOW,
    Classification,
    EntropyBand,
    classify,
    describe_kind,
    entropy_band,
    head_ascii,
    shannon_entropy,
)
from ngb_tools.afs.errors import AfsFormatError, AfsUsageError
from ngb_tools.afs.invariants import Issue, IssueCode, custom_field_semantics, validate_archive
from ngb_tools.afs.records import (
    ArchiveRecord,
    AttrRecord,
    BandRecord,
    CustomFieldRecord,
    EntryRecord,
    ExampleRecord,
    ExtensionRecord,
    InvariantsRecord,
    InventoryRecord,
    IssueRecord,
    KindRecord,
    NestedRecord,
    SummaryRecord,
    TotalsRecord,
)

TOOL: Final = "ngb_tools.afs"
HEAD_BYTES: Final = 16
MAX_NESTING: Final = 3
# Presupuestos por archivo físico frente a AFS hostiles o corruptos. Acotan cosas distintas:
# - MAX_ENTRY_RECORDS_PER_FILE: fichas de entrada, sumando todos los niveles, y con ellas
#   las ventanas de 64 KB que se leen (como mucho 1 GiB). El AFS real más grande (spr.afs)
#   tiene 684 entradas. No acota el parseo de las TOC y los atributos de los hijos:
# - MAX_METADATA_BYTES_PER_FILE: bytes de cabecera, TOC y atributos que lee cada
#   `open_afs` de un AFS hijo, cobrados en el peor caso antes de abrirlo. La TOC del
#   propio archivo ya está acotada por su tamaño. Ninguno de los 206 AFS reales tiene hijos.
MAX_ENTRY_RECORDS_PER_FILE: Final = 16_384
MAX_METADATA_BYTES_PER_FILE: Final = 64 * 1024 * 1024
EXAMPLES_PER_KIND: Final = 3
EXAMPLES_PER_ISSUE: Final = 5
NULL_KIND: Final = "null"
UNREADABLE_LOCATION: Final = "error"
_NULL_CLASSIFICATION: Final = Classification(NULL_KIND, "")

# --- Fichas -------------------------------------------------------------------------------


@dataclass(slots=True)
class _FileWalk:
    """Estado del recorrido de un archivo físico, para acotar el trabajo ante AFS hostiles.

    Varias entradas que apuntan al mismo AFS hijo (o un AFS que se contiene a sí mismo)
    harían crecer el recorrido como N^MAX_NESTING. Por eso cada AFS, identificado por su
    (offset absoluto, tamaño), se inventaría una sola vez por archivo, y hay dos topes:
    fichas (`max_records`) y bytes de metadatos de los hijos (`max_metadata`). Al agotarse
    uno se anota una incidencia, se corta y la ficha del archivo queda `truncated`.
    """

    max_records: int
    max_metadata: int
    records_left: int
    metadata_left: int
    visited: set[tuple[int, int]] = field(default_factory=set)
    issues: list[Issue] = field(default_factory=list)
    records_exhausted: bool = False
    metadata_exhausted: bool = False

    @property
    def truncated(self) -> bool:
        return self.records_exhausted or self.metadata_exhausted

    def take_record(self, archive: AfsArchive, entry: AfsEntry) -> bool:
        """Gasta una ficha; al agotarse, lo anota una sola vez y devuelve False."""
        if self.records_left > 0:
            self.records_left -= 1
            return True
        if not self.records_exhausted:
            self.records_exhausted = True
            detail = (
                f"tope de {self.max_records} fichas por archivo alcanzado en "
                f"{archive.label}#{entry.index}; se omite el resto"
            )
            self.issues.append(Issue(IssueCode.ENTRY_BUDGET, detail))
        return False

    def take_metadata(self, cost: int, ref: str) -> bool:
        """Cobra `cost` bytes de metadatos; agotado el tope, no se abre ningún hijo más."""
        if not self.metadata_exhausted and cost <= self.metadata_left:
            self.metadata_left -= cost
            return True
        if not self.metadata_exhausted:
            self.metadata_exhausted = True
            detail = (
                f"tope de {self.max_metadata} bytes de metadatos de AFS anidados alcanzado "
                f"en {ref}; no se abren más hijos"
            )
            self.issues.append(Issue(IssueCode.METADATA_BUDGET, detail))
        return False


def nested_metadata_cost(count: int) -> int:
    """Peor caso de lo que lee `open_afs` de un hijo de `count` entradas.

    Cabecera, los dos punteros de atributos candidatos, la TOC y la tabla de atributos.
    """
    return HEADER_SIZE + 2 * ATTR_PTR_SIZE + count * (TOC_ENTRY_SIZE + ATTR_ENTRY_SIZE)


def issue_record(issue: Issue) -> IssueRecord:
    return IssueRecord(code=issue.code.value, index=issue.index, detail=issue.detail)


def _entry_record(
    source: ByteSource, archive: AfsArchive, entry: AfsEntry, depth: int, walk: _FileWalk
) -> EntryRecord:
    """Ficha de una entrada; si es un AFS aún no visitado, lo abre en su sitio y lo inventaría.

    Una entrada nula se trata como una ventana vacía: entropía 0 y banda `empty`.
    """
    window = b"" if entry.is_null else read_entry_prefix(source, entry, ENTROPY_WINDOW)
    found = _NULL_CLASSIFICATION if entry.is_null else classify(window)
    entropy = shannon_entropy(window)
    record = EntryRecord(
        index=entry.index,
        offset=entry.offset,
        rel_offset=entry.rel_offset,
        size=entry.size,
        name=entry.name,
        timestamp=entry.attr.timestamp if entry.attr is not None else None,
        kind=found.kind,
        ext=found.ext,
        entropy=round(entropy, 3),
        band=entropy_band(entropy, entry.size).value,
        head_hex=window[:HEAD_BYTES].hex(" "),
        head_ascii=head_ascii(window[:HEAD_BYTES]),
    )
    if found.kind != "AFS" or depth >= MAX_NESTING:
        return record
    ref, key = f"{archive.label}#{entry.index}", (entry.offset, entry.size)
    if key in walk.visited:
        where = f"{entry.offset:#x} ({entry.size:#x} B)"
        record["nested_error"] = f"AFS ya inventariado en este archivo: {where}"
        walk.issues.append(Issue(IssueCode.NESTED_REVISIT, f"{ref} vuelve al AFS de {where}"))
        return record
    walk.visited.add(key)
    try:
        # Se lee solo la cabecera para saber N y cobrar el peor caso antes de abrirlo.
        _, count = parse_header(read_at(source, entry.offset, min(HEADER_SIZE, entry.size)))
        # Si la TOC declarada no cabe en la entrada, open_afs la rechaza sin leerla: no se cobra,
        # para que un N hostil no agote el presupuesto de los hijos siguientes.
        toc_fits = HEADER_SIZE + count * TOC_ENTRY_SIZE <= entry.size
        if toc_fits and not walk.take_metadata(nested_metadata_cost(count), ref):
            record["nested_error"] = "presupuesto de metadatos agotado: no se abre"
            return record
        child = open_afs(source, ref, base=entry.offset, limit=entry.size)
    except AfsFormatError as err:
        record["nested_error"] = str(err)
    else:
        record["nested"] = _archive_record(source, child, depth + 1, walk)
    return record


def _archive_record(
    source: ByteSource, archive: AfsArchive, depth: int, walk: _FileWalk
) -> ArchiveRecord:
    """Ficha de un AFS. Los recuentos salen de las entradas listadas, no de la TOC entera,
    para que cuadren con los totales aunque el recorrido se corte; `count` es el de la TOC.
    """
    entries: list[EntryRecord] = []
    cut = False
    for entry in archive.entries:
        if not walk.take_record(archive, entry):
            cut = True
            break
        entries.append(_entry_record(source, archive, entry, depth, walk))
    listed = archive.entries[: len(entries)]
    data = [e for e in listed if not e.is_null]
    ptr = archive.attr_ptr
    custom = custom_field_semantics(source, archive)
    record = ArchiveRecord(
        label=archive.label,
        size=archive.limit,
        magic=archive.magic.decode("latin-1"),
        count=archive.count,
        null_entries=len(listed) - len(data),
        has_names=archive.has_names,
        attr=AttrRecord(location=ptr.location.value, offset=ptr.offset, size=ptr.size),
        entries_bytes=sum(e.size for e in data),
        first_data_offset=min((e.rel_offset for e in data), default=0),
        custom_field=CustomFieldRecord(
            equals_header_u32=custom.equals_header_u32,
            equals_size=custom.equals_size,
            neither=custom.neither,
        ),
        issues=[issue_record(i) for i in validate_archive(archive)],
        entries=entries,
    )
    if cut:
        record["truncated"] = True
    return record


def inventory_archive(
    source: ByteSource,
    archive: AfsArchive,
    *,
    max_records: int = MAX_ENTRY_RECORDS_PER_FILE,
    max_metadata: int = MAX_METADATA_BYTES_PER_FILE,
) -> ArchiveRecord:
    """Ficha de un AFS ya abierto, con la de sus entradas y la de sus AFS anidados."""
    walk = _FileWalk(
        max_records=max_records,
        max_metadata=max_metadata,
        records_left=max_records,
        metadata_left=max_metadata,
    )
    walk.visited.add((archive.base, archive.limit))
    record = _archive_record(source, archive, 0, walk)
    record["issues"].extend(issue_record(i) for i in walk.issues)
    if walk.truncated:
        record["truncated"] = True
    return record


def unreadable_record(label: str, size: int, error: AfsFormatError | OSError) -> ArchiveRecord:
    """Ficha de un archivo que no se pudo leer: queda registrado, no aborta el inventario."""
    return ArchiveRecord(
        label=label,
        size=size,
        magic="",
        count=0,
        null_entries=0,
        has_names=False,
        attr=AttrRecord(location=UNREADABLE_LOCATION, offset=0, size=0),
        entries_bytes=0,
        first_data_offset=0,
        custom_field=CustomFieldRecord(equals_header_u32=0, equals_size=0, neither=0),
        issues=[issue_record(Issue(IssueCode.UNREADABLE, str(error)))],
        entries=[],
        error=str(error),
    )


def _size_or_zero(path: Path) -> int:
    try:
        return path.stat().st_size
    except OSError:
        return 0


def inventory_file(
    path: Path,
    *,
    max_records: int = MAX_ENTRY_RECORDS_PER_FILE,
    max_metadata: int = MAX_METADATA_BYTES_PER_FILE,
) -> ArchiveRecord:
    """Ficha de un .afs en disco; un fallo de formato o de E/S queda como `unreadable`."""
    try:
        with path.open("rb") as source:
            archive = open_afs(source, path.name)
            record = inventory_archive(
                source, archive, max_records=max_records, max_metadata=max_metadata
            )
    except (AfsFormatError, OSError) as err:
        record = unreadable_record(path.name, _size_or_zero(path), err)
    record["file"] = str(path)
    return record


# --- Agregados ----------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class FlatEntry:
    """Una entrada de cualquier nivel con su referencia (`a.afs#3#0`) y su profundidad."""

    ref: str
    entry: EntryRecord
    depth: int


def iter_flat_entries(archives: Iterable[ArchiveRecord], depth: int = 0) -> Iterator[FlatEntry]:
    """Recorrido en preorden: las entradas de un AFS anidado van tras la que lo contiene."""
    for archive in archives:
        for entry in archive["entries"]:
            yield FlatEntry(f"{archive['label']}#{entry['index']}", entry, depth)
            if "nested" in entry:
                yield from iter_flat_entries((entry["nested"],), depth + 1)


def name_extension(name: str | None) -> str:
    """Extensión del nombre en minúsculas, o la etiqueta del cubo sin nombre/sin extensión."""
    if not name:
        return "(sin nombre)"
    return PurePosixPath(name).suffix.lower() or "(sin extensión)"


@dataclass(slots=True)
class _KindTally:
    entries: int = 0
    nbytes: int = 0
    entropy_sum: float = 0.0
    bands: Counter[str] = field(default_factory=Counter)
    exts: Counter[str] = field(default_factory=Counter)
    examples: list[ExampleRecord] = field(default_factory=list)


@dataclass(slots=True)
class _ExtTally:
    entries: int = 0
    nbytes: int = 0
    entropy_sum: float = 0.0
    kinds: Counter[str] = field(default_factory=Counter)


def _mean(total: float, n: int) -> float:
    return round(total / n, 3)


def _kind_histogram(flat: Iterable[FlatEntry]) -> list[KindRecord]:
    """Tipos por número de entradas (y bytes); los empates conservan el orden de aparición."""
    tallies: dict[str, _KindTally] = {}
    for item in flat:
        e = item.entry
        tally = tallies.setdefault(e["kind"], _KindTally())
        tally.entries += 1
        tally.nbytes += e["size"]
        tally.entropy_sum += e["entropy"]
        tally.bands[e["band"]] += 1
        tally.exts[name_extension(e["name"])] += 1
        if len(tally.examples) < EXAMPLES_PER_KIND:
            example = ExampleRecord(
                ref=item.ref, name=e["name"], size=e["size"], head_hex=e["head_hex"]
            )
            tally.examples.append(example)
    ranked = sorted(tallies.items(), key=lambda kv: (-kv[1].entries, -kv[1].nbytes))
    return [
        KindRecord(
            kind=kind,
            entries=t.entries,
            bytes=t.nbytes,
            bands=dict(t.bands),
            exts=dict(t.exts.most_common(5)),
            examples=t.examples,
            entropy_mean=_mean(t.entropy_sum, t.entries),
            description=describe_kind(kind),
        )
        for kind, t in ranked
    ]


def _name_extensions(flat: Iterable[FlatEntry]) -> list[ExtensionRecord]:
    tallies: dict[str, _ExtTally] = {}
    for item in flat:
        e = item.entry
        tally = tallies.setdefault(name_extension(e["name"]), _ExtTally())
        tally.entries += 1
        tally.nbytes += e["size"]
        tally.entropy_sum += e["entropy"]
        tally.kinds[e["kind"]] += 1
    ranked = sorted(tallies.items(), key=lambda kv: -kv[1].entries)
    return [
        ExtensionRecord(
            ext=ext,
            entries=t.entries,
            bytes=t.nbytes,
            entropy_mean=_mean(t.entropy_sum, t.entries),
            kinds=dict(t.kinds.most_common(4)),
        )
        for ext, t in ranked
    ]


def _entropy_bands(flat: Iterable[FlatEntry]) -> dict[str, BandRecord]:
    bands = {band.value: BandRecord(entries=0, bytes=0) for band in EntropyBand}
    for item in flat:
        bands[item.entry["band"]]["entries"] += 1
        bands[item.entry["band"]]["bytes"] += item.entry["size"]
    return bands


def _nested_afs(flat: Iterable[FlatEntry]) -> list[NestedRecord]:
    nested: list[NestedRecord] = []
    for item in flat:
        e = item.entry
        if "nested" in e or "nested_error" in e:
            child = e.get("nested")
            nested.append(
                NestedRecord(
                    ref=item.ref,
                    name=e["name"],
                    size=e["size"],
                    count=child["count"] if child is not None else None,
                    error=e.get("nested_error"),
                )
            )
    return nested


def _invariants(archives: Iterable[ArchiveRecord]) -> InvariantsRecord:
    counts: Counter[str] = Counter()
    examples: defaultdict[str, list[str]] = defaultdict(list)
    ok = 0
    for archive in archives:
        ok += not archive["issues"]
        for issue in archive["issues"]:
            counts[issue["code"]] += 1
            if len(examples[issue["code"]]) < EXAMPLES_PER_ISSUE:
                index = issue["index"]
                where = archive["label"] if index is None else f"{archive['label']}#{index}"
                examples[issue["code"]].append(f"{where}: {issue['detail']}")
    return InvariantsRecord(
        archives_ok=ok, issue_counts=dict(counts), issue_examples=dict(examples)
    )


def _totals(archives: list[ArchiveRecord], flat: list[FlatEntry]) -> TotalsRecord:
    top = [item.entry for item in flat if item.depth == 0]
    named = sum(e["name"] is not None for e in top)
    return TotalsRecord(
        archives=len(archives),
        archives_with_names=sum(a["has_names"] for a in archives),
        entries=len(top),
        entries_all_levels=len(flat),
        null_entries=sum(item.entry["kind"] == NULL_KIND for item in flat),
        named_entries=named,
        pct_named=round(100.0 * named / len(top), 2) if top else 0.0,
        archive_bytes=sum(a["size"] for a in archives),
        entry_bytes=sum(a["entries_bytes"] for a in archives),
    )


def aggregate(archives: list[ArchiveRecord]) -> SummaryRecord:
    """Totales, histograma de tipos, bandas de entropía, anidados, extensiones e invariantes."""
    flat = list(iter_flat_entries(archives))
    with_data = [item for item in flat if item.entry["kind"] != NULL_KIND]
    custom: Counter[str] = Counter()
    for archive in archives:
        custom.update(archive["custom_field"])
    return SummaryRecord(
        totals=_totals(archives, flat),
        attr_pointer_locations=dict(Counter(a["attr"]["location"] for a in archives)),
        custom_field=dict(custom),
        magic_histogram=_kind_histogram(with_data),
        entropy_bands=_entropy_bands(with_data),
        nested_afs=_nested_afs(with_data),
        name_extensions=_name_extensions(with_data),
        invariants=_invariants(archives),
    )


def find_archives(root: Path) -> list[Path]:
    """Todos los .afs bajo `root`, en orden alfabético de ruta."""
    if not root.is_dir():
        raise AfsUsageError(f"no es un directorio: {root}")
    return sorted(p for p in root.rglob("*") if p.is_file() and p.suffix.lower() == ".afs")


def build_inventory(root: Path, *, generated: dt.datetime | None = None) -> InventoryRecord:
    """Inventaría todos los .afs bajo `root` y agrega. `generated` fija el reloj (tests)."""
    archives = [inventory_file(p) for p in find_archives(root)]
    stamp = generated if generated is not None else dt.datetime.now()
    return InventoryRecord(
        generated=stamp.isoformat(timespec="seconds"),
        root=str(root),
        tool=TOOL,
        entropy_window=ENTROPY_WINDOW,
        summary=aggregate(archives),
        archives=archives,
    )
