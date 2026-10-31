# Launcher fonts

Embedded in `ngb-launcher.exe` as `RCDATA` (see `port/launcher/launcher.rc`) and loaded privately with
`AddFontMemResourceEx`, so nothing is installed on the player's system. Both families are SIL OFL 1.1; their
licenses are next to them. `.gitignore` ignores `*.ttf`, so a change here goes in with `git add -f`.

| File | Source | Notes |
| --- | --- | --- |
| `JosefinSans-Thin.ttf`, `-Light.ttf`, `-SemiBold.ttf` | `googlefonts/josefinsans`, `fonts/ttf/` | Unmodified |
| `ShipporiMinchoB1-SemiBold-kanji.ttf` | `google/fonts`, `ofl/shipporiminchob1/ShipporiMinchoB1-SemiBold.ttf` | Subset to the kanji below |
| `YujiBoku-Regular-dragon.ttf` | `google/fonts`, `ofl/yujiboku/YujiBoku-Regular.ttf` | Subset to 龍 only: the launcher's emblem and, through `--emblem-mask`, the app icon (`port/res/make_icon.py`) |

Windows only ships a Mincho face with the optional Japanese supplemental fonts, so the launcher carries the
handful of kanji it draws. To add one, regenerate the subset with fontTools (a development tool, not a
dependency of the project) and list the character here:

```
pip install fonttools==4.55.3
pyftsubset ShipporiMinchoB1-SemiBold.ttf --text="忍者外伝始一二三四五六画音操設記録龍剣黒" ^
    --output-file=ShipporiMinchoB1-SemiBold-kanji.ttf --name-IDs="*" --layout-features=""
```

The brush dragon is subset the same way: `pyftsubset YujiBoku-Regular.ttf --text="龍" --output-file=YujiBoku-Regular-dragon.ttf --name-IDs="*" --layout-features=""`.

Kanji in the Shippori subset: 忍者外伝 (Ninja Gaiden), 始 (start), 一二三四五六 (steps), 画 音 操 設 記 録 (page marks),
龍 剣 (dragon, sword), 黒 (black).
