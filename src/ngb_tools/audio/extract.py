"""WAV extraction of ADX (in AFS), `.wva` banks (in AFS) and XWB wave banks.

All the output derives from copyrighted data, so the folder goes through
`ngb_tools._paths.ensure_outside_repos` with no exceptions (not even `build/`). By default
it writes to `$NGB_DATA/extracted/audio/<source path inside game/, without extension>`.
Each WAV is written atomically and, at the end, `_manifest.json` with one card per track:
format, duration, loop, signal statistics and status (`ok`, `encrypted`, `unsupported` or
`error`). A track that cannot be decoded is reported and skipped: it does not abort the batch.
"""

from __future__ import annotations

import json
from array import array
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Final, Literal, TypedDict

from ngb_tools._paths import (
    EXTRACTED_SUBDIR,
    GAME_SUBDIR,
    atomic_write_bytes,
    atomic_write_chunks,
    data_root,
    ensure_outside_repos,
)
from ngb_tools.afs import AfsArchive, AfsEntry, AfsError, iter_entry_chunks, open_afs
from ngb_tools.afs import read_entry_prefix as read_prefix
from ngb_tools.audio import adx, wva, xbox_adpcm, xwb
from ngb_tools.audio.analysis import signal_stats
from ngb_tools.audio.errors import (
    AudioFormatError,
    AudioUsageError,
    EncryptedAdxError,
    UnsupportedCodecError,
)
from ngb_tools.audio.wav import iter_wav_chunks

AUDIO_SUBDIR: Final = "audio"
MANIFEST_NAME: Final = "_manifest.json"
UNNAMED: Final = "unnamed"
_BAD_CHARS: Final = frozenset('<>:"/\\|?*')

Status = Literal["ok", "encrypted", "unsupported", "error"]
Kind = Literal["adx", "wva", "xwb"]
STATUSES: Final[tuple[Status, ...]] = ("ok", "encrypted", "unsupported", "error")


class LoopRecord(TypedDict):
    start_sample: int
    end_sample: int


class TrackRecord(TypedDict):
    index: int
    slot: int | None
    name: str | None
    file: str | None
    status: Status
    detail: str | None
    codec: str
    channels: int
    sample_rate: int
    samples: int
    duration_s: float
    loop: LoopRecord | None
    peak: int | None
    clipped: int | None
    rms_dbfs: float | None


class SummaryRecord(TypedDict):
    total: int
    ok: int
    encrypted: int
    unsupported: int
    error: int


class AudioManifest(TypedDict):
    source: str
    kind: Kind
    out_dir: str
    summary: SummaryRecord
    tracks: list[TrackRecord]


OnTrack = Callable[[TrackRecord], None]


@dataclass(frozen=True, slots=True)
class _Format:
    codec: str
    channels: int
    sample_rate: int
    samples: int
    loop: LoopRecord | None = None


# --- Paths --------------------------------------------------------------------------------


def default_audio_dir(source: Path, environ: Mapping[str, str] | None = None) -> Path:
    """`$NGB_DATA/extracted/audio/<path of source inside game/ without extension>`.

    `game/bgm.afs` goes to `extracted/audio/bgm` and `game/Media/Wave.xwb` to
    `extracted/audio/Media/Wave`. A source outside the data root uses only its name.
    """
    root = data_root(environ)
    resolved = source.resolve()
    for base in ((root / GAME_SUBDIR).resolve(), root.resolve()):
        if resolved.is_relative_to(base):
            relative = resolved.relative_to(base).with_suffix("")
            return root / EXTRACTED_SUBDIR / AUDIO_SUBDIR / relative
    return root / EXTRACTED_SUBDIR / AUDIO_SUBDIR / source.stem


def safe_stem(name: str | None) -> str:
    """Extensionless name valid on Windows (no reserved characters nor trailing dot or space)."""
    if not name:
        return UNNAMED
    base = name.replace("\\", "/").rsplit("/", 1)[-1]
    stem = PurePosixPath(base).stem
    clean = "".join("_" if c in _BAD_CHARS or ord(c) < 0x20 else c for c in stem)
    return clean.rstrip(" .") or UNNAMED


# --- Cards -------------------------------------------------------------------------------


def _record(
    index: int,
    slot: int | None,
    name: str | None,
    fmt: _Format,
    *,
    status: Status = "ok",
    detail: str | None = None,
) -> TrackRecord:
    duration = fmt.samples / fmt.sample_rate if fmt.sample_rate else 0.0
    return TrackRecord(
        index=index,
        slot=slot,
        name=name,
        file=None,
        status=status,
        detail=detail,
        codec=fmt.codec,
        channels=fmt.channels,
        sample_rate=fmt.sample_rate,
        samples=fmt.samples,
        duration_s=round(duration, 3),
        loop=fmt.loop,
        peak=None,
        clipped=None,
        rms_dbfs=None,
    )


def _status_of(err: AudioFormatError) -> Status:
    if isinstance(err, EncryptedAdxError):
        return "encrypted"
    return "unsupported" if isinstance(err, UnsupportedCodecError) else "error"


def _write_wav(target: Path, record: TrackRecord, pcm: array[int]) -> None:
    stats = signal_stats(pcm, record["channels"], record["sample_rate"])
    atomic_write_chunks(target, iter_wav_chunks(pcm, record["channels"], record["sample_rate"]))
    record["file"] = target.name
    record["samples"] = stats.frames
    record["duration_s"] = round(stats.duration, 3)
    record["peak"] = stats.peak
    record["clipped"] = stats.clipped
    record["rms_dbfs"] = stats.rms_dbfs


def _summary(tracks: Sequence[TrackRecord]) -> SummaryRecord:
    counts = {status: sum(t["status"] == status for t in tracks) for status in STATUSES}
    return SummaryRecord(
        total=len(tracks),
        ok=counts["ok"],
        encrypted=counts["encrypted"],
        unsupported=counts["unsupported"],
        error=counts["error"],
    )


def _finish(source: Path, kind: Kind, out: Path, tracks: list[TrackRecord]) -> AudioManifest:
    manifest = AudioManifest(
        source=str(source),
        kind=kind,
        out_dir=str(out),
        summary=_summary(tracks),
        tracks=tracks,
    )
    text = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
    atomic_write_bytes(out / MANIFEST_NAME, text.encode("utf-8"), fsync=True)
    return manifest


def _emit(tracks: list[TrackRecord], record: TrackRecord, on_track: OnTrack | None) -> None:
    tracks.append(record)
    if on_track is not None:
        on_track(record)


# --- Selection in AFS ---------------------------------------------------------------------


def _open(afs_path: Path) -> AfsArchive:
    with afs_path.open("rb") as source:
        try:
            return open_afs(source, afs_path.name)
        except AfsError as err:
            raise AudioFormatError(f"{afs_path.name}: {err}") from err


def _select(
    archive: AfsArchive, indices: Sequence[int] | None, wanted: Callable[[AfsEntry], bool]
) -> list[AfsEntry]:
    """The requested entries (without repeats) or, without `indices`, those satisfying `wanted`."""
    if indices is None:
        return [e for e in archive.data_entries() if wanted(e)]
    unknown = [i for i in indices if not 0 <= i < archive.count]
    if unknown:
        raise AudioUsageError(f"indices out of range 0..{archive.count - 1}: {unknown}")
    chosen = [archive.entries[i] for i in dict.fromkeys(indices)]
    empty = [e.index for e in chosen if e.is_null or e.size == 0]
    if empty:
        raise AudioUsageError(f"empty entries: {empty}")
    return chosen


def _read_entry(afs_path: Path, entry: AfsEntry) -> bytes:
    with afs_path.open("rb") as source:
        try:
            return b"".join(iter_entry_chunks(source, entry))
        except AfsError as err:
            raise AudioFormatError(f"entry {entry.index}: {err}") from err


def _is_adx_entry(afs_path: Path, entry: AfsEntry) -> bool:
    with afs_path.open("rb") as source:
        prefix = read_prefix(source, entry, 4)
        try:
            size = adx.header_size(prefix)
        except AudioFormatError:
            return False
        return adx.looks_like_adx(read_prefix(source, entry, size))


def _is_wva_entry(entry: AfsEntry) -> bool:
    return (entry.name or "").lower().endswith(".wva")


# --- ADX ----------------------------------------------------------------------------------


def _adx_track(raw: bytes, entry: AfsEntry, target_dir: Path) -> TrackRecord:
    name = entry.name
    try:
        header = adx.parse_header(raw)
    except AudioFormatError as err:
        fmt = _Format("adx", 0, 0, 0)
        return _record(entry.index, None, name, fmt, status="error", detail=str(err))
    loop = header.loop
    fmt = _Format(
        codec=f"adx_v{header.version}",
        channels=header.channels,
        sample_rate=header.sample_rate,
        samples=header.total_samples,
        loop=LoopRecord(start_sample=loop.start_sample, end_sample=loop.end_sample)
        if loop is not None
        else None,
    )
    record = _record(entry.index, None, name, fmt)
    try:
        pcm = adx.decode_pcm(raw, header)
    except AudioFormatError as err:
        record["status"] = _status_of(err)
        record["detail"] = str(err)
        return record
    _write_wav(target_dir / f"{entry.index:04d}_{safe_stem(name)}.wav", record, pcm)
    return record


def extract_adx(
    afs_path: Path,
    out_dir: Path,
    indices: Sequence[int] | None = None,
    on_track: OnTrack | None = None,
) -> AudioManifest:
    """Decode the AFS ADX entries (all, or `indices`) to WAV and write the manifest."""
    out = ensure_outside_repos(out_dir)
    archive = _open(afs_path)
    entries = _select(archive, indices, lambda e: _is_adx_entry(afs_path, e))
    out.mkdir(parents=True, exist_ok=True)
    tracks: list[TrackRecord] = []
    for entry in entries:
        _emit(tracks, _adx_track(_read_entry(afs_path, entry), entry, out), on_track)
    return _finish(afs_path, "adx", out, tracks)


# --- WVA ----------------------------------------------------------------------------------


def _wva_tracks(raw: bytes, entry: AfsEntry, target_dir: Path) -> list[TrackRecord]:
    name = entry.name
    try:
        records = wva.parse_wva(raw)
    except AudioFormatError as err:
        fmt = _Format("wva", 0, 0, 0)
        return [_record(entry.index, None, name, fmt, status="error", detail=str(err))]
    tracks = []
    for rec in records:
        fmt = _Format("xbox_adpcm", rec.channels, rec.sample_rate, rec.samples)
        track = _record(entry.index, rec.slot, name, fmt)
        try:
            wva.check_xbox_adpcm(rec)
            pcm = xbox_adpcm.decode(wva.record_data(raw, rec), rec.channels)
        except AudioFormatError as err:
            track["status"] = _status_of(err)
            track["detail"] = str(err)
        else:
            stem = f"{entry.index:04d}_{safe_stem(name)}_{rec.slot:02d}"
            _write_wav(target_dir / f"{stem}.wav", track, pcm)
        tracks.append(track)
    return tracks


def extract_wva(
    afs_path: Path,
    out_dir: Path,
    indices: Sequence[int] | None = None,
    on_track: OnTrack | None = None,
) -> AudioManifest:
    """Decode each record of the AFS `.wva` files (all, or `indices`) to WAV."""
    out = ensure_outside_repos(out_dir)
    archive = _open(afs_path)
    entries = _select(archive, indices, _is_wva_entry)
    out.mkdir(parents=True, exist_ok=True)
    tracks: list[TrackRecord] = []
    for entry in entries:
        for track in _wva_tracks(_read_entry(afs_path, entry), entry, out):
            _emit(tracks, track, on_track)
    return _finish(afs_path, "wva", out, tracks)


# --- XWB ----------------------------------------------------------------------------------


def _xwb_samples(entry: xwb.XwbEntry, size: int) -> int:
    """Bytes -> samples per channel according to the entry's codec."""
    if entry.channels == 0:
        return 0
    if entry.codec == xwb.CODEC_XBOX_ADPCM:
        return xbox_adpcm.samples_for(size, entry.channels)
    if entry.codec == xwb.CODEC_PCM:
        return size // (entry.channels * entry.bits // 8)
    return 0


def _xwb_track(data: bytes, entry: xwb.XwbEntry, target_dir: Path) -> TrackRecord:
    loop = None
    if entry.loop_length:
        start = _xwb_samples(entry, entry.loop_start)
        loop = LoopRecord(
            start_sample=start,
            end_sample=start + _xwb_samples(entry, entry.loop_length),
        )
    samples = _xwb_samples(entry, entry.size)
    fmt = _Format(entry.codec_name, entry.channels, entry.sample_rate, samples, loop)
    record = _record(entry.index, None, entry.name, fmt)
    try:
        pcm = xwb.decode_entry(data, entry)
    except AudioFormatError as err:
        record["status"] = _status_of(err)
        record["detail"] = str(err)
        return record
    stem = f"{entry.index:04d}_{safe_stem(entry.name) if entry.name else 'wave'}"
    _write_wav(target_dir / f"{stem}.wav", record, pcm)
    return record


def extract_xwb(xwb_path: Path, out_dir: Path, on_track: OnTrack | None = None) -> AudioManifest:
    """Decode all the wave bank entries to WAV and write the manifest."""
    out = ensure_outside_repos(out_dir)
    data = xwb_path.read_bytes()
    bank = xwb.parse_xwb(data)
    out.mkdir(parents=True, exist_ok=True)
    tracks: list[TrackRecord] = []
    for entry in bank.entries:
        _emit(tracks, _xwb_track(data, entry, out), on_track)
    return _finish(xwb_path, "xwb", out, tracks)
