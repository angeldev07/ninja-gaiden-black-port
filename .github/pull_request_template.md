## Qué

<!-- Qué cambia, en 2-5 viñetas. -->

## Por qué

<!-- Hito del roadmap (H1, H2, …) y motivo. -->

## Evidencia

<!-- Comandos ejecutados y su resultado: tests, reportes, muestras verificadas. -->

## Checklist

- [ ] Ningún dato del juego en el diff (`python -m ngb_tools.hygiene` en verde)
- [ ] `ruff check .`, `ruff format --check .` y `mypy` en verde
- [ ] `python -m unittest discover -s tests -v` en verde
- [ ] Vault actualizado: páginas afectadas, `docs/index.md` si hay página nueva, entrada en `docs/log.md`
- [ ] Afirmaciones de RE con ancla y etiqueta `VERIFIED` / `INFERRED` / `UNVERIFIED`
