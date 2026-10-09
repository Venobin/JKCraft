import java.awt.BasicStroke;
import java.awt.Color;
import java.awt.Desktop;
import java.awt.Dimension;
import java.awt.Font;
import java.awt.GradientPaint;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.URISyntaxException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.Properties;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.TimeUnit;
import javax.swing.BorderFactory;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JComboBox;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JProgressBar;
import javax.swing.JTextField;
import javax.swing.SwingUtilities;
import javax.swing.WindowConstants;

public final class JKCraftLauncher {
    private static final Color BACKGROUND = new Color(12, 20, 30);
    private static final Color PANEL = new Color(24, 35, 45);
    private static final Color TEXT = new Color(231, 239, 231);
    private static final Color MUTED = new Color(151, 168, 170);
    private static final Color GREEN = new Color(94, 198, 119);
    private static final Color GOLD = new Color(231, 182, 92);
    private static final Color RED = new Color(190, 55, 55);
    private static final AtomicBoolean RUNNING = new AtomicBoolean();

    private final Path workspace;
    private final Path packageRoot;
    private final Path settingsFile;
    private final Properties settings = new Properties();
    private JTextField nicknameField;
    private JLabel status;
    private JLabel nicknameLabel;
    private JLabel nicknameHint;
    private JLabel progressDetail;
    private JProgressBar progressBar;
    private JButton launchButton;
    private JButton foldersButton;
    private JButton guideButton;
    private JButton settingsButton;
    private JButton languageButton;
    private JFrame frame;
    private boolean russian;
    private volatile String currentStage = "IDLE";
    private String currentDownload;
    private final Object lifecycleLock = new Object();
    private volatile Process gameProcess;
    private volatile long minecraftPid;
    private volatile long openJkPid;
    private volatile boolean exiting;
    private final AtomicBoolean stopping = new AtomicBoolean();

    private JKCraftLauncher(Path workspace, Path packageRoot) {
        this.workspace = workspace;
        this.packageRoot = packageRoot;
        this.settingsFile = packageRoot.resolve("UserData/offline-launcher.properties");
        if (Files.isRegularFile(settingsFile)) {
            try (InputStream in = Files.newInputStream(settingsFile)) {
                settings.load(in);
            } catch (IOException ignored) {
                // A damaged preference file must never prevent the launcher opening.
            }
        }
        russian = !"EN".equalsIgnoreCase(settings.getProperty("language", "RUS"));
    }

    private static Path[] locate() throws URISyntaxException {
        Path file = Paths.get(JKCraftLauncher.class.getProtectionDomain()
            .getCodeSource().getLocation().toURI()).toAbsolutePath();
        Path parent = Files.isDirectory(file) ? file : file.getParent();
        if (Files.isRegularFile(parent.resolve("Launch-JKCraft-Offline.ps1")) &&
            Files.isRegularFile(parent.resolve("ClientCore/gradlew.bat"))) {
            return new Path[] { parent, parent };
        }
        throw new IllegalStateException("JKCraft runtime not found next to the launcher jar.");
    }

    public static void main(String[] args) {
        try {
            Path[] paths = locate();
            JKCraftLauncher launcher = new JKCraftLauncher(paths[0], paths[1]);
            if (args.length == 1 && args[0].equals("--check")) {
                launcher.checkOnly();
                return;
            }
            if (args.length != 0) {
                throw new IllegalArgumentException("Usage: java -jar JKCraft-OfflineLauncher.jar [--check]");
            }
            SwingUtilities.invokeLater(launcher::show);
        } catch (Exception error) {
            System.err.println("JKCraft launcher: " + error.getMessage());
            if (!java.awt.GraphicsEnvironment.isHeadless()) {
                JOptionPane.showMessageDialog(null, error.getMessage(), "JKCraft", JOptionPane.ERROR_MESSAGE);
            }
            System.exit(1);
        }
    }

    private String defaultAcademy() {
        return packageRoot.resolve("GameResources/JediAcademy/GameData").toString();
    }

    private String defaultMinecraft() {
        return packageRoot.resolve("GameResources/Minecraft/GameData").toString();
    }

    private void show() {
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            Process process;
            synchronized (lifecycleLock) {
                exiting = true;
                process = gameProcess;
            }
            stopOwnedProcesses(process);
        }, "JKCraft-launcher-shutdown"));
        frame = new JFrame("JKCraft • Offline Launcher");
        frame.setDefaultCloseOperation(WindowConstants.EXIT_ON_CLOSE);
        frame.setResizable(false);
        frame.setContentPane(new Skin());
        frame.setSize(new Dimension(760, 430));
        frame.setLocationRelativeTo(null);
        frame.setVisible(true);
    }

    private final class Skin extends JPanel {
        Skin() {
            setLayout(null);
            setBackground(BACKGROUND);
            add(label("JK", 36, 20, 38, GREEN, Font.BOLD));
            add(label("CRAFT", 85, 20, 38, TEXT, Font.BOLD));
            add(label("OFFLINE  /  SINGLE PLAYER", 38, 70, 12, GOLD, Font.BOLD));
            add(label("JEDI ACADEMY  ×  MINECRAFT", 414, 32, 11, MUTED, Font.BOLD));
            languageButton = button("RUS / EN", 615, 23, 107, PANEL, GOLD, () -> toggleLanguage());
            languageButton.setBounds(615, 23, 107, 34);
            add(languageButton);

            nicknameLabel = label("", 38, 132, 12, MUTED, Font.BOLD);
            add(nicknameLabel);
            nicknameField = field(settings.getProperty("nickname", "Jaden"), 38, 157, 684);
            add(nicknameField);
            nicknameHint = label("", 39, 197, 11, MUTED, Font.PLAIN);
            add(nicknameHint);

            launchButton = button("", 38, 236, 316, GREEN, BACKGROUND,
                () -> { if (RUNNING.get()) stopGame(); else launch(); });
            launchButton.setFont(new Font("Segoe UI", Font.BOLD, 17));
            add(launchButton);
            settingsButton = button("", 366, 236, 112, PANEL, TEXT, () -> openSettings());
            add(settingsButton);
            foldersButton = button("", 490, 236, 108, PANEL, TEXT, () -> openDirectory(packageRoot.resolve("GameResources")));
            add(foldersButton);
            guideButton = button("", 610, 236, 112, PANEL, TEXT, () -> openFile(packageRoot.resolve(
                russian ? "INSTALLATION_RU.txt" : "INSTALLATION_EN.txt")));
            add(guideButton);

            status = label("", 38, 291, 12, MUTED, Font.PLAIN);
            status.setSize(690, 28);
            add(status);
            progressBar = new JProgressBar();
            progressBar.setBounds(38, 326, 684, 14);
            progressBar.setForeground(GREEN);
            progressBar.setBackground(PANEL);
            progressBar.setBorder(BorderFactory.createLineBorder(new Color(66, 91, 88), 1));
            progressBar.setVisible(false);
            add(progressBar);
            progressDetail = label("", 38, 345, 11, MUTED, Font.PLAIN);
            progressDetail.setVisible(false);
            add(progressDetail);
            updateLanguage();
        }

        @Override protected void paintComponent(Graphics raw) {
            super.paintComponent(raw);
            Graphics2D g = (Graphics2D) raw.create();
            g.setPaint(new GradientPaint(0, 0, BACKGROUND, 760, 430, new Color(20, 32, 36)));
            g.fillRect(0, 0, getWidth(), getHeight());
            g.setColor(new Color(40, 65, 62));
            for (int x = 0; x < 760; x += 38) g.fillRect(x, 93, 37, 2);
            g.setColor(GOLD);
            g.fillRect(38, 91, 114, 4);
            g.setStroke(new BasicStroke(2f));
            g.setColor(new Color(49, 71, 72));
            g.drawRect(24, 107, 710, 268);
            g.setColor(new Color(29, 55, 50));
            for (int i = 0; i < 7; i++) {
                int x = 636 + (i % 3) * 28;
                int y = 17 + (i / 3) * 21;
                g.fillRect(x, y, 19, 12);
            }
            g.dispose();
        }
    }

    private String tr(String ru, String en) {
        return russian ? ru : en;
    }

    private void updateLanguage() {
        nicknameLabel.setText(tr("ИМЯ ИГРОКА", "PLAYER NAME"));
        nicknameHint.setText(tr("3–16 латинских букв, цифр или _", "3–16 Latin letters, digits or _"));
        updateLaunchButton();
        settingsButton.setText(tr("НАСТРОЙКИ", "SETTINGS"));
        foldersButton.setText(tr("РЕСУРСЫ", "ASSETS"));
        guideButton.setText(tr("ГАЙД", "GUIDE"));
        languageButton.setText(russian ? "RUS / en" : "rus / EN");
        if (RUNNING.get()) {
            status.setForeground(GREEN);
            updateProgressText();
        } else {
            status.setForeground(MUTED);
            status.setText(tr("Локальный запуск без авторизации. Игровые ресурсы не включены.",
                "Local launch without sign-in. Game assets are not included."));
        }
    }

    private void updateLaunchButton() {
        if (launchButton == null) return;
        boolean running = RUNNING.get();
        launchButton.setText(running ? tr("ЗАКРЫТЬ", "CLOSE") : tr("НАЧАТЬ ИГРУ", "PLAY"));
        launchButton.setBackground(running ? RED : GREEN);
        launchButton.setForeground(running ? Color.WHITE : BACKGROUND);
    }

    private void updateProgressText() {
        switch (currentStage) {
            case "PREPARING":
                status.setText(tr("Подготовка Minecraft, Fabric и библиотек...",
                    "Preparing Minecraft, Fabric and libraries..."));
                break;
            case "OPENJK":
                status.setText(tr("Запуск Jedi Academy через OpenJK...",
                    "Starting Jedi Academy through OpenJK..."));
                break;
            case "MINECRAFT":
                status.setText(tr("Загрузка Minecraft 26.3. OpenJK ожидает готовности...",
                    "Loading Minecraft 26.3. OpenJK is waiting for readiness..."));
                break;
            case "RUNNING":
                status.setText(tr("JKCraft запущен.", "JKCraft is running."));
                break;
            default:
                status.setText(tr("Проверка и запуск клиента...", "Checking and starting the client..."));
        }
        if (currentDownload != null && "PREPARING".equals(currentStage)) {
            progressDetail.setText(tr("Скачивается: ", "Downloading: ") + currentDownload);
        } else if ("RUNNING".equals(currentStage)) {
            progressDetail.setText(tr("Игра отображается в окне OpenJK.", "The game is displayed in the OpenJK window."));
        } else {
            progressDetail.setText(tr("Подробности: UserData/launcher.log", "Details: UserData/launcher.log"));
        }
    }

    private void setStage(String stage) {
        if (!stage.equals("PREPARING") && !stage.equals("OPENJK") &&
            !stage.equals("MINECRAFT") && !stage.equals("RUNNING")) return;
        currentStage = stage;
        SwingUtilities.invokeLater(() -> {
            currentStage = stage;
            currentDownload = null;
            if ("RUNNING".equals(stage)) {
                progressBar.setIndeterminate(false);
                progressBar.setValue(100);
            } else {
                progressBar.setIndeterminate(true);
            }
            updateProgressText();
        });
    }

    private void handleOutput(String line) {
        if (line.startsWith("JKCRAFT_OPENJK_PID:")) {
            try { openJkPid = Long.parseLong(line.substring(19).trim()); }
            catch (NumberFormatException ignored) { }
        }
        int minecraftMarker = line.indexOf("JKCRAFT_MINECRAFT_READY pid=");
        if (minecraftMarker >= 0) {
            try {
                minecraftPid = Long.parseLong(line.substring(minecraftMarker +
                    "JKCRAFT_MINECRAFT_READY pid=".length()).trim().split("\\s+", 2)[0]);
            } catch (NumberFormatException ignored) { }
        }
        if (line.startsWith("JKCRAFT_STAGE:")) {
            setStage(line.substring("JKCRAFT_STAGE:".length()).trim());
            return;
        }
        if (line.contains("JKCraft: linked to Jedi Academy") ||
            line.contains("JKCraft: Jedi Academy link up")) {
            setStage("RUNNING");
            return;
        }
        if (!"PREPARING".equals(currentStage)) return;
        int marker = line.indexOf("Downloading ");
        if (marker < 0) marker = line.indexOf("Download ");
        if (marker < 0) return;
        String name = line.substring(marker).replaceFirst("^Download(?:ing)?\\s+", "").trim();
        int query = name.indexOf('?');
        if (query >= 0) name = name.substring(0, query);
        name = name.replace('\\', '/');
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (name.length() > 75) name = name.substring(0, 72) + "...";
        if (name.isEmpty()) return;
        final String download = name;
        SwingUtilities.invokeLater(() -> {
            if (!"PREPARING".equals(currentStage)) return;
            currentDownload = download;
            updateProgressText();
        });
    }

    private void toggleLanguage() {
        russian = !russian;
        settings.setProperty("language", russian ? "RUS" : "EN");
        try {
            Files.createDirectories(settingsFile.getParent());
            try (OutputStream out = Files.newOutputStream(settingsFile)) {
                settings.store(out, "JKCraft launcher settings");
            }
        } catch (IOException error) {
            // The language still changes for this session if settings cannot be saved.
        }
        updateLanguage();
    }

    private void openSettings() {
        String current = settings.getProperty("resolution", "1280x720");
        String[] choices = {"1280x720", "1600x900", "1920x1080", "2560x1440",
            "3440x1440", "3840x2160", tr("Другое...", "Custom...")};
        JComboBox<String> resolution = new JComboBox<>(choices);
        resolution.setEditable(false);
        resolution.setSelectedItem(current);
        if (!current.equals(resolution.getSelectedItem())) {
            resolution.insertItemAt(current, 0);
            resolution.setSelectedItem(current);
        }
        JCheckBox fullscreen = new JCheckBox(tr("Полный экран", "Fullscreen"),
            "1".equals(settings.getProperty("fullscreen", "0")));
        JCheckBox consoleLogs = new JCheckBox(
            tr("Логи JKCraft в консоли Jedi Academy", "JKCraft logs in the Jedi Academy console"),
            "1".equals(settings.getProperty("consoleLogs", "0")));
        int result = JOptionPane.showConfirmDialog(null,
            new Object[] {tr("Разрешение OpenJK:", "OpenJK resolution:"), resolution,
                fullscreen, consoleLogs},
            tr("Настройки JKCraft", "JKCraft settings"), JOptionPane.OK_CANCEL_OPTION);
        if (result != JOptionPane.OK_OPTION) return;
        String selected = (String) resolution.getSelectedItem();
        if (selected != null && selected.equals(choices[choices.length - 1])) {
            selected = JOptionPane.showInputDialog(null,
                tr("Введите ширину и высоту, например 1920x1080:",
                    "Enter width and height, for example 1920x1080:"), current);
            if (selected == null) return;
        }
        if (selected == null || !selected.matches("[0-9]{3,4}x[0-9]{3,4}")) {
            JOptionPane.showMessageDialog(null, tr("Неверный формат разрешения.",
                "Invalid resolution format."), "JKCraft", JOptionPane.ERROR_MESSAGE);
            return;
        }
        String[] dimensions = selected.split("x");
        int width = Integer.parseInt(dimensions[0]);
        int height = Integer.parseInt(dimensions[1]);
        if (width < 640 || width > 3840 || height < 480 || height > 2160) {
            JOptionPane.showMessageDialog(null, tr("Допустимый диапазон: 640x480–3840x2160.",
                "Allowed range: 640x480–3840x2160."), "JKCraft", JOptionPane.ERROR_MESSAGE);
            return;
        }
        settings.setProperty("resolution", selected);
        settings.setProperty("fullscreen", fullscreen.isSelected() ? "1" : "0");
        settings.setProperty("consoleLogs", consoleLogs.isSelected() ? "1" : "0");
        try {
            Files.createDirectories(settingsFile.getParent());
            try (OutputStream out = Files.newOutputStream(settingsFile)) {
                settings.store(out, "JKCraft launcher settings");
            }
        } catch (IOException error) {
            JOptionPane.showMessageDialog(null, error.getMessage(), "JKCraft", JOptionPane.ERROR_MESSAGE);
        }
    }

    private static JLabel label(String text, int x, int y, int size, Color color, int style) {
        JLabel result = new JLabel(text);
        result.setFont(new Font("Segoe UI", style, size));
        result.setForeground(color);
        result.setBounds(x, y, 690, Math.max(25, size + 9));
        return result;
    }

    private static JTextField field(String text, int x, int y, int width) {
        JTextField result = new JTextField(text);
        result.setBounds(x, y, width, 36);
        result.setFont(new Font("Segoe UI", Font.PLAIN, 15));
        result.setForeground(TEXT);
        result.setCaretColor(GREEN);
        result.setBackground(PANEL);
        result.setBorder(BorderFactory.createCompoundBorder(
            BorderFactory.createLineBorder(new Color(66, 91, 88), 1),
            BorderFactory.createEmptyBorder(3, 10, 3, 10)));
        return result;
    }

    private static JButton button(String text, int x, int y, int width, Color background,
                                  Color foreground, Runnable action) {
        JButton result = new JButton(text);
        result.setBounds(x, y, width, 44);
        result.setFont(new Font("Segoe UI", Font.BOLD, 12));
        result.setBackground(background);
        result.setForeground(foreground);
        result.setFocusPainted(false);
        result.setBorder(BorderFactory.createLineBorder(new Color(76, 113, 95), 2));
        result.addActionListener(event -> action.run());
        return result;
    }

    private static void openDirectory(Path directory) {
        try {
            Files.createDirectories(directory);
            Desktop.getDesktop().open(directory.toFile());
        } catch (IOException error) {
            JOptionPane.showMessageDialog(null, error.getMessage(), "JKCraft", JOptionPane.ERROR_MESSAGE);
        }
    }

    private static void openFile(Path file) {
        try {
            Desktop.getDesktop().open(file.toFile());
        } catch (IOException error) {
            JOptionPane.showMessageDialog(null, error.getMessage(), "JKCraft", JOptionPane.ERROR_MESSAGE);
        }
    }

    private List<String> command(boolean checkOnly) {
        String nickname = nicknameField == null ? settings.getProperty("nickname", "Jaden")
            : nicknameField.getText().trim();
        String academy = defaultAcademy();
        String minecraft = defaultMinecraft();
        if (!nickname.matches("[A-Za-z0-9_]{3,16}")) {
            throw new IllegalArgumentException(tr("Ник: 3–16 латинских букв, цифр или _.",
                "Name: 3–16 Latin letters, digits or _."));
        }
        if (!Files.isRegularFile(Paths.get(academy, "base", "assets0.pk3"))) {
            throw new IllegalArgumentException(tr("Не найден Jedi Academy GameData/base/assets0.pk3.",
                "Jedi Academy GameData/base/assets0.pk3 was not found."));
        }
        boolean runtimeReady = Files.isRegularFile(packageRoot.resolve("ClientCore/gradlew.bat")) &&
            Files.isRegularFile(packageRoot.resolve("Runtime/Java25/bin/java.exe")) &&
            Files.isRegularFile(packageRoot.resolve("OpenJK/openjk_sp.x86.exe")) &&
            Files.isRegularFile(packageRoot.resolve("mods/jkcraft-0.1.2.jar"));
        if (!runtimeReady) {
            throw new IllegalArgumentException(tr("Локальный Minecraft-клиент не подготовлен.",
                "The local Minecraft client is not prepared."));
        }
        Path gameDir = Paths.get(minecraft).toAbsolutePath().normalize();
        try {
            Files.createDirectories(gameDir);
            Files.createDirectories(settingsFile.getParent());
        } catch (IOException error) {
            throw new IllegalArgumentException(tr("Не удалось открыть папку Minecraft: ",
                "Could not open the Minecraft folder: ") + error.getMessage());
        }
        settings.setProperty("nickname", nickname);
        settings.remove("academy");
        settings.remove("minecraft");
        settings.remove("academyPathVersion");
        if (!checkOnly) {
            try (OutputStream out = Files.newOutputStream(settingsFile)) {
                settings.store(out, "JKCraft offline launcher settings");
            } catch (IOException error) {
                throw new IllegalArgumentException(tr("Не удалось сохранить настройки: ",
                    "Could not save settings: ") + error.getMessage());
            }
        }
        List<String> args = new ArrayList<>();
        args.add("powershell.exe");
        args.add("-NoProfile");
        args.add("-ExecutionPolicy");
        args.add("Bypass");
        args.add("-File");
        args.add(packageRoot.resolve("Launch-JKCraft-Offline.ps1").toString());
        args.add("-Nickname");
        args.add(nickname);
        String savedResolution = settings.getProperty("resolution", "1280x720");
        if (!savedResolution.matches("[0-9]{3,4}x[0-9]{3,4}")) savedResolution = "1280x720";
        String[] resolution = savedResolution.split("x");
        args.add("-Width");
        args.add(resolution[0]);
        args.add("-Height");
        args.add(resolution[1]);
        args.add("-Fullscreen");
        args.add(settings.getProperty("fullscreen", "0"));
        args.add("-ConsoleLogs");
        args.add(settings.getProperty("consoleLogs", "0"));
        if (checkOnly) args.add("-CheckOnly");
        return args;
    }

    private void checkOnly() throws IOException, InterruptedException {
        Process process = new ProcessBuilder(command(true))
            .directory(workspace.toFile()).inheritIO().start();
        int result = process.waitFor();
        if (result != 0) throw new IllegalStateException("Offline checks failed (exit " + result + ").");
    }

    private void launch() {
        if (!RUNNING.compareAndSet(false, true)) return;
        final List<String> args;
        try {
            args = command(false);
            Files.deleteIfExists(packageRoot.resolve("UserData/stop-request.txt"));
            minecraftPid = 0;
            openJkPid = 0;
            stopping.set(false);
            launchButton.setEnabled(false);
            updateLaunchButton();
            status.setForeground(GREEN);
            currentStage = "CHECKING";
            currentDownload = null;
            progressBar.setValue(0);
            progressBar.setIndeterminate(true);
            progressBar.setVisible(true);
            progressDetail.setVisible(true);
            updateProgressText();
        } catch (Exception error) {
            RUNNING.set(false);
            updateLaunchButton();
            JOptionPane.showMessageDialog(null, error.getMessage(), "JKCraft", JOptionPane.ERROR_MESSAGE);
            return;
        }
        Thread worker = new Thread(() -> {
            try {
                Path log = packageRoot.resolve("UserData/launcher.log");
                Process process;
                synchronized (lifecycleLock) {
                    if (exiting) return;
                    process = new ProcessBuilder(args).directory(workspace.toFile())
                        .redirectErrorStream(true).start();
                    gameProcess = process;
                }
                SwingUtilities.invokeLater(() -> launchButton.setEnabled(true));
                try (BufferedReader output = new BufferedReader(new InputStreamReader(
                         process.getInputStream(), StandardCharsets.UTF_8));
                     BufferedWriter logWriter = Files.newBufferedWriter(log, StandardCharsets.UTF_8)) {
                    String line;
                    while ((line = output.readLine()) != null) {
                        logWriter.write(line);
                        logWriter.newLine();
                        logWriter.flush();
                        handleOutput(line);
                    }
                }
                int result = process.waitFor();
                synchronized (lifecycleLock) {
                    if (gameProcess == process) gameProcess = null;
                }
                SwingUtilities.invokeLater(() -> {
                    boolean wasStopped = stopping.getAndSet(false);
                    progressBar.setIndeterminate(false);
                    progressBar.setValue(result == 0 || wasStopped ? 100 : 0);
                    progressDetail.setText(result == 0 || wasStopped ? tr("Сеанс завершён.", "Session ended.")
                        : tr("Подробности: UserData/launcher.log", "Details: UserData/launcher.log"));
                    status.setForeground(result == 0 || wasStopped ? GREEN : GOLD);
                    status.setText(result == 0 || wasStopped ? tr("Игра закрыта. Можно запустить снова.",
                        "Game closed. You can launch again.")
                        : tr("Ошибка запуска ", "Launch failed: ") + result +
                        tr(". См. UserData/launcher.log", ". See UserData/launcher.log"));
                    RUNNING.set(false);
                    updateLaunchButton();
                    launchButton.setEnabled(true);
                });
            } catch (Exception error) {
                synchronized (lifecycleLock) { gameProcess = null; }
                SwingUtilities.invokeLater(() -> {
                    progressBar.setIndeterminate(false);
                    progressBar.setValue(0);
                    progressDetail.setText(tr("Подробности: UserData/launcher.log", "Details: UserData/launcher.log"));
                    boolean wasStopped = stopping.getAndSet(false);
                    status.setForeground(wasStopped ? GREEN : GOLD);
                    status.setText(wasStopped ? tr("Игра закрыта. Можно запустить снова.",
                        "Game closed. You can launch again.") :
                        tr("Ошибка запуска: ", "Launch error: ") + error.getMessage());
                    RUNNING.set(false);
                    updateLaunchButton();
                    launchButton.setEnabled(true);
                });
            }
        }, "JKCraft-game-launch");
        worker.setDaemon(true);
        worker.start();
    }

    private void stopGame() {
        if (!RUNNING.get() || !stopping.compareAndSet(false, true)) return;
        launchButton.setEnabled(false);
        status.setText(tr("Завершение игры...", "Closing the game..."));
        Thread worker = new Thread(() -> stopOwnedProcesses(gameProcess), "JKCraft-game-stop");
        worker.setDaemon(true);
        worker.start();
    }

    private void stopOwnedProcesses(Process process) {
        if (process == null) return;
        try {
            Files.write(packageRoot.resolve("UserData/stop-request.txt"), new byte[] {1});
        } catch (IOException ignored) { }
        boolean ended = false;
        try {
            ended = process.waitFor("RUNNING".equals(currentStage) ? 25 : 5, TimeUnit.SECONDS);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
        }
        if (!ended) {
            // Preparation may still be inside Gradle. Kill only this launcher's
            // PowerShell process tree, never every java.exe on the computer.
            try {
                Process killer = new ProcessBuilder("taskkill.exe", "/PID",
                    Long.toString(process.pid()), "/T", "/F")
                    .redirectErrorStream(true).redirectOutput(ProcessBuilder.Redirect.DISCARD).start();
                killer.waitFor(5, TimeUnit.SECONDS);
            } catch (Exception ignored) { }
            if (process.isAlive()) process.destroyForcibly();
        }
        stopExactChild(openJkPid, packageRoot.resolve("OpenJK/openjk_sp.x86.exe"));
        stopExactChild(minecraftPid, packageRoot.resolve("Runtime/Java25/bin/java.exe"));
    }

    private static void stopExactChild(long pid, Path expected) {
        if (pid <= 0) return;
        ProcessHandle.of(pid).ifPresent(handle -> {
            String command = handle.info().command().orElse("");
            if (command.equalsIgnoreCase(expected.toAbsolutePath().toString())) {
                handle.destroyForcibly();
            }
        });
    }
}
