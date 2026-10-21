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

## Herramientas propias (`tools/`)

Solo stdlib de Python 3.13. Tests en `tests/` con `unittest`:
`python -m unittest discover -s tests -v`. Sobre datos del juego siempre con `python -I`.

| Herramienta | Qué hace | Uso | Página |
| --- | --- | --- | --- |
| `tools/xbe_info.py` | Parsea el XBE: cabecera, certificado, secciones, librerías del XDK, imports del kernel y strings | en construcción (H1) | [[xbe-default]] |
| `tools/afs.py` | Lista, extrae e inventaría los contenedores AFS | en construcción (H1) | [[afs]] |

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
