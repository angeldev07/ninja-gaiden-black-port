"""Tests del montaje de armas: tabla de anclajes del .itm, herencia del nodo y CLI.

Los esperados de la herencia están calculados a mano. Mano del humanoide sintético (obj5):
mundo (-0,7, 0,45, 0) = pecho (0, 0,2, 0) + hombro (-0,2, 0,25, 0) + 2 · (-0,25, 0, 0).
Arma (parent_and_child): obj0 con T (0, 1, 0) e hijo obj1 con T (2, 0, 0). Ancla con
traslación (0,1, 0, 0) y rotación Ry(90°), que lleva (x, y, z) a (z, y, -x).
"""

from __future__ import annotations

import contextlib
import io
import json
import math
import os
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest import mock

from ngb_tools.kmd.attach import (
    Attachment,
    ItemFormatError,
    find_anchor,
    parse_item_anchors,
)
from ngb_tools.kmd.cli import ExitCode, main
from ngb_tools.kmd.gltf import ExportOptions, build_glb, write_gltf
from ngb_tools.kmd.model import parse_kmd
from ngb_tools.kmd.motion import parse_motion_bank
from ngb_tools.kmd.rig import find_rig, motion_clip
from ngb_tools.kmd.textures import resolve_texture_files
from teamninja.synthetic import build_chunk

from .gltfcheck import load_glb, validate
from .synthetic import build_kmd, parent_and_child
from .synthetic_item import KATANA_LIKE, build_item
from .synthetic_motion import build_bank, humanoid, standing_motion

RY90 = (0.0, math.sqrt(0.5), 0.0, math.sqrt(0.5))
RZ90 = (0.0, 0.0, math.sqrt(0.5), math.sqrt(0.5))


def close(case: unittest.TestCase, got: Any, want: Any, places: int = 5) -> None:
    for g, w in zip(got, want, strict=True):
        case.assertAlmostEqual(g, w, places=places)


# --- evaluador TRS mínimo (independiente del exportador) ----------------------------------


def _quat_rotate(q: Any, v: Any) -> list[float]:
    x, y, z, w = q
    m = [
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ]
    return [sum(m[i][k] * v[k] for k in range(3)) for i in range(3)]


def _quat_mul(a: Any, b: Any) -> list[float]:
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    ]


def world_positions(gltf: dict[str, Any], overrides: dict[int, Any] | None = None) -> list[Any]:
    """Posición de mundo de cada nodo (traslación y rotación; las escalas son 1)."""
    nodes = gltf["nodes"]
    overrides = overrides or {}
    parent = {c: i for i, n in enumerate(nodes) for c in n.get("children", [])}
    cache: dict[int, tuple[list[float], list[float]]] = {}

    def world(i: int) -> tuple[list[float], list[float]]:
        if i not in cache:
            t = nodes[i].get("translation", [0.0, 0.0, 0.0])
            q = overrides.get(i, nodes[i].get("rotation", [0.0, 0.0, 0.0, 1.0]))
            if i in parent:
                pt, pq = world(parent[i])
                cache[i] = ([a + b for a, b in zip(pt, _quat_rotate(pq, t), strict=True)],
                            _quat_mul(pq, q))  # fmt: skip
            else:
                cache[i] = (list(t), list(q))
        return cache[i]

    return [world(i)[0] for i in range(len(nodes))]


def node_named(gltf: dict[str, Any], **extras: Any) -> int:
    return next(
        i
        for i, n in enumerate(gltf["nodes"])
        if all(n.get("extras", {}).get(k) == v for k, v in extras.items())
    )


# --- tests -------------------------------------------------------------------------------


class AnchorTableTests(unittest.TestCase):
    def test_records_state_variant_bone_and_values(self) -> None:
        anchors = parse_item_anchors(build_item(KATANA_LIKE))
        self.assertEqual([(a.state, a.variant, a.bone) for a in anchors],
                         [(2, 0, 11), (3, 0, 11), (1, 0, 0), (3, 0x3B, 1)])  # fmt: skip
        close(self, anchors[0].position, (-0.058, 0.0, -0.025))
        close(self, anchors[2].rotation_deg, (84.232, 180.288, 30.955), places=3)

    def test_held_state_by_default_and_variants_apart(self) -> None:
        anchors = parse_item_anchors(build_item(KATANA_LIKE))
        self.assertEqual(find_anchor(anchors).bone, 11)
        self.assertEqual(find_anchor(anchors, 3, 0x3B).bone, 1)
        with self.assertRaises(ItemFormatError):
            find_anchor(anchors, 7)

    def test_rotation_degrees_to_quaternion(self) -> None:
        # Ry(-13°) -> (0, sin(-6,5°), 0, cos(6,5°))
        anchor = find_anchor(parse_item_anchors(build_item(KATANA_LIKE)))
        half = math.radians(-13) / 2
        close(self, anchor.quaternion, (0.0, math.sin(half), 0.0, math.cos(half)))

    def test_table_without_terminator(self) -> None:
        with self.assertRaises(ItemFormatError):
            parse_item_anchors(build_item(KATANA_LIKE, terminated=False))

    def test_wrong_chunk_or_missing_slot(self) -> None:
        with self.assertRaises(ItemFormatError):
            parse_item_anchors(build_chunk(b"itm_dat2", [bytes(0x20)]))
        with self.assertRaises(ItemFormatError):
            parse_item_anchors(build_chunk(b"chr_dat2", [bytes(0x20), bytes(0x40)]))


class ExportCase(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.hero_path = self.root / "hero.kmd"
        self.hero_path.write_bytes(build_kmd(humanoid()))
        self.sword_path = self.root / "sword.kmd"
        self.sword_path.write_bytes(build_kmd(parent_and_child()))
        self.hero = parse_kmd(self.hero_path.read_bytes())
        self.sword = parse_kmd(self.sword_path.read_bytes())
        self.hero_files, _ = resolve_texture_files(self.hero, self.hero_path, self.root / "t1")
        self.sword_files, _ = resolve_texture_files(self.sword, self.sword_path, self.root / "t2")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def attachment(self) -> Attachment:
        return Attachment(
            name="sword",
            model=self.sword,
            texture_files=self.sword_files,
            parent=5,
            translation=(0.1, 0.0, 0.0),
            rotation=RY90,
            groups=None,
        )


class InheritanceTests(ExportCase):
    def export(self, **options: Any) -> tuple[dict[str, Any], bytes]:
        opts = ExportOptions(attachments=(self.attachment(),), **options)
        (path, bin_path), _ = write_gltf(
            self.hero, self.root / "out", "hero", self.hero_files, opts
        )
        gltf = json.loads(path.read_text(encoding="utf-8"))
        binary = bin_path.read_bytes()
        self.assertEqual(validate(gltf, binary, path.parent), [])
        return gltf, binary

    def test_anchor_is_a_child_of_the_hand_node(self) -> None:
        gltf, _ = self.export()
        hand = node_named(gltf, kmd_object=5)
        anchor = next(i for i, n in enumerate(gltf["nodes"]) if n["name"] == "sword_anchor")
        self.assertIn(anchor, gltf["nodes"][hand]["children"])
        close(self, gltf["nodes"][anchor]["translation"], (0.1, 0.0, 0.0))
        close(self, gltf["nodes"][anchor]["rotation"], RY90)
        weapon_root = node_named(gltf, kmd_object=0, attachment="sword")
        self.assertEqual(gltf["nodes"][anchor]["children"], [weapon_root])

    def test_weapon_nodes_inherit_the_hand_transform(self) -> None:
        gltf, _ = self.export()
        world = world_positions(gltf)
        close(self, world[node_named(gltf, kmd_object=5)], (-0.7, 0.45, 0.0))
        # ancla = mano + (0,1, 0, 0); obj0 = ancla + Ry(90°)·(0, 1, 0) = (-0,6, 1,45, 0)
        close(self, world[node_named(gltf, kmd_object=0, attachment="sword")], (-0.6, 1.45, 0.0))
        # obj1 = obj0 + Ry(90°)·(2, 0, 0) = (-0,6, 1,45, 0) + (0, 0, -2)
        close(self, world[node_named(gltf, kmd_object=1, attachment="sword")], (-0.6, 1.45, -2.0))

    def test_rotating_the_hand_carries_the_weapon(self) -> None:
        gltf, _ = self.export()
        hand = node_named(gltf, kmd_object=5)
        world = world_positions(gltf, {hand: RZ90})
        # Rz(90°) lleva (x, y, z) a (-y, x, z): ancla = (-0,7, 0,45 + 0,1, 0);
        # obj0 = ancla + Rz(90°)·Ry(90°)·(0, 1, 0) = ancla + (-1, 0, 0)
        close(self, world[node_named(gltf, kmd_object=0, attachment="sword")], (-1.7, 0.55, 0.0))

    def test_materials_and_images_of_each_model_stay_apart(self) -> None:
        gltf, _ = self.export()
        names = [m["name"] for m in gltf["materials"]]
        self.assertIn("tex00", names)
        self.assertIn("sword_tex00", names)
        self.assertEqual(len(gltf["images"]), 2)

    def test_animation_targets_only_the_character_and_the_weapon_follows(self) -> None:
        bank = parse_motion_bank(build_bank([standing_motion(frames=1)], [0]))
        clip = motion_clip(find_rig(self.hero), bank.motion(0), "hero#0")
        gltf, _ = self.export(animations=(clip,))
        targets = {c["target"]["node"] for c in gltf["animations"][0]["channels"]}
        weapon_nodes = {
            i for i, n in enumerate(gltf["nodes"]) if "attachment" in n.get("extras", {})
        }
        self.assertFalse(targets & weapon_nodes)
        self.assertIn(node_named(gltf, kmd_object=5), targets)

    def test_missing_parent_is_a_warning(self) -> None:
        bad = Attachment("sword", self.sword, self.sword_files, parent=99)
        opts = ExportOptions(attachments=(bad,))
        _, stats = write_gltf(self.hero, self.root / "out", "hero", self.hero_files, opts)
        self.assertTrue(any("obj99" in w for w in stats.warnings))

    def test_glb_embeds_the_images_of_both_models(self) -> None:
        blob, _ = build_glb(
            self.hero, self.hero_files, ExportOptions(attachments=(self.attachment(),))
        )
        gltf, binary = load_glb(blob)
        self.assertEqual(validate(gltf, binary), [])
        self.assertEqual([i["name"] for i in gltf["images"]], ["tex00", "sword_tex00"])


class CliTests(ExportCase):
    def run_cli(self, *argv: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with (
            mock.patch.dict(os.environ, {"NGB_DATA": str(self.root / "data")}),
            contextlib.redirect_stdout(out),
            contextlib.redirect_stderr(err),
        ):
            code = main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def gltf(self, name: str = "hero") -> dict[str, Any]:
        text = (self.root / "o" / f"{name}.gltf").read_text(encoding="utf-8")
        return dict(json.loads(text))

    def test_attach_with_explicit_bone_and_offset(self) -> None:
        spec = f"{self.sword_path}@5:0.1,0,0,0,90,0"
        code, out, _ = self.run_cli("export", str(self.hero_path), "--out", str(self.root / "o"),
                                    "--attach", spec)  # fmt: skip
        self.assertEqual(code, ExitCode.OK, out)
        self.assertIn("montado: sword en obj5", out)
        anchor = next(n for n in self.gltf()["nodes"] if n["name"] == "sword_anchor")
        close(self, anchor["translation"], (0.1, 0.0, 0.0))
        close(self, anchor["rotation"], RY90)

    def test_attach_from_item_table_and_custom_name(self) -> None:
        item = self.root / "sword.itm"
        item.write_bytes(build_item(KATANA_LIKE))
        code, out, _ = self.run_cli(
            "export", str(self.hero_path), "--out", str(self.root / "o"), "--name", "hero_sword",
            "--attach", f"{self.sword_path}@{item}",
        )  # fmt: skip
        self.assertEqual(code, ExitCode.OK, out)
        self.assertIn("montado: sword en obj11", out)
        anchor = next(n for n in self.gltf("hero_sword")["nodes"] if n["name"] == "sword_anchor")
        close(self, anchor["translation"], (-0.058, 0.0, -0.025))

    def test_item_state_that_does_not_exist_is_a_finding(self) -> None:
        item = self.root / "sword.itm"
        item.write_bytes(build_item(KATANA_LIKE))
        code, _, _ = self.run_cli("export", str(self.hero_path), "--out", str(self.root / "o"),
                                  "--attach", f"{self.sword_path}@{item}#9")  # fmt: skip
        self.assertEqual(code, ExitCode.FINDINGS)

    def test_usage_errors(self) -> None:
        base = ("export", str(self.hero_path), "--out", str(self.root / "o"))
        for extra in (
            ("--attach", str(self.sword_path)),  # sin @
            ("--attach", f"{self.sword_path}@99"),  # hueso fuera del modelo
            ("--attach", f"{self.sword_path}@5:1,2"),  # desplazamiento de 2 números
            ("--attach", f"{self.sword_path}@5", "--format", "obj"),
            ("--name", "../fuera"),
        ):
            with self.subTest(extra=extra):
                code, _, _ = self.run_cli(*base, *extra)
                self.assertEqual(code, ExitCode.ERROR)

    def test_item_without_terminator_is_a_finding(self) -> None:
        item = self.root / "broken.itm"
        item.write_bytes(build_item(KATANA_LIKE, terminated=False))
        code, _, _ = self.run_cli("export", str(self.hero_path), "--out", str(self.root / "o"),
                                  "--attach", f"{self.sword_path}@{item}")  # fmt: skip
        self.assertEqual(code, ExitCode.FINDINGS)


if __name__ == "__main__":
    unittest.main()
