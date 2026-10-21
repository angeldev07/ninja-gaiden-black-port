---
tags: [ngb, reverse-engineering, xbe]
updated: 2026-10-20
status: current
---

# default.xbe: anatomía del ejecutable

Fuente de todos los datos: `python -I -m ngb_tools.xbe <xbe> --strings`, que genera el reporte en
`build/reports/xbe/<nombre>.{json,md}`. Todo lo de esta página es `VERIFIED` salvo que se indique
otra cosa. El líder comprobó una muestra contra el binario: librerías, número de imports, secciones
y la cadena `MSC1310 XTL5849`.

## Identidad

| Campo | Valor |
| --- | --- |
| Archivo | `default.xbe`, 4,743,168 bytes, sha256 `B61F6EB5…D618` |
| Título | `NINJA GAIDEN Black`, versión 3 |
| Title ID | `0x5443000D` (TC-013). Alternativo: TC-003 |
| Región | `0x4` REST_OF_WORLD (`INFERRED`: copia PAL, coherente con el nombre Redump «Europe») |
| Medio permitido | `0x202` (DVD_X2 más el bit MEDIA_BOARD; el significado real de ese bit es dudoso) |
| Certificado | timedate 2005-09-08 |
| Tipo | **retail**: el entry y el thunk se des-XORean con las claves retail sin ambigüedad |
| Base | `0x00010000` |
| Entry point | `0x0032DA55` (en `.text`) |
| Kernel thunk | `0x003E3AE0` (inicio de `.rdata`) |
| TLS | `0x003E4258` |
| Init flags | MOUNT_UTILITY_DRIVE y LIMIT_64MB |

## Origen del build

- **Nombre en clave del motor: "Kunai".** La ruta de depuración es `c:\Kunai\~output\release_ltcg\Kunai.exe`,
  y en las cadenas aparecen `d:\Kunai.xbe` y `" TeamNINJA 2003"`.
- Registro CodeView `RSDS` en el campo `0x180` de la cabecera: PDB `c:\Kunai\~output\release_ltcg\Kunai.pdb`,
  GUID `BAAFA588-83BB-468F-9250-0560D06EC3A6`, age 1. **El PDB no está en el disco.** Si apareciera algún
  build con ese GUID, nos daría símbolos.
- **Toolchain:** la cadena `MSC1310 XTL5849` indica MSVC 13.10 (Visual Studio .NET 2003) con el XDK 5849.
- **Build `release_ltcg`** (link-time code generation): 0 asserts y 0 cadenas `.c`/`.cpp`/`.h`. Las 45 rutas que
  detecta la herramienta son de assets y del sistema (`z:\%s.afs`, `D:\bgm\`, …), no de fuentes.
- Los 4 descriptores de RTTI que hay son de la librería estándar (`exception`, `logic_error`…). No aparece
  ningún `.?AV` propio del motor. `INFERRED`: el motor se compiló sin RTTI.

## Secciones (16)

`.text`, `D3DX`, `DSOUND`, `XONLINE`, `XNET`, `PSFD00`, `XGRPH`, `D3D`, `XPP`, `.rdata`, `.data`, `DOLBY`,
`XON_RD`, `$$XTIMAGE`, `$$XSIMAGE` y `.XTLID`. El SHA-1 de las 16 secciones coincide con el digest guardado en
la cabecera (`VERIFIED`), así que los bytes crudos que lee la herramienta son los que firmó el linker. El
mapeo de virtual a archivo es `INFERRED`: lo respaldan las cadenas, el certificado y el thunk, que se
resuelven bien al seguir punteros virtuales.

`INFERRED`: `PSFD00` es Sofdec (el video de CRI). `D3D`, `DSOUND` y `XGRPH` son librerías del XDK enlazadas
en secciones propias, lo que facilita aislarlas en el rehosting ([[adr-01-enfoque-port]]).

## Librerías del XDK (todas del build 5849)

| Librería | QFE | Nota |
| --- | --- | --- |
| XAPILIB | 1 | API de alto nivel de Xbox |
| D3D8LTCG | 15 | **Direct3D 8 en variante LTCG**: el código de D3D puede estar inlineado en el juego. Esto complica identificar sus funciones por firmas en H5 |
| D3DX8 | 1 | utilidades de D3DX |
| XGRAPHCL | 9 | XGraphics (swizzle de texturas, etc.) |
| DSOUND | 11 | DirectSound |
| XONLINES | 17 | Xbox Live (flag "approved" = posible) |
| XBOXKRNL | 1 | stubs del kernel |
| LIBCMT / LIBCPMT | 1 | CRT de C y C++ |

## Imports del kernel: 145 (128 funciones y 17 datos)

Todos los ordinales se resolvieron con la tabla de nxdk (`src/ngb_tools/data/xboxkrnl_exports.tsv`, CC0).
El campo `kind` de la tabla los separa (`VERIFIED`, `kernel_imports` en el informe):
Los primeros son: `MmPersistContiguousMemory`, `MmAllocateContiguousMemory`, `LaunchDataPage`,
`MmFreeContiguousMemory`, `RtlNtStatusToDosError`, `HalReturnToFirmware`, `NtClose`,
`NtQueryVolumeInformationFile`, `NtOpenFile` y `RtlInitAnsiString`.
- **128 funciones**: lo que el kernel HLE de H6 tiene que implementar o dejar como stubs.
- **17 exports de datos**: variables que el kernel HLE tiene que exponer con su contenido. Son
  `LaunchDataPage`, `KeTickCount`, `XboxHardwareInfo`, `XboxKrnlVersion`, `XeImageFileName`,
  `PsThreadObjectType`, `ExEventObjectType`, `IdexChannelObject`, `HalDiskCachePartitionCount`,
  `HalBootSMCVideoMode`, `HalDiskSerialNumber` y `HalDiskModelNumber`, más las claves (`XboxHDKey`,
  `XboxSignatureKey`, `XboxAlternateSignatureKeys`, `XboxLANKey` y `XePublicKeyData`).

## Middleware de CRI enlazado (cadenas de versión, builds de mayo y junio de 2005)

ADXT 9.50, ADXF 7.33, ADXXB 1.62, AIXP 2.32, ADXENC 1.10, SJ 6.32, SVM 1.60, CVFS 2.40, LSC 2.25, RNA 3.65,
WXCI 2.04, MFCI 1.10 y SKG 0.64. Sofdec: MWSFD 3.57.02, SFD 1.964, MPV 1.964, MPS 1.924, SFH 1.19 y SFX 2.29.
De aquí sale el audio ADX/AIX ([[afs]]) y el video `.sfd` ([[disco-ngb]]).

## Pistas sobre el motor (cadenas)

- **E/S de assets:** `z:\%s.afs`, `%s.afs`, `%s\live00.afs`, `spr.afs`, `sse.afs`, `bgm.afs`, `D:\bgm\`,
  `Z:\instinfo.ins`, `movie\%s`, `ninja.aix`, `dsstdfx.bin`, `load_img.bin` y `MFS:%08x.%08x`.
  `INFERRED`: `z:` es el utility drive, usado como caché en el disco duro.
- **Partidas guardadas:** `save%03d.dat`, `system.dat` y `t:\system.dat`.
- **Lanzador interno:** `D:\NinjaTomoe.xbe`, `D:\NinjaTrilogy.xbe`, `D:\default.xbe`, `d:\dashupdate.xbe`
  y `y:\xboxdash.xbe`.
- **1788 identificadores del motor**, agrupados por prefijo: STRY 479 (163 son `STRY_EVEFLG_*`, flags de
  evento), ITEM 393, SOBJ 367 (objetos de escenario), STG 214, CHAR 112, RM 45 e ITMCOMP 14. Los
  prefijos de zona (TLN, TOMB, RAIL…) coinciden con los nombres de los `.afs`.

## Los otros XBE del disco

Fuente: `build/reports/xbe/<nombre>.json` de cada uno (`VERIFIED`, salida de la herramienta).

| XBE | Título | Libs y notas |
| --- | --- | --- |
| `NinjaTrilogy.xbe` / `NinjaTomoe.xbe` | `ryukenden (us)` / `ryukenden (jp)` | Gemelos, uno US y otro JP. Es el emulador de los Ninja Gaiden de NES (Ryukenden). D3D8 **sin** LTCG y 100 imports. La ruta de depuración está en Shift-JIS: `e:\prog\work\tmg\…\ryukenden (xbox us).exe` |
| `update.xbe` | Online Updater Application | Microsoft, `D:\xboxfre-dec03\private\online\xodash\…\TitleUpdate.exe` |
| `dashupdate.xbe` | Xbox Dashboard Updater | Microsoft, `…\dashupd\obj\i386\DashUpdate.exe` |

Los 5 comparten el certificado (TC-013, misma fecha): se firmaron juntos.

## Discrepancias con la documentación pública

Fuentes: [xboxdevwiki/Xbe](https://xboxdevwiki.net/Xbe) y
[Cxbx-Reloaded `Xbe.h`](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/common/xbe/Xbe.h),
esta última solo para contrastar el layout (no se copió nada).

- `0x130`: xboxdevwiki lo llama "stack size" y Cxbx `dwPeStackCommit`; la herramienta usa `stack_size`.
- `0x180`: xboxdevwiki lo llama "Unknown2 (XDK ≥ 5455)", pero aquí contiene la dirección virtual del
  registro CodeView `RSDS`. `0x178` vale cero en los 5 XBE.
- nxdk exporta los ordinales 1 a 366 y 374 a 378. Los 367 a 373 no existen, y ninguno aparece en estos XBE.

## Siguiente

- H5: proyecto Ghidra con ghidra-xbe, e identificar D3D8LTCG, DSOUND y XAPI por firmas.
- H6: el kernel HLE parte de estas 128 funciones y 17 variables.

## H5: funciones con nombre (Ghidra + XbSymbolDatabase, `VERIFIED`)

El proyecto Ghidra está en `NGB_DATA\ghidra\ngb`, y se reproduce con `tools/ghidra/h5-run.ps1` ([[herramientas]]).
El informe completo, `NGB_DATA\ghidra\reports\h5-cobertura.md`, va acompañado de las decompilaciones, que son
código derivado del juego y **no van al repo**.

- **9,118 funciones, 548 con nombre (6.0 %):** D3D8 98, DSOUND 113, XAPI 130, XONLINE 58, D3DX8 4, XGRAPHICS 2 y
  LIBC/LIBCPMT 142 (Function ID).
- **Por sección:** en `D3D`, 96 de 220 funciones tienen nombre (el 38 % del código). En `.text`, que es el motor
  Kunai más CRI, solo el 0.71 % de los bytes tiene nombre, porque las firmas cubren las librerías del XDK y no el
  código del juego.
- **31 globales de D3D8** con nombre, por ejemplo `g_pDevice` en `0x3DAA88`, `g_RenderState` en `0x3DAA98`,
  `g_DeferredTextureState` en `0x3D83D0` y `g_Stream` en `0x3DBA68`. También tienen nombre los 145 thunks del
  kernel.
- **Primera MMIO del NV2A** (`0x3D171B`, [[arranque-nativo]]): cae en `FUN_003d1715`, dentro de
  `Direct3D_CreateDevice` → `FUN_003cf700` → `CMiniport_InitHardware`. Ahí se mapean los registros de `0xFD000000`.
- **Espera de vblank de H7a** (`0x3CE6CF`): es `KeWaitForSingleObject(dev+0x1DBC = m_VerticalBlankEvent)`, en el
  bucle de AvSetDisplayMode, dentro del epílogo de `D3DDevice_Swap_0`. El evento lo señala la ISR `0x3D2040` a
  través de la DPC `0x3D2310`.

Lo que esto implica para el render está en [[adr-06-render-hibrido]].
