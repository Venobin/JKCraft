# JKCraft 0.1.4 alpha

## Русский

Экспериментальный клиент для Windows, объединяющий Jedi Academy и Minecraft. Версия локального протокола OpenJK ↔ Minecraft: **19**.

### Изменения

- F6 переключает управление Minecraft ↔ Jedi, сохраняя отрисовку Minecraft-блоков, мобов и предметов в обоих режимах.
- Добавлены передача управления и подтверждение телепортации, столкновения джедая с поставленными блоками и синхронизация общего здоровья.
- Доработаны взаимодействия оружия и Силы с Minecraft-мобами, а также воздействие Minecraft-окружения на нативного персонажа.
- В лаунчере появились экспериментальные режимы «Создать игру» и «Подключиться»; добавлены проверки карты OpenJK и версии протокола.
- Клиент поставляется согласованным комплектом OpenJK, Fabric-мода и лаунчера, с Java 25 в комплекте.

### Проверено

- Сборка OpenJK x86 Release, Fabric-мода и лаунчера.
- 32 автоматических теста коллизии, попаданий и баланса боя: без ошибок.
- 50 переключений управления через штатный IPC на карте `t1_sour`: подтверждения переходов, здоровье, heartbeat и публикация кадров оставались рабочими.
- Minecraft сохраняет тестовый мир и завершается после закрытия OpenJK.

### Ограничения

Это предварительная alpha для одиночного тестирования. Режимы хоста и подключения не означают готовый кооператив: NPC и миссии ещё не синхронизированы. Физическая F6, вид камеры, бой, катсцены, смена карты и загрузка сохранений требуют дальнейших ручных проверок. В тестовых логах встречаются редкие тайм-ауты запросов коллизии.

Распакуйте Windows ZIP целиком в новую папку, следуйте `INSTALLATION_RU.txt` и предоставьте свои `assets0.pk3`–`assets3.pk3`. Первый запуск загружает зависимости Minecraft/Fabric. При обновлении сохраните `GameResources` и `UserData` из старой установки. Не смешивайте EXE, DLL или JAR разных версий.

## English

Experimental Windows client combining Jedi Academy and Minecraft. Local OpenJK ↔ Minecraft bridge protocol: **19**.

### Changes

- F6 switches Minecraft/Jedi control while Minecraft blocks, mobs and items continue rendering in both modes.
- Control handoff and teleport acknowledgement, native Jedi collision with placed blocks, and shared health synchronization.
- Updates to Jedi weapons/Force interactions with Minecraft mobs and Minecraft environmental effects on the native player.
- Experimental Host and Join launcher modes with OpenJK map and protocol compatibility checks.
- Matching OpenJK, Fabric mod and launcher components; Java 25 is bundled.

### Validation and limitations

OpenJK x86 Release, Fabric and launcher builds; 32 automated collision/combat tests; 50 acknowledged IPC control transitions on `t1_sour` with active heartbeats and frame publication. Minecraft saved its test world and exited after OpenJK stopped.

This is a single-player testing alpha. Host/Join is experimental: NPC and mission synchronization is not complete. Physical F6 input, visual camera behavior, combat, cinematics, map changes and save loading still require manual validation. Occasional collision query timeouts remain in test logs.

Extract the complete Windows ZIP into a new folder, follow `INSTALLATION_EN.txt`, and provide your own `assets0.pk3`–`assets3.pk3`. First launch downloads Minecraft/Fabric dependencies. Keep `GameResources` and `UserData` when updating. Do not mix binaries or JARs from different versions.
