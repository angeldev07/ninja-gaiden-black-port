"""Tests of the humanoid rig: deduced skeleton, FK, IK with pole and clips (hand-computed)."""

from __future__ import annotations

import math
import unittest

from ngb_tools.kmd.model import parse_kmd
from ngb_tools.kmd.motion import parse_motion_bank
from ngb_tools.kmd.rig import (
    RigError,
    find_rig,
    local_rotations,
    mat_vec,
    motion_clip,
    pose,
    quaternion,
    rot_y,
)

from .synthetic import build_kmd, parent_and_child
from .synthetic_motion import build_bank, humanoid, standing_motion, standing_values


def close(case: unittest.TestCase, got: tuple[float, ...], want: tuple[float, ...]) -> None:
    for g, w in zip(got, want, strict=True):
        case.assertAlmostEqual(g, w, places=3)


def distance(a: tuple[float, ...], b: tuple[float, ...]) -> float:
    return math.dist(a, b)


class FindRigTests(unittest.TestCase):
    def test_bones_and_sides_of_the_humanoid(self) -> None:
        rig = find_rig(parse_kmd(build_kmd(humanoid())))
        self.assertEqual((rig.root, rig.chest, rig.head), (0, 1, 2))
        chains = [(limb.upper, limb.lower, limb.end, limb.side) for limb in rig.limbs]
        self.assertEqual(chains, [(3, 4, 5, -1), (6, 7, 8, 1), (9, 10, 11, -1), (12, 13, 14, 1)])
        self.assertEqual(rig.parents[3], 1)
        self.assertEqual(rig.parents[9], 0)
        self.assertEqual(len(rig.bones), 15)

    def test_a_model_without_limbs_is_rejected(self) -> None:
        with self.assertRaises(RigError):
            find_rig(parse_kmd(build_kmd(parent_and_child())))


class PoseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.rig = find_rig(parse_kmd(build_kmd(humanoid())))
        self.world = pose(self.rig, standing_values())

    def test_root_translation(self) -> None:
        close(self, self.world[0][1], (0.0, 0.9, 0.0))

    def test_leg_ik_reaches_the_target_with_the_knee_forward(self) -> None:
        # hip (-0.1, 0.85, 0); knee = hip + (0, -0.24, 0.32); ankle = target
        close(self, self.world[9][1], (-0.1, 0.85, 0.0))
        close(self, self.world[10][1], (-0.1, 0.61, 0.32))
        close(self, self.world[11][1], (-0.1, 0.37, 0.0))
        close(self, self.world[13][1], (0.1, 0.61, 0.32))  # the +x side with H = -90°

    def test_bone_lengths_are_kept(self) -> None:
        for upper, lower, end in ((9, 10, 11), (12, 13, 14)):
            self.assertAlmostEqual(distance(self.world[upper][1], self.world[lower][1]), 0.4, 6)
            self.assertAlmostEqual(distance(self.world[lower][1], self.world[end][1]), 0.4, 6)

    def test_upper_bone_rotation_points_along_the_thigh(self) -> None:
        # bind direction (0, -1, 0) -> (knee - hip) / 0.4 = (0, -0.6, 0.8)
        close(self, mat_vec(self.world[9][0], (0.0, -1.0, 0.0)), (0.0, -0.6, 0.8))

    def test_straight_arms_reach_the_t_pose_wrist(self) -> None:
        # shoulder (-0.2, 1.35, 0); the target is exactly 0.5 = 0.25 + 0.25 away
        close(self, self.world[4][1], (-0.45, 1.35, 0.0))
        close(self, self.world[5][1], (-0.7, 1.35, 0.0))

    def test_chest_rotation_is_local_euler(self) -> None:
        values = standing_values()
        values[6] = 15708  # chest: X = 90°
        world = pose(self.rig, values)
        local = local_rotations(self.rig, world)
        close(self, local[1], (math.sqrt(0.5), 0.0, 0.0, math.sqrt(0.5)))
        # head: chest (0, 1.1, 0) + Rx(90°)·(0, 0.3, 0) = (0, 1.1, 0.3)
        close(self, world[2][1], (0.0, 1.1, 0.3))

    def test_effector_euler_is_absolute(self) -> None:
        values = standing_values()
        values[40] = 31416  # +x foot: Y = 180°
        rotation = pose(self.rig, values)[14][0]
        for row, want in zip(
            rotation, ((-1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, -1.0)), strict=True
        ):
            close(self, row, want)

    def test_wrong_track_count_is_rejected(self) -> None:
        with self.assertRaises(RigError):
            pose(self.rig, [0.0] * 43)


class QuaternionTests(unittest.TestCase):
    def test_half_turn_about_y(self) -> None:
        close(self, quaternion(rot_y(math.pi)), (0.0, 1.0, 0.0, 0.0))

    def test_quarter_turn_about_y(self) -> None:
        close(self, quaternion(rot_y(math.pi / 2)), (0.0, math.sqrt(0.5), 0.0, math.sqrt(0.5)))


class ClipTests(unittest.TestCase):
    def test_clip_samples_every_frame(self) -> None:
        rig = find_rig(parse_kmd(build_kmd(humanoid())))
        bank = parse_motion_bank(build_bank([standing_motion(frames=2)], [0]))
        clip = motion_clip(rig, bank.motion(0), "m#0")
        self.assertEqual(clip.times, (0.0, 1 / 60, 2 / 60))
        self.assertEqual(len(clip.channels), 16)  # root translation + 15 rotations
        root = clip.channels[0]
        self.assertEqual((root.obj, root.path), (0, "translation"))
        for value in root.values:
            close(self, value, (0.0, 0.9, 0.0))
        for channel in clip.channels[1:]:
            self.assertEqual(channel.path, "rotation")
            for q in channel.values:
                self.assertAlmostEqual(sum(c * c for c in q), 1.0, places=6)

    def test_motion_with_other_track_count_is_rejected(self) -> None:
        rig = find_rig(parse_kmd(build_kmd(humanoid())))
        bank = parse_motion_bank(build_bank([standing_motion()[:40]], [0]))
        with self.assertRaises(RigError):
            motion_clip(rig, bank.motion(0), "short")


if __name__ == "__main__":
    unittest.main()
