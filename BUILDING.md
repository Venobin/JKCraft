# Сборка из исходников / Building from source

Публичный репозиторий содержит исходники, а не переносимую игровую сборку. Сейчас выпуск собирается на Windows; скрипт `launcher/Build-Release.ps1` использует уже подготовленные локальные `dist/`, `.tools/` и результаты сборки, поэтому **чистое клонирование не превращается в готовый клиент одной командой**. Эти каталоги намеренно исключены из Git.

Компоненты:

1. Minecraft-мод: `SkyCraft-main/fabric/`, Java 25 и Gradle Wrapper (`gradlew.bat`). Версии Minecraft, Fabric Loader и Fabric API закреплены в `gradle.properties`. Для сборки используйте `gradlew.bat build` из этой папки с настроенным `JAVA_HOME`.
2. OpenJK: `OpenJK-master/`, CMake и компилятор Visual C++ с целевой архитектурой x86. В текущем дереве изменения JKCraft находятся в исходниках OpenJK и должны собираться вместе с ним; отдельный DLL из старой версии клиента не подойдёт.
3. Лаунчер: `launcher/src/JKCraftLauncher.java`. Для него нужна Java 17+ (автономный клиент включает Java 25). `launcher/Build-Launcher.ps1` собирает JAR с локальным Java SDK в `.tools/`. Скрипт создания выпуска копирует этот JAR и готовые компоненты в папку клиента.

После изменения `OpenJK-master/code/jkcraft/jkc_protocol.h` пересобирайте **все три** компонента OpenJK: `openjk_sp.x86.exe`, `jagamex86.dll` и `rdsp-vanilla_x86.dll`. Старый DLL рендерера может оставить Minecraft невидимым даже при рабочей связи и управлении.

Для теста клиенту требуются **отдельно предоставленные пользователем** файлы `assets0.pk3`–`assets3.pk3` Jedi Academy. Не добавляйте их, Minecraft assets, кэши загрузки, сохранения, логи или персональные настройки в коммиты. Первый запуск клиента требует сети для подготовки зависимостей Minecraft.

## English

This public repository contains source code, not a portable game build. Releases are currently assembled on Windows. `launcher/Build-Release.ps1` expects previously prepared local `dist/`, `.tools/` and compiled outputs, so **a fresh clone cannot produce a complete client with one command**. Those directories are deliberately excluded from Git.

Components:

1. **Minecraft mod:** `SkyCraft-main/fabric/` requires Java 25 and the Gradle Wrapper (`gradlew.bat`). Minecraft, Fabric Loader and Fabric API versions are pinned in `gradle.properties`. From that directory, run `gradlew.bat build` with `JAVA_HOME` pointing to Java 25.
2. **OpenJK:** Build `OpenJK-master/` with CMake and an x86 Visual C++ toolchain. JKCraft's OpenJK changes are part of this source tree and must be compiled together. A DLL from an older client build is not interchangeable.
3. **Launcher:** `launcher/src/JKCraftLauncher.java` requires Java 17 or later; the standalone client includes Java 25. `launcher/Build-Launcher.ps1` builds its JAR using a local Java SDK in `.tools/`. The release script copies that JAR and the compiled components into a client folder.

If you change `OpenJK-master/code/jkcraft/jkc_protocol.h`, rebuild **all three** OpenJK components: `openjk_sp.x86.exe`, `jagamex86.dll` and `rdsp-vanilla_x86.dll`. Keeping an old renderer DLL can make Minecraft invisible even when the bridge and controls are working.

Testing requires Jedi Academy's `assets0.pk3`–`assets3.pk3` files, supplied separately by the player. Do not commit those files, Minecraft assets, download caches, saves, logs or personal settings. The first client launch needs an internet connection to prepare Minecraft dependencies.
