import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.TimeUnit;

/** Windows smoke test: stopping one launcher session also stops its child process. */
public final class LauncherProcessTest {
    public static void main(String[] args) throws Exception {
        Path packageRoot = Files.createTempDirectory("jkcraft-launcher-test");
        Files.createDirectories(packageRoot.resolve("UserData"));
        String command = "$child = Start-Process -FilePath powershell.exe " +
            "-ArgumentList @('-NoProfile','-Command','Start-Sleep -Seconds 120') " +
            "-PassThru; Write-Output ('CHILD:' + $child.Id); Start-Sleep -Seconds 120";
        Process parent = new ProcessBuilder("powershell.exe", "-NoProfile", "-Command", command)
            .redirectErrorStream(true).start();
        long childPid;
        try (BufferedReader output = new BufferedReader(new InputStreamReader(
                 parent.getInputStream(), StandardCharsets.UTF_8))) {
            String line = output.readLine();
            if (line == null || !line.startsWith("CHILD:")) throw new AssertionError(line);
            childPid = Long.parseLong(line.substring(6));
            Constructor<JKCraftLauncher> constructor = JKCraftLauncher.class
                .getDeclaredConstructor(Path.class, Path.class);
            constructor.setAccessible(true);
            JKCraftLauncher launcher = constructor.newInstance(packageRoot, packageRoot);
            Method stop = JKCraftLauncher.class.getDeclaredMethod("stopOwnedProcesses", Process.class);
            stop.setAccessible(true);
            stop.invoke(launcher, parent);
        } finally {
            if (parent.isAlive()) parent.destroyForcibly();
        }
        parent.waitFor(5, TimeUnit.SECONDS);
        if (parent.isAlive() || ProcessHandle.of(childPid).map(ProcessHandle::isAlive).orElse(false)) {
            throw new AssertionError("The launched PowerShell process tree survived shutdown");
        }
        Files.deleteIfExists(packageRoot.resolve("UserData/stop-request.txt"));
        Files.deleteIfExists(packageRoot.resolve("UserData"));
        Files.deleteIfExists(packageRoot);
        System.out.println("Launcher process tree shutdown: OK");
    }
}
