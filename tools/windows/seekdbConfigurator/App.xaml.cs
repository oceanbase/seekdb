using System.Diagnostics;
using System.IO;
using System.Security.Principal;

namespace seekdbConfigurator;

public partial class App : System.Windows.Application
{
    private static readonly string LogPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
        "seekdb", "configurator.log");

    internal static void Log(string message)
    {
        try
        {
            var dir = Path.GetDirectoryName(LogPath);
            if (dir != null) Directory.CreateDirectory(dir);
            File.AppendAllText(LogPath,
                $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] {message}{Environment.NewLine}");
        }
        catch { }
    }

    protected override void OnStartup(System.Windows.StartupEventArgs e)
    {
        base.OnStartup(e);

        DispatcherUnhandledException += (_, ex) =>
        {
            Log($"DispatcherUnhandledException: {ex.Exception}");
            ex.Handled = true;
        };
        AppDomain.CurrentDomain.UnhandledException += (_, ex) =>
            Log($"AppDomain.UnhandledException: {ex.ExceptionObject}");

        var argsStr = string.Join(" ", e.Args);
        Log($"--- OnStartup  args=[{argsStr}]  pid={Environment.ProcessId}  path={Environment.ProcessPath}");

        bool removeMode = e.Args.Any(a =>
            a.Equals("--remove", StringComparison.OrdinalIgnoreCase));

        bool isAdmin = IsRunningAsAdmin();
        Log($"removeMode={removeMode}  isAdmin={isAdmin}");

        if (!isAdmin)
        {
            Log("Not admin, attempting UAC re-launch...");
            try
            {
                var p = Process.Start(new ProcessStartInfo
                {
                    UseShellExecute = true,
                    FileName = Environment.ProcessPath!,
                    Arguments = argsStr,
                    Verb = "runas",
                });
                Log($"Re-launched as admin, child pid={p?.Id}");
            }
            catch (Exception ex)
            {
                Log($"UAC re-launch failed: {ex.Message}");
                if (removeMode) { Shutdown(); return; }
                new MainWindow().Show();
                return;
            }

            Shutdown();
            return;
        }

        try
        {
            System.Windows.Window window;
            if (removeMode)
            {
                Log("Creating RemoveWindow...");
                window = new RemoveWindow();
            }
            else
            {
                Log("Creating MainWindow...");
                window = new MainWindow();
            }
            Log("Calling window.Show()...");
            window.Show();
            Log("Window shown successfully.");
        }
        catch (Exception ex)
        {
            Log($"FATAL: Window creation failed: {ex}");
            System.Windows.MessageBox.Show(
                $"Failed to start seekdb Configurator:\n\n{ex.Message}\n\nSee log: {LogPath}",
                "seekdb Configurator", System.Windows.MessageBoxButton.OK,
                System.Windows.MessageBoxImage.Error);
            Shutdown();
        }
    }

    private static bool IsRunningAsAdmin()
    {
        using var identity = WindowsIdentity.GetCurrent();
        return new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator);
    }
}
