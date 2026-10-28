# Публикация изменений и выпусков / Publishing changes and releases

Этот репозиторий содержит исходники JKCraft, унаследованный код SkyCraft с его лицензией, изменённый OpenJK с его лицензией, лаунчер и документацию. Это **не** готовый клиент и не архив игровых ресурсов.

Перед каждым коммитом или выпуском проверьте:

1. В репозитории нет `GameResources`, `UserData`, `RuntimeCache`, `dist`, `releases`, сохранений, логов, аккаунтов и файлов `assets*.pk3`.
2. В README указаны происхождение SkyCraft и OpenJK, а файлы `LICENSE`, `SkyCraft-main/LICENSE` и `OpenJK-master/LICENSE.txt` сохранены.
3. Не загружайте автономный клиент в Git-коммит с исходниками. После проверки лицензий и состава архива прикрепляйте его к отдельному GitHub Release.
4. Выполните `git status --short` и просмотрите новые файлы перед `git add` и `git push`.
5. Не добавляйте внутренние дорожные карты, журналы разработки и черновики описаний выпусков (`ROADMAP*.md`, `JKCRAFT_STATUS.md`, `RELEASE_NOTES*.md`). Описание выпуска публикуйте в GitHub Releases; в исходниках оставляйте только документацию, нужную для использования и сборки проекта, и обязательные сведения о лицензиях.

Исходный репозиторий: [Venobin/JKCraft](https://github.com/Venobin/JKCraft). Он содержит отдельную копию исходников; локальные рабочие каталоги, кэши и игровые файлы в него не входят.

## English

This repository contains JKCraft source code, inherited SkyCraft code and its license, modified OpenJK and its license, the launcher and documentation. It is **not** a ready-to-run client or a collection of game assets.

Before each commit or release:

1. Keep `GameResources`, `UserData`, `RuntimeCache`, `dist`, `releases`, saves, logs, account data and `assets*.pk3` out of the source repository.
2. Preserve the SkyCraft and OpenJK attribution in the README and the license files at `LICENSE`, `SkyCraft-main/LICENSE` and `OpenJK-master/LICENSE.txt`.
3. Keep standalone clients out of source commits. Review the archive and license notices, then attach the client to a separate GitHub Release.
4. Run `git status --short` and inspect new files before `git add` and `git push`.
5. Keep internal roadmaps, development journals and release-note drafts (`ROADMAP*.md`, `JKCRAFT_STATUS.md`, `RELEASE_NOTES*.md`) local. Publish release descriptions on GitHub Releases; keep source documentation focused on using and building the project and required license notices.

The source repository is [Venobin/JKCraft](https://github.com/Venobin/JKCraft). Local development directories, caches and game files are not part of it.
