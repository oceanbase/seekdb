using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using WpfColor = System.Windows.Media.Color;
using WpfColors = System.Windows.Media.Colors;
using WpfSolidBrush = System.Windows.Media.SolidColorBrush;

namespace seekdbConfigurator;

public partial class RemoveWindow : Window
{
    private readonly string[] _stepNames =
    [
        "Remove Data",
        "Remove Steps",
        "Removal Complete",
    ];

    private readonly string[] _removeSteps =
    [
        "Stopping the server",
        "Removing the seekdb Windows service",
        "Removing Windows Firewall rules",
        "Removing the server configuration file",
        "Removing the data directory",
    ];

    private readonly FrameworkElement[] _pages;
    private readonly TextBlock[] _sidebarLabels;
    private readonly StringBuilder _logBuffer = new();

    private int _currentPage;
    private string? _seekdbExe;
    private string? _dataDirectory;
    private string? _serviceName;
    private int _port = 2881;

    private TextBlock[]? _stepLabels;

    public RemoveWindow()
    {
        App.Log("RemoveWindow: InitializeComponent...");
        InitializeComponent();

        App.Log("RemoveWindow: Setting up pages and sidebar...");
        _pages = [PageRemoveData, PageRemoveSteps, PageRemoveComplete];

        _sidebarLabels = new TextBlock[_stepNames.Length];
        for (int i = 0; i < _stepNames.Length; i++)
        {
            var tb = new TextBlock
            {
                Text = _stepNames[i],
                Style = (Style)FindResource("SidebarItem"),
            };
            _sidebarLabels[i] = tb;
            SidebarPanel.Children.Add(tb);
        }

        App.Log("RemoveWindow: Finding seekdb.exe...");
        _seekdbExe = ConfiguratorEngine.FindSeekdbExe();
        App.Log($"RemoveWindow: seekdbExe={_seekdbExe ?? "(null)"}");

        App.Log("RemoveWindow: Detecting existing installation...");
        DetectExistingInstallation();
        App.Log($"RemoveWindow: dataDir={_dataDirectory ?? "(null)"}  service={_serviceName ?? "(null)"}  port={_port}");

        LblDataDirPath.Text = _dataDirectory ?? "(no data directory detected)";
        UpdateStatusBar();
        ShowPage(0);
        App.Log("RemoveWindow: Constructor complete.");
    }

    private void DetectExistingInstallation()
    {
        var (regDataDir, regServiceName, regPort) = ConfiguratorEngine.LoadInstallInfo();
        _dataDirectory = regDataDir;
        _serviceName = regServiceName;
        _port = regPort;

        if (string.IsNullOrEmpty(_serviceName))
            _serviceName = ConfiguratorEngine.FindSeekdbServiceName();

        if (string.IsNullOrEmpty(_dataDirectory))
        {
            var defaultDir = @"C:\ProgramData\seekdb\";
            if (Directory.Exists(defaultDir))
                _dataDirectory = defaultDir;
        }
    }

    private void UpdateStatusBar()
    {
        LblStatusBar.Text = _dataDirectory != null
            ? $"Remove    seekdb Server    Data Directory: {_dataDirectory}"
            : "Remove    seekdb Server";
    }

    // ── Page navigation ─────────────────────────────────────────

    private void ShowPage(int index)
    {
        _currentPage = index;

        for (int i = 0; i < _pages.Length; i++)
            _pages[i].Visibility = i == index ? Visibility.Visible : Visibility.Collapsed;

        for (int i = 0; i < _sidebarLabels.Length; i++)
            _sidebarLabels[i].Style = (Style)FindResource(i == index ? "SidebarItemActive" : "SidebarItem");

        bool isLast = index == _pages.Length - 1;

        BtnBack.Visibility = index == 1 ? Visibility.Visible : Visibility.Collapsed;
        BtnNext.Visibility = index == 0 ? Visibility.Visible : Visibility.Collapsed;
        BtnCancel.Visibility = !isLast ? Visibility.Visible : Visibility.Collapsed;
        BtnFinish.Visibility = isLast ? Visibility.Visible : Visibility.Collapsed;

        if (index == 1)
            BuildRemoveSteps();
    }

    private void BuildRemoveSteps()
    {
        StepListPanel.Children.Clear();
        _stepLabels = new TextBlock[_removeSteps.Length];
        for (int i = 0; i < _removeSteps.Length; i++)
        {
            var sp = new StackPanel
            {
                Orientation = System.Windows.Controls.Orientation.Horizontal,
                Margin = new Thickness(0, 4, 0, 4),
            };
            var icon = new TextBlock
            {
                Text = "\u25CB",
                Width = 24,
                FontSize = 14,
                VerticalAlignment = VerticalAlignment.Center,
            };
            var label = new TextBlock
            {
                Text = _removeSteps[i],
                FontSize = 13,
                VerticalAlignment = VerticalAlignment.Center,
            };
            _stepLabels[i] = icon;
            sp.Children.Add(icon);
            sp.Children.Add(label);
            StepListPanel.Children.Add(sp);
        }
    }

    private void MarkStep(int index, bool success)
    {
        if (_stepLabels == null || index >= _stepLabels.Length) return;
        _stepLabels[index].Text = success ? "\u2714" : "\u2718";
        _stepLabels[index].Foreground = success
            ? new WpfSolidBrush(WpfColor.FromRgb(0x2E, 0x7D, 0x32))
            : new WpfSolidBrush(WpfColors.Red);
    }

    private void MarkStepRunning(int index)
    {
        if (_stepLabels == null || index >= _stepLabels.Length) return;
        _stepLabels[index].Text = "\u25CF";
        _stepLabels[index].Foreground = new WpfSolidBrush(WpfColor.FromRgb(0x2B, 0x57, 0x97));
    }

    private void MarkStepSkipped(int index)
    {
        if (_stepLabels == null || index >= _stepLabels.Length) return;
        _stepLabels[index].Text = "\u2014";
        _stepLabels[index].Foreground = new WpfSolidBrush(WpfColors.Gray);
    }

    // ── Navigation handlers ─────────────────────────────────────

    private void Next_Click(object sender, RoutedEventArgs e)
    {
        if (_currentPage < _pages.Length - 1)
            ShowPage(_currentPage + 1);
    }

    private void Back_Click(object sender, RoutedEventArgs e)
    {
        if (_currentPage > 0)
            ShowPage(_currentPage - 1);
    }

    private void Cancel_Click(object sender, RoutedEventArgs e)
    {
        if (System.Windows.MessageBox.Show(
                "Are you sure you want to cancel the removal?",
                "seekdb Configurator", MessageBoxButton.YesNo, MessageBoxImage.Question)
            == MessageBoxResult.Yes)
        {
            Close();
        }
    }

    private void Finish_Click(object sender, RoutedEventArgs e) => Close();

    // ── Execute removal ─────────────────────────────────────────

    private async void Execute_Click(object sender, RoutedEventArgs e)
    {
        BtnExecute.IsEnabled = false;
        BtnBack.IsEnabled = false;
        BtnCancel.IsEnabled = false;
        _logBuffer.Clear();

        bool removeData = ChkRemoveData.IsChecked == true;
        bool allOk = true;

        void Log(string msg)
        {
            _logBuffer.AppendLine(msg);
            Dispatcher.Invoke(() => LblRemoveSubtitle.Text = msg);
        }

        string? svcName = _serviceName;
        string? dataDir = _dataDirectory;
        string? exe = _seekdbExe;
        int port = _port;

        await System.Threading.Tasks.Task.Run(() =>
        {
            bool hasService = !string.IsNullOrEmpty(svcName) && ConfiguratorEngine.ServiceExists(svcName!);

            // Step 0: Stop the server
            Dispatcher.Invoke(() => MarkStepRunning(0));
            if (hasService)
            {
                bool ok = ConfiguratorEngine.TryStopService(svcName!, Log);
                Dispatcher.Invoke(() => MarkStep(0, ok));
                if (!ok) allOk = false;
            }
            else
            {
                Log("No seekdb service found, skipping stop.");
                Dispatcher.Invoke(() => MarkStepSkipped(0));
            }

            // Step 1: Remove the Windows service
            Dispatcher.Invoke(() => MarkStepRunning(1));
            if (hasService)
            {
                bool ok = ConfiguratorEngine.TryRemoveService(exe, svcName!, Log);
                Dispatcher.Invoke(() => MarkStep(1, ok));
                if (!ok) allOk = false;
            }
            else
            {
                Log("No seekdb service found, skipping removal.");
                Dispatcher.Invoke(() => MarkStepSkipped(1));
            }

            // Step 2: Remove Windows Firewall rules
            Dispatcher.Invoke(() => MarkStepRunning(2));
            bool fwOk = ConfiguratorEngine.TryRemoveFirewallRules(port, Log);
            Dispatcher.Invoke(() => MarkStep(2, fwOk));

            // Step 3: Remove the server configuration file
            Dispatcher.Invoke(() => MarkStepRunning(3));
            if (!string.IsNullOrEmpty(dataDir))
            {
                bool ok = ConfiguratorEngine.TryRemoveConfigFile(dataDir!, Log);
                Dispatcher.Invoke(() => MarkStep(3, ok));
                if (!ok) allOk = false;
            }
            else
            {
                Log("No data directory known, skipping config file removal.");
                Dispatcher.Invoke(() => MarkStepSkipped(3));
            }

            // Step 4: Remove the data directory
            Dispatcher.Invoke(() => MarkStepRunning(4));
            if (removeData && !string.IsNullOrEmpty(dataDir))
            {
                bool ok = ConfiguratorEngine.TryRemoveDataDirectory(dataDir!, Log);
                Dispatcher.Invoke(() => MarkStep(4, ok));
                if (!ok) allOk = false;
            }
            else
            {
                Log(removeData ? "No data directory known." : "Data directory removal skipped (user choice).");
                Dispatcher.Invoke(() => MarkStepSkipped(4));
            }

            // Clean up registry entries
            ConfiguratorEngine.CleanRegistry();
            Log("Registry entries cleaned.");
        });

        LblRemoveSubtitle.Text = allOk
            ? "The configuration removal operation was successful."
            : "Removal completed with errors. See log for details.";

        LblCompleteSubtitle.Text = allOk
            ? "The configuration removal operation was successful.\nClick Finish to continue."
            : "Removal completed with errors.\nClick Finish to continue.";

        BtnExecute.Visibility = Visibility.Collapsed;
        BtnBack.IsEnabled = false;
        BtnCancel.IsEnabled = false;
        BtnNext.Visibility = Visibility.Visible;
        BtnNext.Content = "Next >";
    }

    // ── Helpers ──────────────────────────────────────────────────

    private void CopyLog_Click(object sender, RoutedEventArgs e)
    {
        System.Windows.Clipboard.SetText(_logBuffer.ToString());
        System.Windows.MessageBox.Show("Log copied to clipboard.",
            "seekdb Configurator", MessageBoxButton.OK, MessageBoxImage.Information);
    }
}
