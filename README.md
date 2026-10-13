# JKCraft

JKCraft — экспериментальный одиночный клиент, который соединяет **Star Wars Jedi Knight: Jedi Academy** (через OpenJK) и **Minecraft: Java Edition**. OpenJK ведёт уровни, NPC и миссии; Minecraft предоставляет персонажа, движение, блоки, инвентарь и интерфейс. Изображение выводится в окне OpenJK.

Автор JKCraft — **Venobin**. В разработке использовался **Codex (OpenAI)** как инструмент ИИ; это не означает авторства или официальной поддержки проекта OpenAI. Проект **основан на [SkyCraft от chasmlol](https://github.com/chasmlol/SkyCraft)**. Minecraft-часть развивалась на основе его исходников; поэтому исторические имена `skycraft` ещё встречаются в пакетах Java, идентификаторах и отдельных файлах. Движок Jedi Academy основан на [OpenJK](https://github.com/JACoders/OpenJK). Новые части JKCraft доступны по MIT; изменённый OpenJK сохраняет свою лицензию GPL-2.0. Подробности — в [AUTHORS.md](AUTHORS.md) и [LICENSES.md](LICENSES.md).

Состояние: **0.1.3 alpha, Windows, одиночная игра**. Это тестовая версия: возможны ошибки физики, камер, миссий и производительности. Мультиплеер пока не готов. JKCraft не связан с правообладателями Jedi Academy и Minecraft.

## Играть

Готовый клиент для Windows: **[скачать JKCraft 0.1.3 Alpha](https://github.com/Venobin/JKCraft/releases/tag/v0.1.3-alpha)**. Архив находится в разделе Releases, а не среди исходников. Распакуйте его целиком и следуйте `INSTALLATION_RU.txt` внутри архива. Потребуются ваши игровые ресурсы Jedi Academy; при первом запуске Minecraft/Fabric и зависимости загружаются в локальный кэш.

В папке клиента запускайте `JKCraft.cmd`. По умолчанию локальный ник — `Jaden`; его можно изменить в лаунчере. Во время игры кнопка становится красной «ЗАКРЫТЬ». Закрытие OpenJK завершает скрытый Minecraft, но лаунчер остаётся открытым для следующего запуска; закрытие лаунчера завершает оба запущенных им игровых процесса. Сохранения и настройки лежат в `GameResources` и `UserData` внутри клиента — сохраняйте эти папки при обновлении.

## Исходники

| Путь | Содержимое |
| --- | --- |
| `OpenJK-master/` | Изменённый OpenJK и код связи с Minecraft. |
| `SkyCraft-main/fabric/` | Fabric-мод Minecraft, унаследованный от SkyCraft и адаптированный для Jedi Academy. |
| `launcher/` | Java-лаунчер, сценарии запуска и тексты клиентского выпуска. |

Сборка и ограничения процесса описаны в [BUILDING.md](BUILDING.md). Для сообщения об ошибке укажите карту, действия для воспроизведения и версии клиента; перед публикацией логов удалите личные данные и локальные пути.

Этот репозиторий содержит только исходники. Правила подготовки будущих коммитов и выпусков — в [PUBLISHING.md](PUBLISHING.md).

---

## English

JKCraft is an experimental single-player client that brings **Star Wars Jedi Knight: Jedi Academy** and **Minecraft: Java Edition** together. A modified [OpenJK](https://github.com/JACoders/OpenJK) runs Jedi Academy levels, NPCs and missions. The Minecraft side provides the player character, movement, blocks, inventory and HUD. The combined view appears in the OpenJK window.

JKCraft was created by **Venobin** with AI-assisted development using **Codex (OpenAI)**. Codex is credited as a development tool; JKCraft is not an official OpenAI project. The Minecraft integration is **based on [SkyCraft by chasmlol](https://github.com/chasmlol/SkyCraft)**. Some Java packages, identifiers and files still use the historical name `skycraft`. See [AUTHORS.md](AUTHORS.md) and [LICENSES.md](LICENSES.md) for attribution and license details. New JKCraft components use MIT; the modified OpenJK tree retains its GPL-2.0 license.

Current status: **0.1.3 alpha, Windows, single-player**. This is a playtest build, so physics, cameras, missions and performance may still have issues. Multiplayer is not ready. JKCraft is not affiliated with the rights holders of Jedi Academy or Minecraft.

### Playing

Windows client: **[download JKCraft 0.1.3 Alpha](https://github.com/Venobin/JKCraft/releases/tag/v0.1.3-alpha)**. The archive is attached to the GitHub Release, not stored in the source tree. Extract it completely and follow its `INSTALLATION_EN.txt` instructions. You must provide your own Jedi Academy game assets. The first launch downloads the Minecraft/Fabric components and dependencies into a local cache.

Run `JKCraft.cmd` from the extracted client folder. The default offline player name is `Jaden`, and you can change it in the launcher. While the game is running, the Play button becomes a red Close button. Closing OpenJK stops its background Minecraft process but leaves the launcher open; closing the launcher stops both game processes it started. Keep the client's `GameResources` and `UserData` folders when updating if you want to preserve saves and settings.

### Source tree

| Path | Purpose |
| --- | --- |
| `OpenJK-master/` | Modified OpenJK engine and the Minecraft bridge. |
| `SkyCraft-main/fabric/` | Fabric mod adapted from SkyCraft for Jedi Academy. |
| `launcher/` | Java launcher, startup and packaging scripts, and client release text. |

For build requirements and current packaging limitations, see [BUILDING.md](BUILDING.md). This is not yet a one-command build from a fresh clone. Before reporting a bug, note the level, steps to reproduce it and client version; remove personal information and local paths from any logs you share. Review [PUBLISHING.md](PUBLISHING.md) before committing files or preparing a release.
