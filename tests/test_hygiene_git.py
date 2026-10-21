"""Tests de integración de la guardia contra repos git reales y desechables."""

from __future__ import annotations

import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from collections.abc import Callable
from pathlib import Path
from unittest import mock

from ngb_tools.hygiene import (
    GitError,
    GitRepo,
    Rule,
    ScanReport,
    main,
    read_command_prefix,
    run_command,
    scan,
)

XBE = b"XBEH" + bytes(60)
GIT = shutil.which("git")


def found(report: ScanReport) -> set[tuple[str, Rule]]:
    return {(v.path, v.rule) for v in report.violations}


class TempRepo:
    """Repo git desechable; la config del usuario queda fuera (ver `GitTestCase`)."""

    def __init__(self, root: Path) -> None:
        self.root = root
        root.mkdir()
        self.git("init", "-q")
        self.git("config", "user.name", "ngb-tests")
        self.git("config", "user.email", "ngb-tests@example.invalid")

    def git(self, *args: str, stdin: bytes | None = None) -> bytes:
        command = ["git", *args]
        return subprocess.run(
            command, cwd=self.root, input=stdin, check=True, capture_output=True
        ).stdout

    def write(self, path: str, content: bytes) -> None:
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)

    def stage(self, path: str, content: bytes) -> None:
        self.write(path, content)
        self.git("add", "--", path)

    def hash_blob(self, content: bytes) -> str:
        """Guarda un blob en la base de objetos y devuelve su oid."""
        return self.git("hash-object", "-w", "--stdin", stdin=content).decode("ascii").strip()

    def stage_entry(self, mode: str, path: str, content: bytes) -> None:
        """Mete un blob en el índice sin tocar el disco (symlinks, cambios de tipo)."""
        oid = self.hash_blob(content)
        self.git("update-index", "--add", "--cacheinfo", f"{mode},{oid},{path}")

    def commit(self) -> None:
        self.git("commit", "-q", "--no-verify", "-m", "fixture")

    def scan(self, *, staged: bool) -> ScanReport:
        return scan(GitRepo.discover(self.root), staged=staged)


@unittest.skipUnless(GIT, "git no está en PATH")
class GitTestCase(unittest.TestCase):
    """Cada test tiene su repo temporal y un git aislado de la config y el entorno del usuario."""

    def setUp(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tmp = Path(tmp.name)
        empty_config = self.tmp / "gitconfig"
        empty_config.write_bytes(b"")
        # Sin GIT_DIR, GIT_INDEX_FILE... heredados (p. ej. si los tests corren desde un hook).
        env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
        env |= {
            "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_CONFIG_GLOBAL": str(empty_config),
            "GIT_CEILING_DIRECTORIES": str(self.tmp),
        }
        patcher = mock.patch.dict(os.environ, env, clear=True)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.repo = TempRepo(self.tmp / "repo")

    def run_main(self, *args: str) -> tuple[int, str]:
        """Ejecuta la CLI en proceso; devuelve el código y lo escrito en stderr."""
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = main(list(args))
        return code, stderr.getvalue()


class StagedScanTest(GitTestCase):
    def test_staged_scan_judges_the_index_blob_not_the_worktree(self) -> None:
        self.repo.stage("notes.txt", XBE)
        self.repo.write("notes.txt", b"solo texto\n")
        self.assertEqual(found(self.repo.scan(staged=True)), {("notes.txt", Rule.SIGNATURE)})

    def test_type_change_from_symlink_to_file_is_checked(self) -> None:
        self.repo.stage_entry("120000", "notes.txt", b"target.txt")
        self.repo.commit()
        self.repo.stage_entry("100644", "notes.txt", XBE)
        self.assertEqual(found(self.repo.scan(staged=True)), {("notes.txt", Rule.SIGNATURE)})

    def test_staged_scan_skips_paths_already_committed(self) -> None:
        self.repo.stage("old.afs", b"x")
        self.repo.commit()
        self.repo.stage("new.txt", b"hola\n")
        report = self.repo.scan(staged=True)
        self.assertEqual((report.checked, report.violations), (1, ()))


class IndexScanTest(GitTestCase):
    def test_file_deleted_from_worktree_is_still_checked_from_index(self) -> None:
        self.repo.stage("notes.txt", XBE)
        self.repo.stage("README.md", b"# ok\n")
        self.repo.commit()
        (self.repo.root / "notes.txt").unlink()
        (self.repo.root / "README.md").unlink()
        report = self.repo.scan(staged=False)
        self.assertEqual(found(report), {("notes.txt", Rule.SIGNATURE)})
        self.assertEqual(report.checked, 2)

    def test_paths_with_spaces_and_non_ascii_round_trip(self) -> None:
        self.repo.stage("señal ñ.txt", XBE)
        self.repo.stage("dir con espacios/nota final.md", b"# hola\n")
        report = self.repo.scan(staged=False)
        self.assertEqual(found(report), {("señal ñ.txt", Rule.SIGNATURE)})
        self.assertEqual(report.checked, 2)

    def test_big_blob_is_read_through_a_cut_pipe(self) -> None:
        # Mucho mayor que el búfer de un tubo: git sigue escribiendo cuando se corta.
        image = bytes(0x10000) + b"MICROSOFT*XBOX*MEDIA" + bytes(4 * 1024 * 1024)
        self.repo.stage("disc.img", image)
        self.assertEqual(
            found(self.repo.scan(staged=False)),
            {("disc.img", Rule.SIZE), ("disc.img", Rule.SIGNATURE)},
        )

    def test_unreadable_blob_is_an_error_not_an_empty_pass(self) -> None:
        repo = GitRepo.discover(self.repo.root)
        with self.assertRaises(GitError):
            repo.read_blob_prefix("1" * 40, 4)

    def test_empty_file_passes(self) -> None:
        self.repo.stage("empty.txt", b"")
        report = self.repo.scan(staged=False)
        self.assertEqual((report.checked, report.violations), (1, ()))

    def test_symlinks_are_checked_as_blobs_with_their_target(self) -> None:
        self.repo.stage_entry("120000", "disc.iso", b"../ngb-data/game/disc.iso")
        self.repo.stage_entry("120000", "docs/dangling", b"../does/not/exist")
        report = self.repo.scan(staged=False)
        self.assertEqual(found(report), {("disc.iso", Rule.EXTENSION)})
        self.assertEqual(report.checked, 2)

    def test_gitlinks_are_skipped(self) -> None:
        self.repo.git("update-index", "--add", "--cacheinfo", f"160000,{'1' * 40},vendor/sub")
        self.repo.stage("README.md", b"# ok\n")
        report = self.repo.scan(staged=False)
        self.assertEqual((report.checked, report.violations), (1, ()))

    def test_replace_refs_do_not_hide_the_real_blob(self) -> None:
        self.repo.stage("notes.txt", XBE)
        game_oid = self.repo.git("rev-parse", ":notes.txt").decode("ascii").strip()
        self.repo.git("replace", game_oid, self.repo.hash_blob(b"texto\n"))
        # Para un git normal el reemplazo está activo...
        self.assertEqual(self.repo.git("cat-file", "blob", game_oid), b"texto\n")
        # ...pero la guardia sigue viendo el blob real.
        for staged in (True, False):
            with self.subTest(staged=staged):
                self.assertEqual(
                    found(self.repo.scan(staged=staged)), {("notes.txt", Rule.SIGNATURE)}
                )

    def test_force_added_file_under_an_ignored_dir_is_rejected(self) -> None:
        self.repo.stage(".gitignore", b"game/\n")
        self.repo.write("game/notas.txt", b"hola\n")
        self.repo.git("add", "-f", "--", "game/notas.txt")
        self.assertEqual(found(self.repo.scan(staged=True)), {("game/notas.txt", Rule.PATH)})


class AllowlistIntegrationTest(GitTestCase):
    def test_allowlist_skips_extension_but_never_signature(self) -> None:
        self.repo.stage(".hygiene-allow", b"# fixtures\ntools/plain.bin\ntools/xbe.bin\n")
        self.repo.stage("tools/plain.bin", b"\x01\x02")
        self.repo.stage("tools/xbe.bin", XBE)
        self.repo.stage("tools/other.bin", b"\x01\x02")
        self.assertEqual(
            found(self.repo.scan(staged=False)),
            {("tools/xbe.bin", Rule.SIGNATURE), ("tools/other.bin", Rule.EXTENSION)},
        )

    def test_allowlist_only_in_the_worktree_has_no_effect(self) -> None:
        self.repo.write(".hygiene-allow", b"tools/plain.bin\n")
        self.repo.stage("tools/plain.bin", b"\x01\x02")
        self.assertEqual(found(self.repo.scan(staged=False)), {("tools/plain.bin", Rule.EXTENSION)})

    def test_allowlist_with_bom_and_non_ascii_paths(self) -> None:
        self.repo.stage(".hygiene-allow", b"\xef\xbb\xbf" + "señal/ñ.bin\n".encode())
        self.repo.stage("señal/ñ.bin", b"\x01\x02")
        self.assertEqual(self.repo.scan(staged=False).violations, ())

    def test_allowlist_that_is_not_utf8_exits_2(self) -> None:
        # PowerShell 5 escribe UTF-16 con `>`: mejor un error claro que ignorarlo.
        self.repo.stage(".hygiene-allow", "tools/plain.bin\n".encode("utf-16"))
        code, stderr = self.run_main("--root", str(self.repo.root))
        self.assertEqual(code, 2)
        self.assertIn(".hygiene-allow", stderr)

    def test_committed_allowlist_applies_to_later_staged_scans(self) -> None:
        self.repo.stage(".hygiene-allow", b"tools/plain.bin\n")
        self.repo.commit()
        self.repo.stage("tools/plain.bin", b"\x01\x02")
        self.assertEqual(self.repo.scan(staged=True).violations, ())

    def test_staged_scan_without_head_uses_an_empty_allowlist(self) -> None:
        self.repo.stage(".hygiene-allow", b"tools/plain.bin\n")
        self.repo.stage("tools/plain.bin", b"\x01\x02")
        self.assertEqual(found(self.repo.scan(staged=True)), {("tools/plain.bin", Rule.EXTENSION)})

    def test_a_commit_cannot_exempt_itself(self) -> None:
        self.repo.stage("README.md", b"# ok\n")
        self.repo.commit()
        self.repo.stage(".hygiene-allow", b"tools/plain.bin\n")
        self.repo.stage("tools/plain.bin", b"\x01\x02")
        # El hook usa la lista de HEAD; la CI, la del árbol de la PR (visible en la revisión).
        self.assertEqual(found(self.repo.scan(staged=True)), {("tools/plain.bin", Rule.EXTENSION)})
        self.assertEqual(self.repo.scan(staged=False).violations, ())

    def test_allowlist_over_64_kib_exits_2(self) -> None:
        self.repo.stage(".hygiene-allow", b"#" * 65536 + b"\n")
        code, stderr = self.run_main("--root", str(self.repo.root))
        self.assertEqual(code, 2)
        self.assertIn(".hygiene-allow", stderr)

    def test_allowlist_of_exactly_64_kib_is_read(self) -> None:
        entry = b"tools/plain.bin\n"
        self.repo.stage(".hygiene-allow", entry + b"#" * (65536 - len(entry) - 1) + b"\n")
        self.repo.stage("tools/plain.bin", b"\x01\x02")
        self.assertEqual(self.repo.scan(staged=False).violations, ())


class ProcessTimeoutTest(unittest.TestCase):
    """Un git colgado (p. ej. esperando a la red) no puede bloquear el hook ni la CI."""

    SLOW = (sys.executable, "-c", "import time; time.sleep(30)")
    # Un nieto que hereda stdout/stderr, como el git real bajo el lanzador `cmd\git.exe` de
    # Windows: matar solo al hijo deja los tubos abiertos y la lectura colgada 30 s.
    SLOW_TREE = (
        sys.executable,
        "-c",
        "import subprocess, sys, time;"
        "subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'],"
        " stdout=1, stderr=2);"
        "time.sleep(30)",
    )

    def assert_times_out(self, call: Callable[[], object]) -> None:
        start = time.monotonic()
        with self.assertRaises(GitError):
            call()
        self.assertLess(time.monotonic() - start, 15)

    def test_slow_command_is_a_git_error(self) -> None:
        self.assert_times_out(lambda: run_command(self.SLOW, cwd=Path.cwd(), timeout=0.5))

    def test_slow_prefix_read_is_a_git_error(self) -> None:
        self.assert_times_out(
            lambda: read_command_prefix(self.SLOW, cwd=Path.cwd(), length=4, timeout=0.5)
        )

    def test_timeout_also_kills_descendants_holding_the_pipes(self) -> None:
        cwd = Path.cwd()
        with self.subTest(runner="run_command"):
            self.assert_times_out(lambda: run_command(self.SLOW_TREE, cwd=cwd, timeout=2))
        with self.subTest(runner="read_command_prefix"):
            self.assert_times_out(
                lambda: read_command_prefix(self.SLOW_TREE, cwd=cwd, length=4, timeout=2)
            )


class MainTest(GitTestCase):
    def test_exit_code_is_0_when_clean_and_1_on_violations(self) -> None:
        self.repo.stage("README.md", b"# ok\n")
        self.assertEqual(self.run_main("--root", str(self.repo.root))[0], 0)
        self.repo.stage("disc.iso", b"x")
        code, stderr = self.run_main("--root", str(self.repo.root))
        self.assertEqual(code, 1)
        self.assertIn("disc.iso", stderr)

    def test_staged_flag_limits_the_scan_to_prepared_files(self) -> None:
        self.repo.stage("old.afs", b"x")
        self.repo.commit()
        self.repo.stage("new.txt", b"hola\n")
        self.assertEqual(self.run_main("--staged", "--root", str(self.repo.root))[0], 0)
        self.assertEqual(self.run_main("--root", str(self.repo.root))[0], 1)

    def test_root_in_a_subdirectory_scans_the_whole_repo(self) -> None:
        self.repo.stage("disc.iso", b"x")
        self.repo.stage("sub/a.txt", b"a\n")
        code, stderr = self.run_main("--root", str(self.repo.root / "sub"))
        self.assertEqual(code, 1)
        self.assertIn("disc.iso", stderr)

    def test_outside_a_repo_exits_2_with_a_message(self) -> None:
        outside = self.tmp / "outside"
        outside.mkdir()
        code, stderr = self.run_main("--root", str(outside))
        self.assertEqual(code, 2)
        self.assertTrue(stderr.startswith("ngb-hygiene:"), stderr)

    def test_missing_root_exits_2_with_a_message(self) -> None:
        code, stderr = self.run_main("--root", str(self.tmp / "no-such-dir"))
        self.assertEqual(code, 2)
        self.assertTrue(stderr.startswith("ngb-hygiene:"), stderr)


if __name__ == "__main__":
    unittest.main()
