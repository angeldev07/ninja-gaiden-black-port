"""CLI de audio: `python -P -s -m ngb_tools.audio {adx,wva,xwb,analyze}`.

- `adx <archivo.afs> [--index N] [--out DIR]`: ADX de CRI a WAV PCM16.
- `wva <archivo.afs> [--index N] [--out DIR]`: registros de los bancos `.wva` a WAV.
- `xwb <Wave.xwb> [--out DIR]`: entradas de un wave bank de XACT a WAV.
- `analyze <archivo.wav>... [--png-dir DIR]`: validación sin oír (estadísticas y PNG de
  forma de onda y espectrograma).

Por defecto los WAV van a `$NGB_DATA/extracted/audio/<origen>/` con `_manifest.json`, y
los PNG junto a cada WAV. Nada se escribe dentro de un repo git.

Códigos de salida:

- 0: todo correcto.
- 1: hallazgo sobre los datos: pistas cifradas, códecs no soportados o datos ilegibles
  (se informan y se saltan; el resto del lote se extrae igual).
- 2: error de uso o de E/S: argumentos, índices, salida dentro de un repo, archivos.
"""

from __future__ import annotations

import argparse
import io
import sys
import wave
from collections.abc import Sequence
from enum import IntEnum
from pathlib import Path
from types import MappingProxyType
from typing import Final

from ngb_tools._paths import DATA_ENV_VAR, UnsafeOutputError, atomic_write_bytes
from ngb_tools._paths import ensure_outside_repos as guard
from ngb_tools.audio.analysis import render_png, signal_stats
from ngb_tools.audio.errors import AudioFormatError, AudioUsageError
from ngb_tools.audio.extract import (
    AudioManifest,
    TrackRecord,
    default_audio_dir,
    extract_adx,
    extract_wva,
    extract_xwb,
)
from ngb_tools.audio.pcm import from_le_bytes

PROG: Final = "python -m ngb_tools.audio"


class ExitCode(IntEnum):
    OK = 0
    FINDINGS = 1
    ERROR = 2


def _print_track(track: TrackRecord) -> None:
    slot = f".{track['slot']:02d}" if track["slot"] is not None else "   "
    what = track["file"] if track["status"] == "ok" else track["detail"]
    print(
        f"{track['index']:4d}{slot}  {track['status']:<11}  {track['channels']}ch "
        f"{track['sample_rate']:>5} Hz  {track['duration_s']:>8.2f} s  {what}",
        flush=True,
    )


def _report(manifest: AudioManifest) -> ExitCode:
    s = manifest["summary"]
    print(
        f"{s['ok']}/{s['total']} ok, {s['encrypted']} cifradas, {s['unsupported']} no "
        f"soportadas, {s['error']} con error -> {manifest['out_dir']}"
    )
    return ExitCode.OK if s["ok"] == s["total"] else ExitCode.FINDINGS


def _out_dir(args: argparse.Namespace, source: Path) -> Path:
    out: Path | None = args.out
    return out if out is not None else default_audio_dir(source)


def cmd_adx(args: argparse.Namespace) -> ExitCode:
    source: Path = args.source
    manifest = extract_adx(source, _out_dir(args, source), args.index, _print_track)
    return _report(manifest)


def cmd_wva(args: argparse.Namespace) -> ExitCode:
    source: Path = args.source
    manifest = extract_wva(source, _out_dir(args, source), args.index, _print_track)
    return _report(manifest)


def cmd_xwb(args: argparse.Namespace) -> ExitCode:
    source: Path = args.source
    return _report(extract_xwb(source, _out_dir(args, source), _print_track))


def _read_wav(path: Path) -> tuple[int, int, bytes]:
    try:
        with wave.open(str(path), "rb") as reader:
            if reader.getsampwidth() != 2:
                raise AudioFormatError(f"{path.name}: solo se analiza PCM de 16 bits")
            return reader.getnchannels(), reader.getframerate(), reader.readframes(-1)
    except (wave.Error, EOFError) as err:
        raise AudioFormatError(f"{path.name}: WAV ilegible ({err})") from err


def _per_second(values: Sequence[float]) -> str:
    return " ".join(f"{v:.0f}" for v in values)


def cmd_analyze(args: argparse.Namespace) -> ExitCode:
    png_dir: Path | None = args.png_dir
    wavs: list[Path] = args.wav
    targets = [
        guard((png_dir if png_dir is not None else wav.parent) / f"{wav.stem}.png") for wav in wavs
    ]
    for wav, target in zip(wavs, targets, strict=True):
        channels, rate, frames = _read_wav(wav)
        pcm = from_le_bytes(frames)
        stats = signal_stats(pcm, channels, rate)
        target.parent.mkdir(parents=True, exist_ok=True)
        atomic_write_bytes(target, render_png(pcm, channels, rate))
        print(
            f"{wav.name}: {channels}ch {rate} Hz {stats.duration:.2f} s, pico {stats.peak}, "
            f"saturadas {stats.clipped}, RMS {stats.rms_dbfs:.1f} dBFS -> {target}"
        )
        print(f"  RMS/s (dBFS): {_per_second(stats.rms_per_second_dbfs)}")
    return ExitCode.OK


_COMMANDS: Final = MappingProxyType(
    {"adx": cmd_adx, "wva": cmd_wva, "xwb": cmd_xwb, "analyze": cmd_analyze}
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog=PROG, description="Audio del juego a WAV PCM16.")
    sub = parser.add_subparsers(dest="command", required=True)
    out_help = f"por defecto ${DATA_ENV_VAR}/extracted/audio/<origen dentro de game/>"
    for name, help_text, source in (
        ("adx", "ADX de CRI dentro de un AFS", "AFS"),
        ("wva", "bancos .wva (Xbox ADPCM) dentro de un AFS", "AFS"),
        ("xwb", "wave bank de XACT (WBND)", "XWB"),
    ):
        p = sub.add_parser(name, help=help_text)
        p.add_argument("source", type=Path, metavar=source)
        if source == "AFS":
            p.add_argument("--index", type=int, action="append", metavar="N", help="repetible")
        p.add_argument("--out", type=Path, metavar="DIR", help=out_help)
    p = sub.add_parser("analyze", help="estadísticas y PNG de forma de onda y espectrograma")
    p.add_argument("wav", type=Path, nargs="+")
    p.add_argument("--png-dir", type=Path, metavar="DIR", help="por defecto, junto a cada WAV")
    return parser


def tolerate_unencodable_output() -> None:
    """Una consola que no sabe representar un carácter lo sustituye en vez de abortar."""
    if isinstance(sys.stdout, io.TextIOWrapper):
        sys.stdout.reconfigure(errors="replace")


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    command = _COMMANDS[args.command]
    try:
        return command(args)
    except AudioFormatError as err:
        print(f"error: {err}", file=sys.stderr)
        return ExitCode.FINDINGS
    except (AudioUsageError, UnsafeOutputError, OSError) as err:
        print(f"error: {err}", file=sys.stderr)
        return ExitCode.ERROR


def run() -> int:
    """Entrada de consola (`python -m ngb_tools.audio`; el entry point lo añade el líder)."""
    tolerate_unencodable_output()
    return main()
