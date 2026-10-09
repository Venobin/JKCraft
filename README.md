# JKCraft

JKCraft — экспериментальный одиночный клиент, который соединяет **Star Wars Jedi Knight: Jedi Academy** (через OpenJK) и **Minecraft: Java Edition**. OpenJK ведёт уровни, NPC и миссии; Minecraft предоставляет персонажа, движение, блоки, инвентарь и интерфейс. Изображение выводится в окне OpenJK.

Автор JKCraft — **Venobin**. В разработке использовался **Codex (OpenAI)** как инструмент ИИ; это не означает авторства или официальной поддержки проекта OpenAI. Проект **основан на [SkyCraft от chasmlol](https://github.com/chasmlol/SkyCraft)**. Minecraft-часть развивалась на основе его исходников; поэтому исторические имена `skycraft` ещё встречаются в пакетах Java, идентификаторах и отдельных файлах. Движок Jedi Academy основан на [OpenJK](https://github.com/JACoders/OpenJK). Новые части JKCraft доступны по MIT; изменённый OpenJK сохраняет свою лицензию GPL-2.0. Подробности — в [AUTHORS.md](AUTHORS.md) и [LICENSES.md](LICENSES.md).

Состояние: **0.1.2 alpha, Windows, одиночная игра**. Это тестовая версия: возможны ошибки физики, камер, миссий и производительности. Мультиплеер пока не готов. JKCraft не связан с правообладателями Jedi Academy и Minecraft.

## Играть

Готовый автономный клиент распространяется отдельным архивом, а не в исходном репозитории. Если опубликован выпуск, распакуйте его целиком и следуйте `INSTALLATION_RU.txt` или `INSTALLATION_EN.txt` внутри архива. Prism Launcher не требуется. Потребуются ваши игровые ресурсы Jedi Academy; при первом запуске Minecraft/Fabric и зависимости загружаются в локальный кэш. Не помещайте игровые ресурсы в Git-репозиторий.

В папке клиента запускайте `JKCraft.cmd`. По умолчанию локальный ник — `Jaden`; его можно изменить в лаунчере. Во время игры кнопка становится красной «ЗАКРЫТЬ». Закрытие OpenJK завершает скрытый Minecraft, но лаунчер остаётся открытым для следующего запуска; закрытие лаунчера завершает оба запущенных им игровых процесса. Сохранения и настройки лежат в `GameResources` и `UserData` внутри клиента — сохраняйте эти папки при обновлении.

## Исходники

| Путь | Содержимое |
| --- | --- |
| `OpenJK-master/` | Изменённый OpenJK и код связи с Minecraft. |
| `SkyCraft-main/fabric/` | Fabric-мод Minecraft, унаследованный от SkyCraft и адаптированный для Jedi Academy. |
| `launcher/` | Java-лаунчер, сценарии запуска и тексты клиентского выпуска. |
| `Start-JKCraft-Playtest.ps1` | Локальный сценарий разработчика; не готовый клиент. |
| `JKCRAFT_STATUS.md` | Исторический технический журнал, а не актуальная инструкция установки. |

Сборка и ограничения процесса описаны в [BUILDING.md](BUILDING.md). Для сообщения об ошибке укажите карту, действия для воспроизведения и версии клиента; перед публикацией логов удалите личные данные и локальные пути.

Этот репозиторий содержит только исходники. Правила подготовки будущих коммитов и выпусков — в [PUBLISHING.md](PUBLISHING.md).

---

**English:** JKCraft is an experimental single-player bridge between Jedi Academy/OpenJK and Minecraft Java Edition, created by Venobin with AI-assisted development using Codex (OpenAI). It is based on [SkyCraft](https://github.com/chasmlol/SkyCraft) by chasmlol and uses modified [OpenJK](https://github.com/JACoders/OpenJK). The source repository does not include either game's assets or a ready-to-run client. Use the release archive and its `INSTALLATION_EN.txt` guide; see [BUILDING.md](BUILDING.md), [AUTHORS.md](AUTHORS.md), and [LICENSES.md](LICENSES.md).
