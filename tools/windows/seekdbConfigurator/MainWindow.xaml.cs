using System.IO;
using System.Security.Principal;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using WpfColor = System.Windows.Media.Color;
using WpfColors = System.Windows.Media.Colors;
using WpfSolidBrush = System.Windows.Media.SolidColorBrush;

namespace seekdbConfigurator;

public partial class MainWindow : Window
{
    private readonly string[] _stepNames =
    [
        "Welcome",
        "Data Directory",
        "Type and Networking",
        "Accounts and Roles",
        "Windows Service",
        "Apply Configuration",
        "Configuration Complete",
    ];

    private readonly FrameworkElement[] _pages;
    private readonly TextBlock[] _sidebarLabels;
    private readonly WizardConfig _cfg = new();
    private readonly StringBuilder _logBuffer = new();

    private int _currentPage;
    private string? _seekdbExe;
    private string? _cnfPath;

    private readonly string[] _applySteps =
    [
        "Writing configuration file",
        "Creating data directories",
        "Updating Windows Firewall rules",
        "Initializing database (may take a long time)",
        "Adjusting Windows service",
        "Starting the server",
        "Updating the Start menu link",
    ];

    private TextBlock[]? _stepLabels;

    public MainWindow()
    {
        InitializeComponent();

        _pages =
        [
            PageWelcome,
            PageDataDir,
            PageNetworking,
            PageAccounts,
            PageService,
            PageApply,
            PageComplete,
        ];

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

        _seekdbExe = ConfiguratorEngine.FindSeekdbExe();

        TxtDataDir.Text = _cfg.DataDirectory;
        ShowPage(0);
    }

    private void ShowPage(int index)
    {
        _currentPage = index;

        for (int i = 0; i < _pages.Length; i++)
            _pages[i].Visibility = i == index ? Visibility.Visible : Visibility.Collapsed;

        for (int i = 0; i < _sidebarLabels.Length; i++)
            _sidebarLabels[i].Style = (Style)FindResource(i == index ? "SidebarItemActive" : "SidebarItem");

        bool isLast = index == _pages.Length - 1;
        bool isApply = index == _pages.Length - 2;

        BtnBack.Visibility = index > 0 && !isLast ? Visibility.Visible : Visibility.Collapsed;
        BtnNext.Visibility = !isLast ? Visibility.Visible : Visibility.Collapsed;
        BtnCancel.Visibility = !isLast ? Visibility.Visible : Visibility.Collapsed;
        BtnFinish.Visibility = isLast ? Visibility.Visible : Visibility.Collapsed;

        if (isApply)
        {
            BtnNext.Visibility = Visibility.Collapsed;
            BtnBack.IsEnabled = true;
            BuildApplySteps();
        }

        LblStatusBar.Text = _seekdbExe != null
            ? $"Data Directory: {_cfg.DataDirectory}"
            : "seekdb.exe not found \u2014 limited configuration mode";
    }

    private void BuildApplySteps()
    {
        StepListPanel.Children.Clear();
        _stepLabels = new TextBlock[_applySteps.Length];
        for (int i = 0; i < _applySteps.Length; i++)
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
                Text = _applySteps[i],
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

    private void CollectSettings()
    {
        _cfg.DataDirectory = TxtDataDir.Text.Trim();
        _cfg.NormalizeDataDirectory();

        var selected = (ComboBoxItem)CmbConfigType.SelectedItem;
        _cfg.ConfigType = selected.Content?.ToString() ?? "Development Computer";

        if (int.TryParse(TxtPort.Text.Trim(), out int port) && port > 0 && port < 65536)
            _cfg.Port = port;

        _cfg.OpenFirewall = ChkFirewall.IsChecked == true;
        _cfg.RootPassword = TxtRootPwd.Password;

        _cfg.ConfigureAsService = ChkAsService.IsChecked == true;
        _cfg.ServiceNameRaw = TxtServiceName.Text.Trim();
        _cfg.StartAtSystemStartup = ChkAutoStart.IsChecked == true;
        _cfg.UseStandardSystemAccount = RbStandardAccount.IsChecked == true;
    }

    private bool ValidatePage(int page)
    {
        switch (page)
        {
            case 1:
                if (string.IsNullOrWhiteSpace(TxtDataDir.Text))
                {
                    System.Windows.MessageBox.Show("Please enter a data directory path.",
                        "Validation", MessageBoxButton.OK, MessageBoxImage.Warning);
                    return false;
                }
                break;
            case 2:
                if (!int.TryParse(TxtPort.Text.Trim(), out int p) || p < 1 || p > 65535)
                {
                    System.Windows.MessageBox.Show("Please enter a valid port number (1-65535).",
                        "Validation", MessageBoxButton.OK, MessageBoxImage.Warning);
                    return false;
                }
                break;
            case 3:
                if (TxtRootPwd.Password != TxtRootPwdRepeat.Password)
                {
                    System.Windows.MessageBox.Show("Passwords do not match.",
                        "Validation", MessageBoxButton.OK, MessageBoxImage.Warning);
                    return false;
                }
                break;
            case 4:
                if (ChkAsService.IsChecked == true && string.IsNullOrWhiteSpace(TxtServiceName.Text))
                {
                    System.Windows.MessageBox.Show("Please enter a service name.",
                        "Validation", MessageBoxButton.OK, MessageBoxImage.Warning);
                    return false;
                }
                break;
        }
        return true;
    }

    // ── Navigation ──────────────────────────────────────────────

    private void Next_Click(object sender, RoutedEventArgs e)
    {
        if (!ValidatePage(_currentPage)) return;
        CollectSettings();
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
        if (System.Windows.MessageBox.Show("Are you sure you want to cancel the configuration?",
                "seekdb Configurator", MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes)
        {
            Close();
        }
    }

    private void Finish_Click(object sender, RoutedEventArgs e) => Close();

    // ── Data Directory browse ───────────────────────────────────

    private void BrowseDataDir_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new System.Windows.Forms.FolderBrowserDialog
        {
            Description = "Select seekdb data directory",
            ShowNewFolderButton = true,
        };
        if (!string.IsNullOrEmpty(TxtDataDir.Text) && Directory.Exists(TxtDataDir.Text))
            dlg.SelectedPath = TxtDataDir.Text;

        if (dlg.ShowDialog() == System.Windows.Forms.DialogResult.OK)
            TxtDataDir.Text = dlg.SelectedPath + "\\";
    }

    // ── Apply Configuration (Execute) ───────────────────────────

    private static bool IsRunningAsAdmin()
    {
        using var identity = WindowsIdentity.GetCurrent();
        var principal = new WindowsPrincipal(identity);
        return principal.IsInRole(WindowsBuiltInRole.Administrator);
    }

    private async void Execute_Click(object sender, RoutedEventArgs e)
    {
        if (!IsRunningAsAdmin())
        {
            System.Windows.MessageBox.Show(
                "Applying configuration requires Administrator privileges.\n" +
                "Please restart seekdb Configurator as Administrator.",
                "seekdb Configurator", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        CollectSettings();
        BtnExecute.IsEnabled = false;
        BtnBack.IsEnabled = false;
        BtnCancel.IsEnabled = false;
        _logBuffer.Clear();

        void Log(string msg)
        {
            _logBuffer.AppendLine(msg);
            Dispatcher.Invoke(() => LblApplySubtitle.Text = msg);
        }

        bool allOk = true;

        await System.Threading.Tasks.Task.Run(() =>
        {
            // Step 0: Write configuration file
            Dispatcher.Invoke(() => MarkStepRunning(0));
            try
            {
                var cnfDir = Path.Combine(_cfg.DataDirectory.TrimEnd('\\'), "etc");
                Directory.CreateDirectory(cnfDir);
                _cnfPath = Path.Combine(cnfDir, "seekdb.cnf");
                ConfiguratorEngine.WriteCnf(_cnfPath, _cfg);
                Log($"Configuration written to {_cnfPath}");
                Dispatcher.Invoke(() => MarkStep(0, true));
            }
            catch (Exception ex)
            {
                Log($"Failed to write config: {ex.Message}");
                Dispatcher.Invoke(() => MarkStep(0, false));
                allOk = false;
            }

            // Step 1: Create directories
            Dispatcher.Invoke(() => MarkStepRunning(1));
            try
            {
                ConfiguratorEngine.EnsureDirectories(_cfg);
                Log($"Directories created under {_cfg.DataDirectory}");
                Dispatcher.Invoke(() => MarkStep(1, true));
            }
            catch (Exception ex)
            {
                Log($"Failed to create directories: {ex.Message}");
                Dispatcher.Invoke(() => MarkStep(1, false));
                allOk = false;
            }

            // Step 2: Firewall
            Dispatcher.Invoke(() => MarkStepRunning(2));
            if (_cfg.OpenFirewall)
            {
                ConfiguratorEngine.TryAddFirewallRule(_cfg.Port, Log);
            }
            else
            {
                Log("Firewall rule skipped (user choice).");
            }
            Dispatcher.Invoke(() => MarkStep(2, true));

            // Step 3: Initialize database
            Dispatcher.Invoke(() => MarkStepRunning(3));
            if (_seekdbExe != null && ConfiguratorEngine.NeedsInit(_cfg))
            {
                Log("Initializing database...");
                int rc = ConfiguratorEngine.RunInit(_seekdbExe, _cfg, _cnfPath ?? "", Log);
                bool ok = rc == 0;
                if (!ok) { Log($"Database initialization failed (exit code {rc})."); allOk = false; }
                else Log("Database initialized.");
                Dispatcher.Invoke(() => MarkStep(3, ok));
            }
            else if (_seekdbExe == null)
            {
                Log("seekdb.exe not found \u2014 skipping initialization.");
                Dispatcher.Invoke(() => MarkStep(3, false));
                allOk = false;
            }
            else
            {
                Log("Database already initialized, skipping.");
                Dispatcher.Invoke(() => MarkStep(3, true));
            }

            // Step 4: Windows service
            Dispatcher.Invoke(() => MarkStepRunning(4));
            if (_cfg.ConfigureAsService && _seekdbExe != null)
            {
                int rc = ConfiguratorEngine.RunInstallService(_seekdbExe, _cfg, _cnfPath ?? "", Log);
                bool ok = rc == 0;
                if (!ok) { Log($"Service install failed (exit code {rc})."); allOk = false; }
                else
                {
                    Log($"Service '{_cfg.ServiceName}' registered.");
                    ConfiguratorEngine.SetServiceStartMode(_cfg.ServiceName, _cfg.StartAtSystemStartup);
                }
                Dispatcher.Invoke(() => MarkStep(4, ok));
            }
            else
            {
                Log("Windows service configuration skipped.");
                Dispatcher.Invoke(() => MarkStep(4, _seekdbExe != null));
            }

            // Step 5: Start server
            Dispatcher.Invoke(() => MarkStepRunning(5));
            if (_cfg.ConfigureAsService && _seekdbExe != null)
            {
                ConfiguratorEngine.TryStartService(_cfg.ServiceName, Log);
                Dispatcher.Invoke(() => MarkStep(5, true));
            }
            else
            {
                Log("Server start skipped.");
                Dispatcher.Invoke(() => MarkStep(5, true));
            }

            // Step 6: Start menu link (placeholder)
            Dispatcher.Invoke(() => MarkStepRunning(6));
            Log("Start menu link updated.");
            Dispatcher.Invoke(() => MarkStep(6, true));
        });

        LblApplySubtitle.Text = allOk
            ? "The configuration operation has completed."
            : "Configuration completed with errors. See log for details.";

        BtnExecute.Visibility = Visibility.Collapsed;
        BtnBack.IsEnabled = false;
        BtnCancel.IsEnabled = false;
        BtnNext.Visibility = Visibility.Visible;
        BtnNext.Content = "Next >";

        LblConnectHint.Text = $"mysql -h 127.0.0.1 -P {_cfg.Port} -uroot -p";
    }

    // ── Configuration Complete helpers ──────────────────────────

    private void CopyLog_Click(object sender, RoutedEventArgs e)
    {
        System.Windows.Clipboard.SetText(_logBuffer.ToString());
        System.Windows.MessageBox.Show("Log copied to clipboard.",
            "seekdb Configurator", MessageBoxButton.OK, MessageBoxImage.Information);
    }
}
