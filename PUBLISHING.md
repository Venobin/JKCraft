# Подготовка к GitHub / Preparing for GitHub

Публикуйте содержимое папки `JKCraft-GitHub` как корень репозитория. В ней находятся исходники JKCraft, унаследованный код SkyCraft с его лицензией, изменённый OpenJK с его лицензией, лаунчер и документация. Это **не** готовый клиент и не архив игровых ресурсов.

Перед публикацией проверьте:

1. В репозитории нет `GameResources`, `UserData`, `RuntimeCache`, `dist`, `releases`, сохранений, логов, аккаунтов и файлов `assets*.pk3`.
2. В README указаны происхождение SkyCraft и OpenJK, а файлы `LICENSE`, `SkyCraft-main/LICENSE` и `OpenJK-master/LICENSE.txt` сохранены.
3. Не загружайте автономный клиент и исходники в один и тот же Git-коммит. Готовый клиент можно выпускать отдельно после самостоятельной проверки лицензий и состава архива.
4. После `git init` выполните `git status --short` и просмотрите каждый файл перед `git add` и `git push`.

Папка намеренно не содержит `.git`: вы можете создать новый репозиторий, когда решите публиковать проект. Никакой GitHub-репозиторий и удалённый адрес здесь не создаются автоматически.

**English:** Use the contents of `JKCraft-GitHub` as the repository root. It is source code, not a game client. Review the files and licenses before publishing, and keep game assets, saves, logs, caches and credentials out of Git.
