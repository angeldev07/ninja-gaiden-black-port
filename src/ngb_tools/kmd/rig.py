"""Rig humanoide de Team Ninja: de las 44 pistas de una motion a la pose de los huesos.

Las motions de `ryu.mtn` no guardan una rotación por hueso. Mezclan FK para el tronco con
IK para los miembros. Disposición de las pistas, en unidades de 1e-4 m y 1e-4 rad (INFERRED
fuerte: dentro de una motion hay saltos exactos de 62831 = 2π·1e4, y en la pose estática la
cadera queda a 0,99 m, su altura real):

    0-2    posición de la raíz (cadera) en el espacio del modelo, con el suelo en y = 0
    3-5    rotación de la cadera, Euler XYZ (R = Rz·Ry·Rx, INFERRED)
    6-8    rotación del pecho, local a la cadera
    9-11   rotación de la cabeza, local al pecho
    12-19  miembro A, brazo del lado -x
    20-27  miembro B, brazo +x
    28-35  miembro C, pierna -x
    36-43  miembro D, pierna +x

Cada miembro: objetivo IK de la muñeca o el tobillo en el espacio del modelo (xyz),
orientación absoluta de la mano o el pie (Euler) y dos ángulos G y H. Con H se orienta el
polo: polo = R_padre · Ry(H) · (lado, 0, 0), así que las rodillas miran hacia delante y los
codos hacia atrás. G no se usa: su semántica es desconocida. Evidencia de esta disposición:
los objetivos de los cuatro miembros tienen los mismos rangos que la raíz, porque viajan con
ella; en la pose estática los pies quedan a 0,11 m, la altura del tobillo de bind; y los
renders de idle, andar, correr, saltar y los tajos de katana salen coherentes.

**IK.** Dos huesos con las longitudes de bind, resuelto en el plano que forman el hombro
o la cadera, el objetivo y el polo, con la ley de los cosenos. Si el objetivo no está a su
alcance, la distancia se recorta.

**Esqueleto.** Se deduce del KMD (objetos del grupo de detalle):

- raíz: la de HIERARC;
- pecho: el hijo de la raíz más alto;
- cabeza: el hijo del pecho centrado en x;
- miembros: cadenas de 3 huesos (superior, inferior, extremo) bajo el pecho (brazos) y
  bajo la raíz (piernas), con el lado según el signo de x del superior.

En la pose de bind los huesos no tienen rotación: la del modelo es la pose T.
"""

from __future__ import annotations

import math
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from types import MappingProxyType
from typing import Final

from ngb_tools.kmd.animation import ROTATION, TRANSLATION, AnimationClip, NodeChannel
from ngb_tools.kmd.errors import KmdError
from ngb_tools.kmd.model import KmdModel
from ngb_tools.kmd.motion import Motion
from ngb_tools.kmd.objinfo import ObjectGroup, Vec3

TRACK_COUNT: Final = 44
UNIT: Final = 1e-4
MOTION_FPS: Final = 60.0  # INFERRED: el juego va a 60 fps
ROOT_POSITION: Final = 0
ROOT_ROTATION: Final = 3
CHEST_ROTATION: Final = 6
HEAD_ROTATION: Final = 9
LIMB_STARTS: Final = (12, 20, 28, 36)  # brazo -x, brazo +x, pierna -x, pierna +x
LIMB_TARGET: Final = 0
LIMB_EFFECTOR: Final = 3
LIMB_SWIVEL: Final = 7
CENTER_TOLERANCE: Final = 0.02
_EPS: Final = 1e-6

type Mat3 = tuple[Vec3, Vec3, Vec3]
type Quat = tuple[float, float, float, float]
type Pose = dict[int, tuple[Mat3, Vec3]]


class RigError(KmdError):
    """El KMD no tiene el esqueleto humanoide que esperan las motions, o la motion no cuadra."""


@dataclass(frozen=True, slots=True)
class Limb:
    """Cadena de 3 huesos con su lado (-1 o +1) y la dirección de flexión en bind."""

    upper: int
    lower: int
    end: int
    side: int
    parent: int
    bend: Vec3


@dataclass(frozen=True, slots=True)
class HumanoidRig:
    root: int
    chest: int
    head: int
    limbs: tuple[Limb, ...]
    bind: Mapping[int, Vec3]
    parents: Mapping[int, int]

    @property
    def bones(self) -> tuple[int, ...]:
        chains = [b for limb in self.limbs for b in (limb.upper, limb.lower, limb.end)]
        return (self.root, self.chest, self.head, *chains)


# --- álgebra 3x3 con tuplas ---------------------------------------------------------------

IDENTITY: Final[Mat3] = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))


def _add(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _scale(a: Vec3, s: float) -> Vec3:
    return (a[0] * s, a[1] * s, a[2] * s)


def _dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a: Vec3, b: Vec3) -> Vec3:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _length(a: Vec3) -> float:
    return math.sqrt(_dot(a, a))


def _unit(a: Vec3, fallback: Vec3) -> Vec3:
    n = _length(a)
    return fallback if n < _EPS else _scale(a, 1.0 / n)


def mat_mul(a: Mat3, b: Mat3) -> Mat3:
    rows = [tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3)]
    return ((rows[0][0], rows[0][1], rows[0][2]), (rows[1][0], rows[1][1], rows[1][2]),
            (rows[2][0], rows[2][1], rows[2][2]))  # fmt: skip


def mat_vec(a: Mat3, v: Vec3) -> Vec3:
    return (_dot(a[0], v), _dot(a[1], v), _dot(a[2], v))


def transpose(a: Mat3) -> Mat3:
    return ((a[0][0], a[1][0], a[2][0]), (a[0][1], a[1][1], a[2][1]), (a[0][2], a[1][2], a[2][2]))


def rot_x(t: float) -> Mat3:
    c, s = math.cos(t), math.sin(t)
    return ((1.0, 0.0, 0.0), (0.0, c, -s), (0.0, s, c))


def rot_y(t: float) -> Mat3:
    c, s = math.cos(t), math.sin(t)
    return ((c, 0.0, s), (0.0, 1.0, 0.0), (-s, 0.0, c))


def rot_z(t: float) -> Mat3:
    c, s = math.cos(t), math.sin(t)
    return ((c, -s, 0.0), (s, c, 0.0), (0.0, 0.0, 1.0))


def euler_xyz(x: float, y: float, z: float) -> Mat3:
    """R = Rz·Ry·Rx: primero X, luego Y y luego Z (orden INFERRED)."""
    return mat_mul(rot_z(z), mat_mul(rot_y(y), rot_x(x)))


def _frame(direction: Vec3, normal: Vec3) -> Mat3:
    """Base ortonormal con columnas (dirección, normal ortogonalizada, dirección × normal)."""
    d = _unit(direction, (1.0, 0.0, 0.0))
    n = _unit(_sub(normal, _scale(d, _dot(normal, d))), _any_perpendicular(d))
    w = _cross(d, n)
    return ((d[0], n[0], w[0]), (d[1], n[1], w[1]), (d[2], n[2], w[2]))


def _any_perpendicular(d: Vec3) -> Vec3:
    axis = (0.0, 0.0, 1.0) if abs(d[2]) < 0.9 else (1.0, 0.0, 0.0)
    return _unit(_cross(d, axis), (0.0, 1.0, 0.0))


def quaternion(m: Mat3) -> Quat:
    """Cuaternión glTF (x, y, z, w) de una matriz de rotación."""
    trace = m[0][0] + m[1][1] + m[2][2]
    if trace > 0:
        s = 2.0 * math.sqrt(trace + 1.0)
        q = ((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, s / 4)
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = 2.0 * math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2])
        q = (s / 4, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s)
    elif m[1][1] > m[2][2]:
        s = 2.0 * math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2])
        q = ((m[0][1] + m[1][0]) / s, s / 4, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s)
    else:
        s = 2.0 * math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1])
        q = ((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, s / 4, (m[1][0] - m[0][1]) / s)
    n = math.sqrt(sum(c * c for c in q))
    return (q[0] / n, q[1] / n, q[2] / n, q[3] / n)


# --- esqueleto ------------------------------------------------------------------------------


def _detail_children(model: KmdModel, obj: int) -> list[int]:
    return [c for c in model.children(obj) if model.objects[c].group == ObjectGroup.DETAIL]


def _chain(model: KmdModel, upper: int) -> tuple[int, int, int] | None:
    """(superior, inferior, extremo): el inferior es el hijo de detalle con hijos de detalle."""
    lowers = [c for c in _detail_children(model, upper) if _detail_children(model, c)]
    if not lowers:
        return None
    lower = max(lowers, key=lambda c: _length(model.objects[c].info.translation))
    ends = _detail_children(model, lower)
    end = max(ends, key=lambda c: _length(model.objects[c].info.translation))
    return (upper, lower, end)


def _limbs_under(model: KmdModel, parent: int, candidates: Sequence[int], kind: str) -> list[Limb]:
    """Un miembro por lado (-x y +x) entre `candidates`, en ese orden."""
    by_side: dict[int, Limb] = {}
    # Flexión en bind: codos hacia atrás (-z), rodillas hacia delante (+z).
    bend = (0.0, 0.0, -1.0) if kind == "brazo" else (0.0, 0.0, 1.0)
    for upper in candidates:
        x = model.objects[upper].info.translation[0]
        chain = _chain(model, upper)
        if chain is None or abs(x) < CENTER_TOLERANCE:
            continue
        side = 1 if x > 0 else -1
        if side in by_side:
            raise RigError(f"dos {kind} del lado {side:+d} bajo obj{parent}")
        by_side[side] = Limb(*chain, side=side, parent=parent, bend=bend)
    if set(by_side) != {-1, 1}:
        raise RigError(f"no hay un {kind} a cada lado bajo obj{parent}")
    return [by_side[-1], by_side[1]]


def find_rig(model: KmdModel) -> HumanoidRig:
    """Deduce el rig humanoide del KMD, o `RigError` si no lo tiene."""
    if model.hierarchy is None:
        raise RigError("el KMD no tiene HIERARC")
    root = model.hierarchy.root
    below_root = _detail_children(model, root)
    upward = [c for c in below_root if model.objects[c].info.translation[1] > 0]
    if not upward:
        raise RigError(f"obj{root} no tiene un pecho por encima")
    chest = max(upward, key=lambda c: model.objects[c].info.translation[1])
    below_chest = _detail_children(model, chest)
    centred = [
        c for c in below_chest if abs(model.objects[c].info.translation[0]) < CENTER_TOLERANCE
    ]
    if not centred:
        raise RigError(f"obj{chest} no tiene cabeza")
    head = max(centred, key=lambda c: model.objects[c].info.translation[1])
    arms = _limbs_under(model, chest, below_chest, "brazo")
    legs = [c for c in below_root if c != chest]
    limbs = (*arms, *_limbs_under(model, root, legs, "pierna"))
    bones = (root, chest, head, *(b for limb in limbs for b in (limb.upper, limb.lower, limb.end)))
    bind = {b: model.objects[b].info.translation for b in bones}
    parents = {b: p for b in bones if (p := model.parent(b)) is not None}
    return HumanoidRig(root, chest, head, limbs, MappingProxyType(bind), MappingProxyType(parents))


# --- pose -----------------------------------------------------------------------------------


def _vec(values: Sequence[float], start: int) -> Vec3:
    return (values[start] * UNIT, values[start + 1] * UNIT, values[start + 2] * UNIT)


def _solve_limb(
    rig: HumanoidRig, limb: Limb, values: Sequence[float], start: int, out: Pose
) -> None:
    parent_r, parent_p = out[limb.parent]
    bind = rig.bind
    shoulder = _add(parent_p, mat_vec(parent_r, bind[limb.upper]))
    target = _vec(values, start + LIMB_TARGET)
    l1, l2 = _length(bind[limb.lower]), _length(bind[limb.end])
    b1 = _unit(bind[limb.lower], (1.0, 0.0, 0.0))
    b2 = _unit(bind[limb.end], b1)
    to_target = _sub(target, shoulder)
    direction = _unit(to_target, mat_vec(parent_r, b1))
    reach = min(max(_length(to_target), abs(l1 - l2) + _EPS), l1 + l2 - _EPS)
    swivel = values[start + LIMB_SWIVEL] * UNIT
    pole = mat_vec(parent_r, mat_vec(rot_y(swivel), (float(limb.side), 0.0, 0.0)))
    bend_dir = _unit(
        _sub(pole, _scale(direction, _dot(pole, direction))), mat_vec(parent_r, limb.bend)
    )
    cos_a = max(-1.0, min(1.0, (l1 * l1 + reach * reach - l2 * l2) / (2 * l1 * reach)))
    sin_a = math.sqrt(1.0 - cos_a * cos_a)
    elbow = _add(shoulder, _add(_scale(direction, l1 * cos_a), _scale(bend_dir, l1 * sin_a)))
    wrist = _add(shoulder, _scale(direction, reach))
    normal = _cross(_unit(_sub(elbow, shoulder), direction), bend_dir)
    bind_normal = _cross(b1, limb.bend)
    upper_r = mat_mul(_frame(_sub(elbow, shoulder), normal), transpose(_frame(b1, bind_normal)))
    lower_r = mat_mul(_frame(_sub(wrist, elbow), normal), transpose(_frame(b2, bind_normal)))
    effector = _vec(values, start + LIMB_EFFECTOR)
    out[limb.upper] = (upper_r, shoulder)
    out[limb.lower] = (lower_r, elbow)
    out[limb.end] = (euler_xyz(*effector), wrist)


def pose(rig: HumanoidRig, values: Sequence[float]) -> Pose:
    """Rotación y posición de cada hueso en el espacio del modelo (valores brutos de pista)."""
    if len(values) != TRACK_COUNT:
        raise RigError(f"la motion tiene {len(values)} pistas; el rig humanoide necesita 44")
    out: Pose = {}
    root_r = euler_xyz(*_vec(values, ROOT_ROTATION))
    out[rig.root] = (root_r, _vec(values, ROOT_POSITION))
    chest_r = mat_mul(root_r, euler_xyz(*_vec(values, CHEST_ROTATION)))
    out[rig.chest] = (chest_r, _add(out[rig.root][1], mat_vec(root_r, rig.bind[rig.chest])))
    head_r = mat_mul(chest_r, euler_xyz(*_vec(values, HEAD_ROTATION)))
    out[rig.head] = (head_r, _add(out[rig.chest][1], mat_vec(chest_r, rig.bind[rig.head])))
    for limb, start in zip(rig.limbs, LIMB_STARTS, strict=True):
        _solve_limb(rig, limb, values, start, out)
    return out


def local_rotations(rig: HumanoidRig, world: Pose) -> dict[int, Quat]:
    """Rotación de cada hueso relativa a su padre (la raíz, en el espacio del modelo)."""
    local: dict[int, Quat] = {}
    for bone in rig.bones:
        rotation = world[bone][0]
        parent = rig.parents.get(bone)
        if parent is not None and parent in world:
            rotation = mat_mul(transpose(world[parent][0]), rotation)
        local[bone] = quaternion(rotation)
    return local


def _continuous(previous: Quat | None, q: Quat) -> Quat:
    """Mismo hemisferio que la muestra anterior, para que LINEAR no dé la vuelta larga."""
    if previous is not None and sum(a * b for a, b in zip(previous, q, strict=True)) < 0:
        return (-q[0], -q[1], -q[2], -q[3])
    return q


def motion_clip(
    rig: HumanoidRig, motion: Motion, name: str, *, fps: float = MOTION_FPS
) -> AnimationClip:
    """Muestrea la motion en cada frame: traslación de la raíz y rotación de cada hueso."""
    if len(motion.tracks) != TRACK_COUNT:
        raise RigError(f"{name}: {len(motion.tracks)} pistas; el rig humanoide necesita 44")
    frames = range(motion.frames + 1)
    rotations: dict[int, list[Quat]] = {bone: [] for bone in rig.bones}
    translations: list[tuple[float, ...]] = []
    for frame in frames:
        world = pose(rig, motion.sample(frame))
        translations.append(world[rig.root][1])
        for bone, q in local_rotations(rig, world).items():
            series = rotations[bone]
            series.append(_continuous(series[-1] if series else None, q))
    channels = [NodeChannel(rig.root, TRANSLATION, tuple(translations))]
    channels += [NodeChannel(bone, ROTATION, tuple(rotations[bone])) for bone in rig.bones]
    return AnimationClip(
        name=name,
        times=tuple(frame / fps for frame in frames),
        channels=tuple(channels),
        extras={"frames": motion.frames, "fps": fps},
    )
