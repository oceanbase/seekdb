namespace seekdbConfigurator;

/// <summary>User choices collected across wizard steps (similar in spirit to MySQL Configurator).</summary>
public sealed class WizardConfig
{
    public string DataDirectory { get; set; } = @"C:\ProgramData\seekdb\";

    /// <summary>Development Computer | Server Computer | Dedicated Computer</summary>
    public string ConfigType { get; set; } = "Development Computer";

    public int Port { get; set; } = 2881;

    public bool OpenFirewall { get; set; } = true;

    public string RootPassword { get; set; } = "";

    public bool ConfigureAsService { get; set; } = true;

    public string ServiceName =>
        string.IsNullOrWhiteSpace(ServiceNameRaw) ? "seekdb" : ServiceNameRaw.Trim();

    public string ServiceNameRaw { get; set; } = "seekdb";

    public bool StartAtSystemStartup { get; set; } = true;

    /// <summary>Use LocalSystem; custom account not implemented in this simple wizard.</summary>
    public bool UseStandardSystemAccount { get; set; } = true;

    public void NormalizeDataDirectory()
    {
        var p = DataDirectory.Trim();
        if (p.Length == 0)
        {
            DataDirectory = @"C:\ProgramData\seekdb\";
            return;
        }
        if (!p.EndsWith('\\') && !p.EndsWith('/'))
            p += "\\";
        DataDirectory = p;
    }

    public string BaseDirForCnf => DataDirectory.TrimEnd('\\', '/').Replace("\\", "/");

    public void ApplyConfigTypePresets()
    {
        (MemoryLimit, CpuCount) = ConfigType switch
        {
            "Server Computer" => ("4G", Math.Max(2, Environment.ProcessorCount / 2)),
            "Dedicated Computer" => ("8G", Math.Max(4, Environment.ProcessorCount)),
            _ => ("2G", Math.Min(4, Math.Max(1, Environment.ProcessorCount)))
        };
    }

    public string MemoryLimit { get; private set; } = "2G";

    public int CpuCount { get; private set; } = 4;
}
