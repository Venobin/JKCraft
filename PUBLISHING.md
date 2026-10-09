# Публикация изменений и выпусков / Publishing changes and releases

Этот репозиторий содержит исходники JKCraft, унаследованный код SkyCraft с его лицензией, изменённый OpenJK с его лицензией, лаунчер и документацию. Это **не** готовый клиент и не архив игровых ресурсов.

Перед каждым коммитом или выпуском проверьте:

1. В репозитории нет `GameResources`, `UserData`, `RuntimeCache`, `dist`, `releases`, сохранений, логов, аккаунтов и файлов `assets*.pk3`.
2. В README указаны происхождение SkyCraft и OpenJK, а файлы `LICENSE`, `SkyCraft-main/LICENSE` и `OpenJK-master/LICENSE.txt` сохранены.
3. Не загружайте автономный клиент и исходники в один и тот же Git-коммит. Готовый клиент можно выпускать отдельно после самостоятельной проверки лицензий и состава архива.
4. Выполните `git status --short` и просмотрите новые файлы перед `git add` и `git push`.

Исходный репозиторий: [Venobin/JKCraft](https://github.com/Venobin/JKCraft). Он содержит отдельную копию исходников; локальные рабочие каталоги, кэши и игровые файлы в него не входят.

**English:** This repository contains source code, not a ready-to-run client. Before pushing changes or publishing a release, review the files and licenses, and keep game assets, saves, logs, caches and credentials out of Git.
