# Сборка из исходников / Building from source

Публичный репозиторий содержит исходники, а не переносимую игровую сборку. Сейчас выпуск собирается на Windows; скрипт `launcher/Build-Release.ps1` использует уже подготовленные локальные `dist/`, `.tools/` и результаты сборки, поэтому **чистое клонирование не превращается в готовый клиент одной командой**. Эти каталоги намеренно исключены из Git.

Компоненты:

1. Minecraft-мод: `SkyCraft-main/fabric/`, Java 25 и Gradle Wrapper (`gradlew.bat`). Версии Minecraft, Fabric Loader и Fabric API закреплены в `gradle.properties`. Для сборки используйте `gradlew.bat build` из этой папки с настроенным `JAVA_HOME`.
2. OpenJK: `OpenJK-master/`, CMake и компилятор Visual C++ с целевой архитектурой x86. В текущем дереве изменения JKCraft находятся в исходниках OpenJK и должны собираться вместе с ним; отдельный DLL из старой версии клиента не подойдёт.
3. Лаунчер: `launcher/src/JKCraftLauncher.java`. Для него нужна Java 17+ (автономный клиент включает Java 25). `launcher/Build-Launcher.ps1` собирает JAR с локальным Java SDK в `.tools/`. Скрипт создания выпуска копирует этот JAR и готовые компоненты в папку клиента.

После изменения `OpenJK-master/code/jkcraft/jkc_protocol.h` пересобирайте **все три** компонента OpenJK: `openjk_sp.x86.exe`, `jagamex86.dll` и `rdsp-vanilla_x86.dll`. Старый DLL рендерера может оставить Minecraft невидимым даже при рабочей связи и управлении.

Для теста клиенту требуются **отдельно предоставленные пользователем** файлы `assets0.pk3`–`assets3.pk3` Jedi Academy. Не добавляйте их, Minecraft assets, кэши загрузки, сохранения, логи или персональные настройки в коммиты. Первый запуск клиента требует сети для подготовки зависимостей Minecraft.

The same applies in English: this is a source tree, not a one-command portable build. The current packaging script expects local build artifacts. Game data and personal runtime files are intentionally ignored by Git.
