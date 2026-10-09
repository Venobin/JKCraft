# Публикация изменений и выпусков / Publishing changes and releases

Этот репозиторий содержит исходники JKCraft, унаследованный код SkyCraft с его лицензией, изменённый OpenJK с его лицензией, лаунчер и документацию. Это **не** готовый клиент и не архив игровых ресурсов.

Перед каждым коммитом или выпуском проверьте:

1. В репозитории нет `GameResources`, `UserData`, `RuntimeCache`, `dist`, `releases`, сохранений, логов, аккаунтов и файлов `assets*.pk3`.
2. В README указаны происхождение SkyCraft и OpenJK, а файлы `LICENSE`, `SkyCraft-main/LICENSE` и `OpenJK-master/LICENSE.txt` сохранены.
3. Не загружайте автономный клиент и исходники в один и тот же Git-коммит. Готовый клиент можно выпускать отдельно после самостоятельной проверки лицензий и состава архива.
4. Выполните `git status --short` и просмотрите новые файлы перед `git add` и `git push`.

Исходный репозиторий: [Venobin/JKCraft](https://github.com/Venobin/JKCraft). Он содержит отдельную копию исходников; локальные рабочие каталоги, кэши и игровые файлы в него не входят.

## English

This repository contains JKCraft source code, inherited SkyCraft code and its license, modified OpenJK and its license, the launcher and documentation. It is **not** a ready-to-run client or a collection of game assets.

Before each commit or release:

1. Keep `GameResources`, `UserData`, `RuntimeCache`, `dist`, `releases`, saves, logs, account data and `assets*.pk3` out of the source repository.
2. Preserve the SkyCraft and OpenJK attribution in the README and the license files at `LICENSE`, `SkyCraft-main/LICENSE` and `OpenJK-master/LICENSE.txt`.
3. Package any standalone client separately from source commits, and review its contents and license notices before publishing it.
4. Run `git status --short` and inspect new files before `git add` and `git push`.

The source repository is [Venobin/JKCraft](https://github.com/Venobin/JKCraft). Local development directories, caches and game files are not part of it.
