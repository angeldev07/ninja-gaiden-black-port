## What

<!-- What changes, in 2-5 bullets. -->

## Why

<!-- Roadmap milestone (H1, H2, ...) and motivation. -->

## Evidence

<!-- Commands run and their results: tests, reports, verified samples. -->

## Checklist

- [ ] No game data in the diff (`python -m ngb_tools.hygiene` is green)
- [ ] `ruff check .`, `ruff format --check .` and `mypy` are green
- [ ] `python -m unittest discover -s tests -v` is green
- [ ] Vault updated: affected pages, `docs/index.md` if there is a new page, an entry in `docs/log.md`
- [ ] RE claims carry an anchor and a `VERIFIED` / `INFERRED` / `UNVERIFIED` tag
