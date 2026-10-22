"""Markdown summary of the analysis: readable tables, trimmed string lists.

The JSON is the full report; the Markdown is for reading in the vault, so it
trims the long lists and points to the JSON.
"""

from __future__ import annotations

from collections.abc import Callable, Mapping

from ngb_tools.xbe.analysis import XbeAnalysis
from ngb_tools.xbe.binary import hex32
from ngb_tools.xbe.strings import StringCategory, StringSummary
from ngb_tools.xbe.structs import XBE_MAGIC, LibraryVersion, format_title_id

NONE_MARK = "—"
MD_STRING_LIMITS: Mapping[StringCategory, int] = {
    StringCategory.SOURCE_PATHS: 80,
    StringCategory.ASSERTS: 40,
    StringCategory.DEBUG_MESSAGES: 60,
    StringCategory.ASSET_NAMES: 80,
    StringCategory.VERSION_STAMPS: 40,
    StringCategory.ENGINE_IDENTIFIERS: 0,  # summarized with the prefix histogram
    StringCategory.RTTI_TYPES: 80,
}
MD_STRING_TITLES: Mapping[StringCategory, str] = {
    StringCategory.SOURCE_PATHS: "Source paths / names",
    StringCategory.ASSERTS: "Asserts",
    StringCategory.DEBUG_MESSAGES: "Debug / error messages",
    StringCategory.ASSET_NAMES: "Asset file names",
    StringCategory.VERSION_STAMPS: "Version stamps (middleware)",
    StringCategory.ENGINE_IDENTIFIERS: "Engine identifiers (snake_case / UPPER_CASE)",
    StringCategory.RTTI_TYPES: "RTTI types (MSVC)",
}
MD_PREFIX_LIMIT = 40  # identifier prefixes shown
DIGEST_MARKS: Mapping[bool | None, str] = {True: "ok", False: "BAD", None: NONE_MARK}


def h32(value: int | None) -> str:
    """32-bit hex, or a dash if there is no value."""
    return NONE_MARK if value is None else hex32(value)


def md_code(text: str | None) -> str:
    """Inline code that does not break the table: escapes '|' and newlines, tolerates backticks."""
    if not text:
        return NONE_MARK
    escaped = (
        text.replace("|", "\\|").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
    )
    return f"`` {escaped} ``" if "`" in escaped else f"`{escaped}`"


def _table(title: str, rows: list[tuple[str, str]]) -> list[str]:
    lines = [title, "", "| Field | Value |", "|---|---|"]
    lines += [f"| {name} | {value} |" for name, value in rows]
    return [*lines, ""]


def md_image_header(analysis: XbeAnalysis) -> list[str]:
    h, xor, dbg = analysis.image.header, analysis.image.xor, analysis.debug_strings
    ambiguous = " (ambiguous)" if xor.ambiguous else ""
    rows = [
        ("Magic", XBE_MAGIC.decode("ascii")),
        ("Base address", h32(h.base_address)),
        (
            "Size of headers / image / image header",
            f"0x{h.size_of_headers:X} / 0x{h.size_of_image:X} / 0x{h.size_of_image_header:X}",
        ),
        ("TimeDate", f"{h32(h.timedate)} ({h.timedate_iso})"),
        ("Init flags", f"{h32(h.init_flags)} {', '.join(h.init_flags_decoded) or '(none)'}"),
        (
            "Entry point",
            f"{h32(xor.entry_point)} (encoded {h32(h.entry_point_encoded)}, "
            f"section {md_code(analysis.entry_point_section)})",
        ),
        (
            "Kernel thunk",
            f"{h32(xor.kernel_thunk_addr)} (encoded {h32(h.kernel_thunk_encoded)})",
        ),
        ("Build type (XOR)", f"**{xor.build_kind}**{ambiguous}"),
        ("TLS addr", h32(h.tls_addr)),
        ("Stack size", f"0x{h.stack_size:X}"),
        ("PE heap reserve / commit", f"0x{h.pe_heap_reserve:X} / 0x{h.pe_heap_commit:X}"),
        ("PE base / size of image", f"{h32(h.pe_base_address)} / 0x{h.pe_size_of_image:X}"),
        ("PE checksum", h32(h.pe_checksum)),
        ("PE TimeDate", f"{h32(h.pe_timedate)} ({h.pe_timedate_iso})"),
        ("Debug pathname", md_code(dbg.pathname)),
        ("Debug filename", md_code(dbg.filename)),
        ("Debug unicode filename", md_code(dbg.unicode_filename)),
        ("Non-kernel import dir", h32(h.non_kernel_import_dir_addr)),
        ("Logo bitmap addr / size", f"{h32(h.logo_bitmap_addr)} / {h.logo_bitmap_size} bytes"),
    ]
    if h.debug_info_addr is not None:
        rows.append(("Debug info addr (0x180)", h32(h.debug_info_addr)))
    codeview = analysis.codeview
    if codeview is not None:
        rows.append(("CodeView PDB", md_code(codeview.pdb_path)))
        rows.append(
            (
                "CodeView GUID / age",
                f"`{codeview.guid}` / {codeview.age} (id `{codeview.pdb_id}`)",
            )
        )
    return _table("## Image header", rows)


def md_certificate(analysis: XbeAnalysis) -> list[str]:
    c = analysis.certificate
    alternates = ", ".join(format_title_id(title) for title in c.alternate_title_ids)
    rows = [
        ("Title ID", f"{h32(c.title_id)} → **{c.title_id_str}**"),
        ("Title name", md_code(c.title_name)),
        ("Alternate title IDs", alternates or "(none)"),
        ("TimeDate", f"{h32(c.timedate)} ({c.timedate_iso})"),
        ("Allowed media", f"{h32(c.allowed_media)} {', '.join(c.allowed_media_decoded)}"),
        ("Game region", f"{h32(c.game_region)} {', '.join(c.game_region_decoded)}"),
        ("Game ratings", h32(c.game_ratings)),
        ("Disk number", str(c.disk_number)),
        ("Version", f"{h32(c.version)} ({c.version})"),
        ("Certificate size", f"0x{c.size:X}"),
    ]
    if c.online_service is not None:
        rows.append(("Online service", f"{h32(c.online_service)} {md_code(c.online_service_text)}"))
    if c.security_flags is not None:
        rows.append(("Security flags", h32(c.security_flags)))
    return _table("## Certificate", rows)


def md_sections(analysis: XbeAnalysis) -> list[str]:
    sections = analysis.image.sections
    lines = [
        f"## Sections ({len(sections)})",
        "",
        "| # | Name | Flags | VA | VSize | Raw | RawSize | SHA-1 |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for section in sections:
        s = section.header
        flags = " ".join(s.flags_decoded) or NONE_MARK
        lines.append(
            f"| {section.index} | {md_code(section.name)} | {flags} | "
            f"{h32(s.virtual_addr)} | 0x{s.virtual_size:X} | {h32(s.raw_addr)} | "
            f"0x{s.raw_size:X} | {DIGEST_MARKS[section.digest_ok]} |"
        )
    return [*lines, ""]


def _library_line(label: str, library: LibraryVersion | None) -> str:
    value = f"{md_code(library.name)} {library.version}" if library else NONE_MARK
    return f"- {label}: {value}"


def md_libraries(analysis: XbeAnalysis) -> list[str]:
    guess = analysis.xdk_build_guess
    lines = [
        f"## Libraries ({len(analysis.libraries)}) — estimated XDK build: "
        f"{guess if guess is not None else NONE_MARK}",
        "",
        "| Name | Version | QFE | Approved | Debug |",
        "|---|---|---|---|---|",
    ]
    for lib in analysis.libraries:
        debug = "yes" if lib.debug_build else "no"
        lines.append(
            f"| {md_code(lib.name)} | {lib.version} | {lib.qfe} | {lib.approved_text} | {debug} |"
        )
    lines += ["", _library_line("Kernel lib", analysis.kernel_library_version)]
    lines += ["", _library_line("XAPI lib", analysis.xapi_library_version)]
    return [*lines, ""]


def md_tls(analysis: XbeAnalysis) -> list[str]:
    tls = analysis.tls
    if tls is None:
        return ["## TLS", "", "No TLS directory.", ""]
    fields = (
        ("raw_data_start_addr", tls.raw_data_start_addr),
        ("raw_data_end_addr", tls.raw_data_end_addr),
        ("index_addr", tls.index_addr),
        ("callbacks_addr", tls.callbacks_addr),
        ("size_of_zero_fill", tls.size_of_zero_fill),
        ("characteristics", tls.characteristics),
    )
    return ["## TLS", "", *(f"- {name}: {h32(value)}" for name, value in fields), ""]


def md_kernel_imports(analysis: XbeAnalysis) -> list[str]:
    kernel = analysis.kernel
    imports = kernel.imports if kernel else ()
    lines = [f"## Kernel imports ({len(imports)})", ""]
    if kernel:
        lines += [
            f"Table at VA {h32(kernel.va)} (file {h32(kernel.file_offset)}, "
            f"section {md_code(kernel.section)}).",
            "",
        ]
    lines += ["| Slot VA | Ordinal | Name | Kind | Conv. |", "|---|---|---|---|---|"]
    for item in imports:
        conv = item.callconv.value if item.callconv else NONE_MARK
        if item.arg_bytes is not None:
            conv += f" ({item.arg_bytes} B)"
        ordinal = NONE_MARK if item.ordinal is None else str(item.ordinal)
        kind = item.kind.value if item.kind else NONE_MARK
        lines.append(
            f"| {h32(item.slot_va)} | {ordinal} | {md_code(item.name)} | {kind} | {conv} |"
        )
    return [*lines, ""]


def md_identifier_prefixes(summary: StringSummary) -> list[str]:
    """Prefix histogram with one example of each."""
    prefixes = list(summary.identifier_prefixes.items())
    if not prefixes:
        return []
    examples: dict[str, str] = {}
    for item in summary.categories[StringCategory.ENGINE_IDENTIFIERS]:
        examples.setdefault(item.text.split("_", 1)[0], item.text)
    lines = ["| Prefix | N | Example |", "|---|---|---|"]
    for prefix, count in prefixes[:MD_PREFIX_LIMIT]:
        lines.append(f"| {md_code(prefix)} | {count} | {md_code(examples[prefix])} |")
    if len(prefixes) > MD_PREFIX_LIMIT:
        lines.append(f"| … | +{len(prefixes) - MD_PREFIX_LIMIT} prefixes | see JSON |")
    return lines


def md_strings(analysis: XbeAnalysis) -> list[str]:
    summary = analysis.strings
    if summary is None:
        return []
    by_section = ", ".join(
        f"{md_code(name)}: {count}" for name, count in summary.by_section.items()
    )
    lines = [
        "## Strings",
        "",
        f"- Total: {summary.total} (unique: {summary.unique}; NUL-terminated: "
        f"{summary.nul_terminated}, the only ones that are classified)",
        f"- By section: {by_section}",
        "",
    ]
    for category in StringCategory:
        items = summary.categories[category]
        limit = MD_STRING_LIMITS[category]
        lines += [f"### {MD_STRING_TITLES[category]} ({len(items)})", ""]
        if category is StringCategory.ENGINE_IDENTIFIERS:
            lines += md_identifier_prefixes(summary)
        for item in items[:limit]:
            text = item.demangled if item.demangled is not None else item.text
            lines.append(f"- {h32(item.va)} {item.section}: {md_code(text)}")
        if 0 < limit < len(items):
            lines.append(f"- … +{len(items) - limit} more in the JSON")
        lines.append("")
    return lines


SECTIONS: tuple[Callable[[XbeAnalysis], list[str]], ...] = (
    md_image_header,
    md_certificate,
    md_sections,
    md_libraries,
    md_tls,
    md_kernel_imports,
    md_strings,
)


def render_markdown(analysis: XbeAnalysis) -> str:
    """Readable summary; the title is the file name."""
    image = analysis.image
    lines = [
        f"# XBE: {image.name}",
        "",
        f"- Size: {len(image.data)} bytes",
        f"- SHA-256: `{analysis.sha256}`",
        "",
    ]
    for part in SECTIONS:
        lines += part(analysis)
    return "\n".join(lines).rstrip() + "\n"
