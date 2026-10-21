"""Tests de `ngb_tools._paths`: raíz de datos, guarda de repos y escritura atómica."""

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from collections.abc import Iterator
from pathlib import Path
from unittest import mock

from ngb_tools._paths import (
    DATA_ENV_VAR,
    PARTIAL_SUFFIX,
    UnsafeOutputError,
    atomic_write_bytes,
    atomic_write_chunks,
    data_root,
    ensure_outside_repos,
    find_repo_root,
    find_repo_roots,
    package_repo_root,
    report_dirs,
)

REPO = Path(__file__).resolve().parents[1]
IN_CHECKOUT = (REPO / ".git").exists()  # en un sdist no hay repo que proteger ni build/


def _can_symlink() -> bool:
    with tempfile.TemporaryDirectory() as tmp:
        try:
            (Path(tmp) / "link").symlink_to(Path(tmp) / "target")
        except OSError:
            return False
    return True


CAN_SYMLINK = _can_symlink()  # en Windows hace falta el modo desarrollador o privilegios


def link_dir(link: Path, target: Path) -> bool:
    """Enlace de directorio: unión (`mklink /J`, sin privilegios) en Windows, symlink fuera."""
    if os.name == "nt":  # sys.platform haría que mypy diera la otra rama por inalcanzable
        command = ["cmd", "/c", "mklink", "/J", str(link), str(target)]
        return subprocess.run(command, capture_output=True, check=False).returncode == 0
    try:
        link.symlink_to(target, target_is_directory=True)
    except OSError:
        return False
    return True


class TempDirTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name).resolve()

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def make_repo(self, name: str) -> Path:
        repo = self.root / name
        (repo / ".git").mkdir(parents=True)
        return repo

    def temp_dir_in_build(self) -> Path:
        """Carpeta temporal dentro de `build/` del repo real (ignorada y bloqueada por higiene)."""
        build = REPO / "build"
        build.mkdir(exist_ok=True)
        tmp = tempfile.TemporaryDirectory(dir=build)
        self.addCleanup(tmp.cleanup)
        return Path(tmp.name).resolve()


class DataRootTests(unittest.TestCase):
    def test_env_var_wins(self) -> None:
        self.assertEqual(data_root({DATA_ENV_VAR: "D:/datos"}), Path("D:/datos"))

    def test_defaults_to_home_when_unset_or_empty(self) -> None:
        for environ in ({}, {DATA_ENV_VAR: ""}):
            with self.subTest(environ=environ):
                self.assertEqual(data_root(environ), Path.home() / "ngb-data")


class RepoRootTests(TempDirTestCase):
    def test_found_from_a_git_dir_or_a_worktree_file(self) -> None:
        main = self.make_repo("main")
        worktree = self.root / "wt"
        (worktree / "src").mkdir(parents=True)
        (worktree / ".git").write_text("gitdir: ../main/.git/worktrees/wt\n", encoding="utf-8")
        self.assertEqual(find_repo_root(main / "a" / "b"), main)
        self.assertEqual(find_repo_root(worktree / "src"), worktree)
        self.assertIsNone(find_repo_root(Path(self.root.anchor)))

    def test_dot_git_file_without_gitdir_is_not_a_repo(self) -> None:
        outer = self.make_repo("outer")
        fake = outer / "fake"
        fake.mkdir()
        (fake / ".git").write_text("no soy un repo\n", encoding="utf-8")
        self.assertEqual(find_repo_root(fake / "x"), outer)
        (self.root / "lone").mkdir()
        (self.root / "lone" / ".git").write_bytes(b"")
        self.assertIsNone(find_repo_root(self.root / "lone" / "x"))

    def test_every_enclosing_repo_is_found_nearest_first(self) -> None:
        outer = self.make_repo("outer")
        (outer / "inner" / ".git").mkdir(parents=True)
        self.assertEqual(find_repo_roots(outer / "inner" / "x"), (outer / "inner", outer))

    @unittest.skipUnless(IN_CHECKOUT, "sin .git: el paquete no está en un clon")
    def test_package_repo_is_this_checkout(self) -> None:
        self.assertEqual(package_repo_root(), REPO)
        self.assertEqual(report_dirs(), (REPO / "build",))


class EnsureOutsideReposTests(TempDirTestCase):
    def test_path_inside_any_repo_is_refused(self) -> None:
        other = self.make_repo("other")
        for inside in (other, other / "out" / "x"):
            with self.subTest(path=inside), self.assertRaises(UnsafeOutputError):
                ensure_outside_repos(inside)
        self.assertFalse((other / "out").exists())

    def test_outer_repo_counts_even_if_the_nearest_dot_git_is_bogus(self) -> None:
        outer = self.make_repo("outer")
        (outer / "sub").mkdir()
        (outer / "sub" / ".git").write_text("basura\n", encoding="utf-8")
        with self.assertRaises(UnsafeOutputError):
            ensure_outside_repos(outer / "sub" / "x")

    @unittest.skipUnless(IN_CHECKOUT, "sin .git: el paquete no está en un clon")
    def test_path_inside_the_package_repo_is_refused(self) -> None:
        with self.assertRaises(UnsafeOutputError):
            ensure_outside_repos(REPO / "src" / "nope")

    def test_path_outside_every_repo_is_returned_resolved(self) -> None:
        outside = self.root / "data" / "extracted"
        self.assertEqual(ensure_outside_repos(outside), outside.resolve())

    def test_dotdot_in_a_missing_path_is_normalized(self) -> None:
        missing = self.root / "a" / "missing" / ".." / "b"
        self.assertEqual(ensure_outside_repos(missing), self.root / "a" / "b")

    @unittest.skipUnless(IN_CHECKOUT, "sin .git: el paquete no está en un clon")
    def test_build_of_the_package_repo_is_allowed(self) -> None:
        target = REPO / "build" / "_no_se_crea" / "inv.json"
        self.assertEqual(ensure_outside_repos(target, allow=report_dirs()), target.resolve())
        self.assertFalse(target.parent.exists())

    def test_allowance_only_applies_to_the_package_repo(self) -> None:
        build = self.make_repo("repo") / "build"
        with self.assertRaises(UnsafeOutputError):
            ensure_outside_repos(build / "inv.json", allow=[build])

    @unittest.skipUnless(IN_CHECKOUT, "sin .git: el paquete no está en un clon")
    def test_dotdot_cannot_climb_out_of_the_allowed_dir(self) -> None:
        escape = REPO / "build" / "missing" / ".." / ".." / "src" / "x"
        with self.assertRaises(UnsafeOutputError):
            ensure_outside_repos(escape, allow=report_dirs())

    @unittest.skipUnless(IN_CHECKOUT, "sin .git: el paquete no está en un clon")
    def test_repo_nested_inside_build_is_refused(self) -> None:
        clone = self.temp_dir_in_build() / "clone"
        (clone / ".git").mkdir(parents=True)
        with self.assertRaises(UnsafeOutputError):
            ensure_outside_repos(clone / "x", allow=report_dirs())

    @unittest.skipUnless(IN_CHECKOUT, "sin .git: el paquete no está en un clon")
    def test_allowed_dir_linked_into_another_repo_is_refused(self) -> None:
        target = self.make_repo("other") / "data"
        target.mkdir()
        fake_build = self.temp_dir_in_build() / "fake_build"
        if not link_dir(fake_build, target):
            self.skipTest("no se pudo crear el enlace de directorio")
        with self.assertRaises(UnsafeOutputError):
            ensure_outside_repos(fake_build / "inv.json", allow=[fake_build])

    def test_allowed_dir_that_is_a_link_inside_the_package_repo_is_refused(self) -> None:
        # build/ como unión hacia src/ del mismo repo: la ruta resuelta sigue en el repo del
        # paquete, pero fuera de la zona ignorada.
        package = self.make_repo("pkg")
        (package / "src").mkdir()
        build = package / "build"
        if not link_dir(build, package / "src"):
            self.skipTest("no se pudo crear el enlace de directorio")
        with (
            mock.patch("ngb_tools._paths.package_repo_root", return_value=package),
            self.assertRaises(UnsafeOutputError),
        ):
            ensure_outside_repos(build / "inv.json", allow=[build])

    def test_real_allowed_dir_inside_a_patched_package_repo_is_accepted(self) -> None:
        package = self.make_repo("pkg")
        build = package / "build"
        build.mkdir()
        with mock.patch("ngb_tools._paths.package_repo_root", return_value=package):
            self.assertEqual(
                ensure_outside_repos(build / "inv.json", allow=[build]), build / "inv.json"
            )


class AtomicWriteTests(TempDirTestCase):
    def test_writes_content_and_leaves_no_partial(self) -> None:
        target = self.root / "a.bin"
        atomic_write_bytes(target, b"hola")
        self.assertEqual(target.read_bytes(), b"hola")
        self.assertEqual([p.name for p in self.root.iterdir()], ["a.bin"])

    def test_overwrites_an_existing_target(self) -> None:
        target = self.root / "a.bin"
        target.write_bytes(b"viejo y algo largo")
        atomic_write_bytes(target, b"nuevo")
        self.assertEqual(target.read_bytes(), b"nuevo")

    def test_stale_partial_from_a_crash_is_replaced(self) -> None:
        target = self.root / "a.bin"
        target.with_name(target.name + PARTIAL_SUFFIX).write_bytes(b"basura de un corte")
        atomic_write_bytes(target, b"nuevo", fsync=True)
        self.assertEqual(target.read_bytes(), b"nuevo")
        self.assertEqual([p.name for p in self.root.iterdir()], ["a.bin"])

    @unittest.skipUnless(CAN_SYMLINK, "crear symlinks exige privilegios en este sistema")
    def test_partial_symlink_is_replaced_without_touching_its_target(self) -> None:
        victim = self.root / "victim.txt"
        victim.write_bytes(b"intacto")
        target = self.root / "a.bin"
        target.with_name(target.name + PARTIAL_SUFFIX).symlink_to(victim)
        atomic_write_bytes(target, b"nuevo")
        self.assertEqual(victim.read_bytes(), b"intacto")
        self.assertEqual(target.read_bytes(), b"nuevo")
        self.assertFalse(target.is_symlink())

    def test_failure_keeps_the_previous_file_and_removes_the_partial(self) -> None:
        target = self.root / "a.bin"
        target.write_bytes(b"anterior")

        def chunks() -> Iterator[bytes]:
            yield b"medio "
            raise OSError("disco lleno")

        with self.assertRaisesRegex(OSError, "disco lleno"):
            atomic_write_chunks(target, chunks())
        self.assertEqual(target.read_bytes(), b"anterior")
        self.assertEqual([p.name for p in self.root.iterdir()], ["a.bin"])

    def test_partial_created_by_another_writer_is_left_alone(self) -> None:
        target = self.root / "a.bin"
        partial = target.with_name(target.name + PARTIAL_SUFFIX)
        partial.write_bytes(b"del otro escritor")
        real_unlink = Path.unlink
        calls: list[Path] = []

        def unlink_except_first(path: Path, missing_ok: bool = False) -> None:
            # Simula la carrera: el otro escritor recrea el .part justo tras nuestro borrado.
            calls.append(path)
            if len(calls) > 1:
                real_unlink(path, missing_ok=missing_ok)

        with (
            mock.patch.object(Path, "unlink", unlink_except_first),
            self.assertRaises(FileExistsError),
        ):
            atomic_write_bytes(target, b"nuevo")
        self.assertEqual(partial.read_bytes(), b"del otro escritor")
        self.assertFalse(target.exists())


if __name__ == "__main__":
    unittest.main()
