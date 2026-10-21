---
tags: [ngb, tools]
updated: 2026-10-20
status: draft
---

# Herramientas: cómo se usan

## extract-xiso (externa)

- v2.7.1, Win64, de los releases de [XboxDev/extract-xiso](https://github.com/XboxDev/extract-xiso).
  Ruta: `%USERPROFILE%\xemu\tools\extract-xiso\artifacts\extract-xiso.exe`.
- Las opciones están `VERIFIED` con la salida de `extract-xiso.exe -h` (2026-10-20).
- **De Redump a XISO sin tocar el original:** `extract-xiso.exe -r -d "<carpeta_destino>" "<redump>.iso"`.
  Sin `-d` reescribe en el mismo sitio y deja un `.old`. `-r` aplica por defecto el parche de "media enable"
  al XBE (se desactiva con `-m`).
- **Extraer el árbol:** `extract-xiso.exe -x -d "%USERPROFILE%\ngb-data\game" "<juego>.xiso.iso"`.
- **Listar:** `extract-xiso.exe -l "<juego>.xiso.iso"`.
- Convención de nombre: `<Juego>.xiso.iso`.
- Gotcha: en este entorno, `Invoke-WebRequest` falla con `release-assets.githubusercontent.com`. Con `curl -L` sí funciona.

## Herramientas propias: el paquete `ngb_tools` (`src/ngb_tools/`)

Usan solo la stdlib de Python 3.13 y no tienen dependencias de runtime. El layout es `src/`, y cada
herramienta es un subpaquete con su propia CLI.

**Puesta en marcha, una vez por clon:**
```sh
python -m venv .venv
.venv/Scripts/python -m pip install -e ".[dev]"   # ruff y mypy fijados
git config core.hooksPath .githooks              # activa la guardia pre-commit
```

**Ejecución sobre datos del juego:** con el paquete instalado, `python -I -m ngb_tools.<herramienta> …`.
`-I` deja el directorio de los datos fuera de `sys.path`. En un worktree sin instalar se usa
`PYTHONPATH=src python -P -s -m ngb_tools.<herramienta> …` desde la raíz del repo, que es equivalente.

**Calidad:** `ruff format .`, `ruff check .`, `mypy` (strict) y `python -m unittest discover -s tests -v`.

| Herramienta | Qué hace | Uso | Página |
| --- | --- | --- | --- |
| `ngb_tools.afs` | Lista, extrae e inventaría contenedores AFS (estructura, invariantes, magias, entropía) | `python -I -m ngb_tools.afs list|extract|inventory …` | [[afs]] |
| `ngb_tools.xbe` | Analiza ejecutables XBE: cabecera, certificado, secciones, librerías del XDK, imports del kernel y strings | `python -I -m ngb_tools.xbe <xbe> [--strings] [--json …] [--md …]` | [[xbe-default]] |
| `ngb_tools.xpr` | Exporta las texturas de un XPR0 (suelto o dentro de un KMD) a PNG: DXT1/3/5, P8, A8, ARGB, des-swizzle | `python -I -m ngb_tools.xpr list|export <archivo> [--kmd]` | [[xpr0]] |
| `ngb_tools.kmd` | Exporta modelos KMD a glTF 2.0 u OBJ, con jerarquía, materiales y texturas | `python -I -m ngb_tools.kmd info|export <kmd>` | [[kmd]] |
| `ngb_tools.audio` | Decodifica ADX, Xbox ADPCM (.wva) y XWB a WAV; análisis con espectrograma | `python -I -m ngb_tools.audio adx|wva|xwb|analyze …` | [[adx]] |
| `ngb_tools.stage` | Exporta escenarios completos (todas las partes de una zona) a glTF/GLB, con colisión opcional | `python -I -m ngb_tools.stage export <zona>` | [[escenarios]] |
| `ngb_tools.hygiene` | Guardia que impide versionar datos del juego (hook y CI) | `python -m ngb_tools.hygiene [--staged]` | [[adr-03-datos-fuera-del-repo]] |

### `ngb_tools.afs`

```sh
python -I -m ngb_tools.afs list    %USERPROFILE%\ngb-data\game\chr_ryu.afs
python -I -m ngb_tools.afs extract %USERPROFILE%\ngb-data\game\chr_ryu.afs            # → NGB_DATA\extracted\chr_ryu\
python -I -m ngb_tools.afs extract %USERPROFILE%\ngb-data\game\cmn.afs --index 253
python -I -m ngb_tools.afs inventory %USERPROFILE%\ngb-data\game --json build/reports/afs/inventory.json --md build/reports/afs/inventory.md
```

- Códigos de salida: 0 = OK, 1 = archivo que no es AFS o con invariantes rotos, 2 = error de uso o de E/S.
- `extract` se niega a escribir dentro del repo. Escribe cada archivo de forma atómica (`.part` y luego
  rename) y deja un `_manifest.json`.
- El inventario de los 206 AFS tarda unos 6 a 7 s. Lo midió el analista el 2026-10-20 en esta máquina (Ryzen 7 5700G con
  los datos en un disco local).

### `ngb_tools.xbe`

```sh
python -I -m ngb_tools.xbe %USERPROFILE%\ngb-data\game\default.xbe --strings --json build/reports/xbe/default.json --md build/reports/xbe/default.md
python -I -m ngb_tools.xbe %USERPROFILE%\ngb-data\game\default.xbe            # resumen Markdown por stdout
```

- Códigos de salida: 0 = OK, 1 = no es un XBE o no verifica (SHA-1 de sección o claves XOR desconocidas; en el
  segundo caso escribe igual el informe), 2 = error de uso o de E/S.
- La tabla de exports del kernel viene en el paquete (`ngb_tools/data/xboxkrnl_exports.tsv`, de nxdk, CC0).
  `--exports` permite usar otra.
- Hallazgos: [[xbe-default]].

### `ngb_tools.xpr`

```sh
python -I -m ngb_tools.xpr list   %USERPROFILE%\ngb-data\extracted\chr_ryu\0000_ryu.kmd --kmd
python -I -m ngb_tools.xpr export %USERPROFILE%\ngb-data\extracted\chr_ryu\0000_ryu.kmd --kmd    # → NGB_DATA\extracted\textures\0000_ryu\
python -I -m ngb_tools.xpr export %USERPROFILE%\ngb-data\game\Media\font.xpr
```

- Un PNG por textura (`NNNN_<formato>_<ancho>x<alto>.png`, nivel 0) y un `_manifest.json` con el índice, el
  formato, las dimensiones, los mips, el offset y el sha256 de los bytes de origen.
- `--kmd` busca, entre los slots del chunk de Team Ninja, el único cuyo contenido empieza por `XPR0` (en `ryu.kmd` es
  el slot 1, ver [[kmd]]). Sin `--kmd`, el archivo tiene que ser un
  XPR0 suelto (`.xpr`, `.spr`).
- Las texturas son datos del juego: nunca se escriben dentro de un repo (`ngb_tools._paths`).
- Formatos: [[xpr0]].

### `ngb_tools.kmd`

```sh
python -I -m ngb_tools.kmd info   %USERPROFILE%\ngb-data\extracted\chr_ryu\0000_ryu.kmd
python -I -m ngb_tools.kmd export %USERPROFILE%\ngb-data\extracted\chr_ryu\0000_ryu.kmd             # → NGB_DATA\extracted\models\0000_ryu\
python -I -m ngb_tools.kmd export <kmd> --format obj      # OBJ + MTL (posiciones de mundo)
python -I -m ngb_tools.kmd export <kmd> --skin --all-groups
python -I -m ngb_tools.kmd export <kmd> --format glb     # un solo archivo con texturas incrustadas
python -I -m ngb_tools.kmd export <ryu.kmd> --format glb --name ryu_sword --attach <katana.kmd>@<katana.itm>#2 --motions <rm_kt.mot>:141,151
python -I -m ngb_tools.kmd export <ryu.kmd> --format glb --motions <ryu.mtn>:1,3,90,36 --motions <rm_kt.mot>:141,151
```

- glTF 2.0 (`.gltf` + `.bin`): un nodo por hueso de HIERARC, una malla por objeto y una primitiva por submalla en
  TRIANGLES, con materiales PBR que usan los PNG de [[xpr0]]. Es glTF estándar, así que cualquier visor de glTF
  debería abrirlo (Visor 3D de Windows, Blender). Por ahora solo se ha validado con el rasterizador propio
  (`preview.png`).
- Reutiliza los PNG de H2 si su manifiesto cuadra; si no, los exporta.
- Formato: [[kmd]].

### `ngb_tools.audio`

```sh
python -I -m ngb_tools.audio adx %USERPROFILE%\ngb-data\game\bgm.afs             # 95 pistas → NGB_DATA\extracted\audio\bgm\
python -I -m ngb_tools.audio adx %USERPROFILE%\ngb-data\game\sse.afs --index 2   # una voz
python -I -m ngb_tools.audio wva %USERPROFILE%\ngb-data\game\cmn.afs             # bancos .wva → un WAV por registro
python -I -m ngb_tools.audio xwb %USERPROFILE%\ngb-data\game\Media\Wave.xwb
python -I -m ngb_tools.audio analyze <wav...> --png-dir <dir>                       # RMS, saturación, forma de onda + espectrograma
```

- Salida en WAV PCM16 con `_manifest.json` (loops, frecuencia, canales). Formatos: [[adx]] y [[wva]].

## Ghidra 12.1.4 + ghidra-xbe (externa, instalada el 2026-10-21)

- Ruta: `%GHIDRA_HOME%`, fuera del repo y de OneDrive. Usa el JDK 21 de Adoptium.
- `ghidra_12.1.4_PUBLIC_20260921.zip`, con un sha256 `ddac49f9…d4db` que coincide con el de las notas de
  [la release oficial](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build).
  `VERIFIED`.
- Extensión [XboxDev/ghidra-xbe](https://github.com/XboxDev/ghidra-xbe) build `202610161023`, compilada para la
  12.1.4. Está en `Ghidra\Extensions\ghidra-xbe` (XboxExecutableLoader: carga XBE e identifica librerías del XDK
  con XbSymbolDatabase).
- El proyecto de análisis vive en `%USERPROFILE%\ngb-data\ghidra\` (proyecto `ngb`), con sus scripts e informes.
  Resultados en [[xbe-default]] (H5).
## Cargador nativo (`port/`, C con MSVC x86)

```bat
port\build.cmd                                                        :: MSVC x86, /W4 /WX → build\port\ngb-loader.exe + ngb-core.dll
build\port\ngb-loader.exe %USERPROFILE%\ngb-data\game\default.xbe   :: ejecuta el juego hasta la 1.ª MMIO del NV2A
   [--log F] [--max-calls N] [--timeout S] [--idle S] [--echo] [--mmio-strict] [--mmio-gpu-ram]
python tools\gen_kernel_stubs.py [--check]                           :: regenera port\src\kernel\stubs.c desde el TSV
```

- Log por defecto en `NGB_DATA\runs\h6-<fecha>.log`; disco duro virtual en `NGB_DATA\runs\hdd\`.
- Códigos de salida: 0 OK, 2 llamada sin implementar, 3 crash o MMIO, 4 `--max-calls`, 5 timeout o inactividad,
  6 el juego pidió apagar.
- Diseño: [[adr-05-runtime-nativo]]. Lo que hace el juego al arrancar: [[arranque-nativo]].

### `ngb_tools.stage`

```sh
python -I -m ngb_tools.stage export boff                    # → NGB_DATA/extracted/stages/boff/boff.glb (todas las partes)
python -I -m ngb_tools.stage export ssato --parts 01,02,03,04,05
python -I -m ngb_tools.stage export boff --collision        # añade la colisión HITMDL como malla translúcida
python -I -m ngb_tools.stage info boff
```

- Junta las partes `<zona>_NN` de una zona; las variantes idénticas se exportan una vez. Formato: [[escenarios]].
