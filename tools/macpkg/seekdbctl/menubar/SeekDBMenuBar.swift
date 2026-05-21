import AppKit

// MARK: - Constants

let SEEKDBCTL = "/opt/homebrew/bin/seekdbctl"
let STATUS_INTERVAL: TimeInterval = 10.0

// MARK: - Status Model

let SEEKDB_CONFIG = "/opt/homebrew/etc/seekdb/seekdb.cnf"
let SEEKDB_BIN = "/opt/homebrew/bin/seekdb"
let DEFAULT_PORT = "2881"

enum ServiceState { case running, stopped, transitioning }

struct SeekDBStatus {
    var port = ""
    var processRunning = false
    var pid = ""
    var portOpen = false

    var state: ServiceState {
        if processRunning && portOpen { return .running }
        if !processRunning && !portOpen { return .stopped }
        return .transitioning
    }

    var summary: String {
        if processRunning { return "Running (PID \(pid))" }
        if portOpen { return "Starting..." }
        return "Stopped"
    }

    static func detect() -> SeekDBStatus {
        var s = SeekDBStatus()
        s.port = readConfigPort()
        let pgrepResult = runCommand(["/usr/bin/pgrep", "-f", SEEKDB_BIN])
        let pids = pgrepResult.output.trimmingCharacters(in: .whitespacesAndNewlines)
        if pgrepResult.exitCode == 0 && !pids.isEmpty {
            s.processRunning = true
            s.pid = pids.components(separatedBy: "\n").first ?? ""
        }
        let ncResult = runCommand(["/usr/bin/nc", "-z", "127.0.0.1", s.port])
        s.portOpen = ncResult.exitCode == 0
        return s
    }
}

func readConfigValue(_ key: String, fallback: String = "") -> String {
    guard let content = try? String(contentsOfFile: SEEKDB_CONFIG, encoding: .utf8) else {
        return fallback
    }
    for line in content.components(separatedBy: "\n") {
        let trimmed = line.trimmingCharacters(in: .whitespaces)
        if trimmed.hasPrefix("#") || trimmed.hasPrefix(";") || trimmed.isEmpty { continue }
        let parts = trimmed.split(separator: "=", maxSplits: 1)
        if parts.count == 2 && parts[0].trimmingCharacters(in: .whitespaces) == key {
            return parts[1].trimmingCharacters(in: .whitespaces)
        }
    }
    return fallback
}

func readConfigPort() -> String {
    return readConfigValue("port", fallback: DEFAULT_PORT)
}

// MARK: - Shell Helpers

func runCommand(_ args: [String]) -> (output: String, exitCode: Int32) {
    let proc = Process()
    proc.executableURL = URL(fileURLWithPath: args[0])
    proc.arguments = Array(args.dropFirst())
    let pipe = Pipe()
    proc.standardOutput = pipe
    proc.standardError = pipe
    do {
        try proc.run()
        proc.waitUntilExit()
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        return (String(data: data, encoding: .utf8) ?? "", proc.terminationStatus)
    } catch {
        return (error.localizedDescription, -1)
    }
}

// MARK: - XPC Helper Protocol

@objc(SeekDBHelperProtocol)
protocol SeekDBHelperProtocol {
    func execute(command: String, args: [String], withReply reply: @escaping (Bool, String) -> Void)
}

func helperProxy() -> SeekDBHelperProtocol? {
    let conn = NSXPCConnection(machServiceName: "com.seekdb.helper", options: .privileged)
    conn.remoteObjectInterface = NSXPCInterface(with: SeekDBHelperProtocol.self)
    conn.resume()
    return conn.remoteObjectProxyWithErrorHandler { error in
        NSLog("XPC error: %@", error.localizedDescription)
    } as? SeekDBHelperProtocol
}

func runPrivileged(command: String, args: [String] = [], completion: @escaping (Bool, String) -> Void) {
    guard let helper = helperProxy() else {
        DispatchQueue.main.async { completion(false, "Cannot connect to helper service") }
        return
    }
    helper.execute(command: command, args: args) { success, output in
        DispatchQueue.main.async { completion(success, output) }
    }
}

func openTerminal(_ command: String) {
    let tmp = NSTemporaryDirectory() + "seekdb_cmd.command"
    let content = "#!/bin/bash\n\(command)\n"
    try? content.write(toFile: tmp, atomically: true, encoding: .utf8)
    chmod(tmp, 0o755)
    NSWorkspace.shared.open(URL(fileURLWithPath: tmp))

    DispatchQueue.global().asyncAfter(deadline: .now() + 3) {
        try? FileManager.default.removeItem(atPath: tmp)
    }
}

func chmod(_ path: String, _ mode: UInt16) {
    Darwin.chmod(path, mode_t(mode))
}

// MARK: - Status Icon

func makeStatusIcon(_ state: ServiceState) -> NSImage {
    let size = NSSize(width: 18, height: 18)
    let image = NSImage(size: size, flipped: false) { rect in
        let color: NSColor
        switch state {
        case .running:       color = .systemGreen
        case .stopped:       color = .systemRed
        case .transitioning: color = .systemYellow
        }
        color.setFill()
        NSBezierPath(ovalIn: rect.insetBy(dx: 4, dy: 4)).fill()

        let attrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 7, weight: .bold),
            .foregroundColor: NSColor.white
        ]
        let text = "S" as NSString
        let textSize = text.size(withAttributes: attrs)
        let textRect = NSRect(
            x: (rect.width - textSize.width) / 2,
            y: (rect.height - textSize.height) / 2,
            width: textSize.width,
            height: textSize.height
        )
        text.draw(in: textRect, withAttributes: attrs)
        return true
    }
    image.isTemplate = false
    return image
}

// MARK: - Settings Window

class SettingsWindowController: NSObject, NSWindowDelegate {
    var window: NSWindow!
    var portField: NSTextField!
    var baseDirField: NSTextField!
    var dataDirField: NSTextField!
    var redoDirField: NSTextField!
    var cpuCountField: NSTextField!
    var memoryField: NSTextField!
    var saveButton: NSButton!
    var bootStartupSwitch: NSButton!
    var statusLabel: NSTextField!
    var onSaved: (() -> Void)?
    private var bootStartupApplying = false

    func showWindow() {
        if window != nil && window.isVisible {
            window.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            loadBootStartupState()
            return
        }

        let w: CGFloat = 520
        let h: CGFloat = 370
        window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: w, height: h),
            styleMask: [.titled, .closable],
            backing: .buffered, defer: false)
        window.title = "SeekDB Settings"
        window.center()
        window.delegate = self
        window.isReleasedWhenClosed = false

        let content = window.contentView!
        let labelW: CGFloat = 110
        let fieldW: CGFloat = 280
        let btnW: CGFloat = 70
        let rowH: CGFloat = 30
        let pad: CGFloat = 16
        var y = h - 50

        func addRow(label: String, value: String, withBrowse: Bool = false) -> NSTextField {
            let lbl = NSTextField(labelWithString: label)
            lbl.frame = NSRect(x: pad, y: y, width: labelW, height: 22)
            lbl.alignment = .right
            content.addSubview(lbl)

            let fw = withBrowse ? fieldW - btnW - 4 : fieldW
            let field = NSTextField(string: value)
            field.frame = NSRect(x: pad + labelW + 8, y: y, width: fw, height: 22)
            field.isEditable = true
            field.isBezeled = true
            field.bezelStyle = .roundedBezel
            content.addSubview(field)

            if withBrowse {
                let btn = NSButton(title: "Browse", target: self, action: #selector(browseDir(_:)))
                btn.frame = NSRect(x: pad + labelW + 8 + fw + 4, y: y - 1, width: btnW, height: 24)
                btn.tag = y.hashValue
                content.addSubview(btn)
                objc_setAssociatedObject(btn, "field", field, .OBJC_ASSOCIATION_RETAIN)
            }

            y -= rowH
            return field
        }

        let bootLbl = NSTextField(labelWithString: "Start at Boot:")
        bootLbl.frame = NSRect(x: pad, y: y, width: labelW, height: 22)
        bootLbl.alignment = .right
        content.addSubview(bootLbl)

        bootStartupSwitch = NSButton(checkboxWithTitle: "Start automatically when macOS boots", target: self, action: #selector(bootStartupToggled))
        bootStartupSwitch.frame = NSRect(x: pad + labelW + 8, y: y - 2, width: fieldW, height: 22)
        bootStartupSwitch.setButtonType(.switch)
        bootStartupSwitch.state = .off
        bootStartupSwitch.isEnabled = false
        content.addSubview(bootStartupSwitch)
        y -= rowH

        portField = addRow(label: "Port:", value: readConfigValue("port", fallback: "2881"))
        baseDirField = addRow(label: "Base Dir:", value: readConfigValue("base-dir", fallback: "/opt/homebrew/var/seekdb/data"), withBrowse: true)
        dataDirField = addRow(label: "Data Dir:", value: readConfigValue("data-dir", fallback: ""), withBrowse: true)
        redoDirField = addRow(label: "Redo Dir:", value: readConfigValue("redo-dir", fallback: ""), withBrowse: true)
        cpuCountField = addRow(label: "CPU Count:", value: readConfigValue("cpu_count", fallback: "4"))
        memoryField = addRow(label: "Memory Limit:", value: readConfigValue("memory_limit", fallback: "2G"))

        y -= 8

        let hint = NSTextField(wrappingLabelWithString: "CPU Count and Memory Limit only take effect during initial setup.")
        hint.frame = NSRect(x: pad + labelW + 8, y: y - 10, width: fieldW, height: 32)
        hint.font = NSFont.systemFont(ofSize: 11)
        hint.textColor = .secondaryLabelColor
        content.addSubview(hint)
        y -= 40

        statusLabel = NSTextField(labelWithString: "")
        statusLabel.frame = NSRect(x: pad, y: pad, width: 300, height: 22)
        statusLabel.textColor = .secondaryLabelColor
        content.addSubview(statusLabel)

        saveButton = NSButton(title: "Save & Restart", target: self, action: #selector(saveSettings))
        saveButton.frame = NSRect(x: w - 140 - pad, y: pad, width: 140, height: 32)
        saveButton.bezelStyle = .rounded
        saveButton.keyEquivalent = "\r"
        content.addSubview(saveButton)

        let cancelButton = NSButton(title: "Cancel", target: self, action: #selector(cancelSettings))
        cancelButton.frame = NSRect(x: w - 140 - pad - 90, y: pad, width: 80, height: 32)
        cancelButton.bezelStyle = .rounded
        cancelButton.keyEquivalent = "\u{1b}"
        content.addSubview(cancelButton)

        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        loadBootStartupState()
    }

    func loadBootStartupState() {
        guard bootStartupSwitch != nil else { return }
        bootStartupSwitch.isEnabled = false
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let result = runCommand([SEEKDBCTL, "boot-status"])
            let status = result.output.trimmingCharacters(in: .whitespacesAndNewlines)
            DispatchQueue.main.async {
                guard let self = self, !self.bootStartupApplying else { return }
                switch status {
                case "enabled":
                    self.bootStartupSwitch.state = .on
                    self.bootStartupSwitch.isEnabled = true
                case "disabled":
                    self.bootStartupSwitch.state = .off
                    self.bootStartupSwitch.isEnabled = true
                default:
                    self.bootStartupSwitch.state = .off
                    self.bootStartupSwitch.isEnabled = false
                }
            }
        }
    }

    @objc func bootStartupToggled(_ sender: NSButton) {
        guard !bootStartupApplying else { return }
        let enable = sender.state == .on
        bootStartupApplying = true
        bootStartupSwitch.isEnabled = false
        statusLabel.stringValue = enable ? "Enabling boot startup..." : "Disabling boot startup..."

        runPrivileged(command: enable ? "enable-boot" : "disable-boot") { [weak self] success, output in
            guard let self = self else { return }
            self.bootStartupApplying = false
            if success {
                self.bootStartupSwitch.state = enable ? .on : .off
                self.bootStartupSwitch.isEnabled = true
                self.statusLabel.stringValue = enable ? "Boot startup enabled." : "Boot startup disabled."
            } else {
                self.bootStartupSwitch.state = enable ? .off : .on
                self.bootStartupSwitch.isEnabled = true
                self.statusLabel.stringValue = "Error: \(output)"
            }
        }
    }

    @objc func browseDir(_ sender: NSButton) {
        guard let field = objc_getAssociatedObject(sender, "field") as? NSTextField else { return }
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.directoryURL = URL(fileURLWithPath: field.stringValue)
        if panel.runModal() == .OK, let url = panel.url {
            field.stringValue = url.path
        }
    }

    @objc func saveSettings() {
        saveButton.isEnabled = false
        statusLabel.stringValue = "Saving..."

        var args: [String] = []
        let port = portField.stringValue.trimmingCharacters(in: .whitespaces)
        let baseDir = baseDirField.stringValue.trimmingCharacters(in: .whitespaces)
        let dataDir = dataDirField.stringValue.trimmingCharacters(in: .whitespaces)
        let redoDir = redoDirField.stringValue.trimmingCharacters(in: .whitespaces)

        if !port.isEmpty { args += ["--port", port] }
        if !baseDir.isEmpty { args += ["--base-dir", baseDir] }
        if !dataDir.isEmpty { args += ["--data-dir", dataDir] }
        if !redoDir.isEmpty { args += ["--redo-dir", redoDir] }
        args += ["--restart"]

        runPrivileged(command: "config", args: args) { [weak self] success, output in
            self?.saveButton.isEnabled = true
            if success {
                self?.statusLabel.stringValue = "Saved and restarted."
                self?.onSaved?()
                DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                    self?.window.close()
                }
            } else {
                self?.statusLabel.stringValue = "Error: \(output)"
            }
        }
    }

    @objc func cancelSettings() {
        window.close()
    }
}

// MARK: - App Delegate

class SeekDBMenuBarApp: NSObject, NSApplicationDelegate {
    var statusItem: NSStatusItem!
    var menu: NSMenu!
    var statusTimer: Timer?
    var currentStatus = SeekDBStatus()
    let settingsController = SettingsWindowController()

    // menu items that update dynamically
    var statusMenuItem: NSMenuItem!
    var portMenuItem: NSMenuItem!
    var startItem: NSMenuItem!
    var stopItem: NSMenuItem!
    var restartItem: NSMenuItem!

    func applicationDidFinishLaunching(_ notification: Notification) {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        statusItem.button?.image = makeStatusIcon(.stopped)

        buildMenu()
        statusItem.menu = menu

        refreshStatus()
        statusTimer = Timer.scheduledTimer(withTimeInterval: STATUS_INTERVAL, repeats: true) { [weak self] _ in
            self?.refreshStatus()
        }
    }

    func buildMenu() {
        menu = NSMenu()

        statusMenuItem = NSMenuItem(title: "SeekDB: Unknown", action: nil, keyEquivalent: "")
        statusMenuItem.isEnabled = false
        menu.addItem(statusMenuItem)

        portMenuItem = NSMenuItem(title: "Port: --", action: nil, keyEquivalent: "")
        portMenuItem.isEnabled = false
        menu.addItem(portMenuItem)

        menu.addItem(.separator())

        startItem = NSMenuItem(title: "Start Service", action: #selector(startService), keyEquivalent: "")
        startItem.target = self
        menu.addItem(startItem)

        stopItem = NSMenuItem(title: "Stop Service", action: #selector(stopService), keyEquivalent: "")
        stopItem.target = self
        menu.addItem(stopItem)

        restartItem = NSMenuItem(title: "Restart Service", action: #selector(restartService), keyEquivalent: "")
        restartItem.target = self
        menu.addItem(restartItem)

        menu.addItem(.separator())

        let logsItem = NSMenuItem(title: "View Logs...", action: #selector(viewLogs), keyEquivalent: "")
        logsItem.target = self
        menu.addItem(logsItem)

        let followItem = NSMenuItem(title: "Follow Logs...", action: #selector(followLogs), keyEquivalent: "")
        followItem.target = self
        menu.addItem(followItem)

        menu.addItem(.separator())

        let settingsItem = NSMenuItem(title: "Settings...", action: #selector(openSettings), keyEquivalent: ",")
        settingsItem.target = self
        menu.addItem(settingsItem)

        let doctorItem = NSMenuItem(title: "Run Doctor", action: #selector(runDoctor), keyEquivalent: "")
        doctorItem.target = self
        menu.addItem(doctorItem)

        menu.addItem(.separator())

        let setupItem = NSMenuItem(title: "Setup Service", action: #selector(setupService), keyEquivalent: "")
        setupItem.target = self
        menu.addItem(setupItem)

        let cleanItem = NSMenuItem(title: "Clean Data...", action: #selector(cleanData), keyEquivalent: "")
        cleanItem.target = self
        menu.addItem(cleanItem)

        let uninstallItem = NSMenuItem(title: "Uninstall...", action: #selector(uninstallService), keyEquivalent: "")
        uninstallItem.target = self
        menu.addItem(uninstallItem)

        menu.addItem(.separator())

        let quitItem = NSMenuItem(title: "Quit SeekDB Monitor", action: #selector(quitApp), keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)
    }

    func refreshStatus() {
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let status = SeekDBStatus.detect()
            DispatchQueue.main.async {
                guard let self = self else { return }
                self.currentStatus = status
                self.statusItem.button?.image = makeStatusIcon(status.state)
                self.statusMenuItem.title = "SeekDB: \(status.summary)"
                self.portMenuItem.title = "Port: \(status.port.isEmpty ? "--" : status.port)"
                self.startItem.isEnabled = !status.processRunning
                self.stopItem.isEnabled = status.processRunning
                self.restartItem.isEnabled = status.processRunning
            }
        }
    }

    func showResult(success: Bool, output: String, title: String = "SeekDB") {
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = output.isEmpty ? (success ? "Done" : "Failed") : output
        alert.alertStyle = success ? .informational : .critical
        alert.runModal()
    }

    func confirmAction(message: String, info: String) -> Bool {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.messageText = message
        alert.informativeText = info
        alert.alertStyle = .critical
        alert.addButton(withTitle: "Cancel")
        alert.addButton(withTitle: "Confirm")
        return alert.runModal() == .alertSecondButtonReturn
    }

    // MARK: - Service Actions

    @objc func startService() {
        statusItem.button?.image = makeStatusIcon(.transitioning)
        statusMenuItem.title = "SeekDB: Starting..."
        runPrivileged(command: "start") { [weak self] success, output in
            if !success { self?.showResult(success: false, output: output, title: "Start Failed") }
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) { self?.refreshStatus() }
        }
    }

    @objc func stopService() {
        statusItem.button?.image = makeStatusIcon(.transitioning)
        statusMenuItem.title = "SeekDB: Stopping..."
        runPrivileged(command: "stop") { [weak self] success, output in
            if !success { self?.showResult(success: false, output: output, title: "Stop Failed") }
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) { self?.refreshStatus() }
        }
    }

    @objc func restartService() {
        statusItem.button?.image = makeStatusIcon(.transitioning)
        statusMenuItem.title = "SeekDB: Restarting..."
        runPrivileged(command: "restart") { [weak self] success, output in
            if !success { self?.showResult(success: false, output: output, title: "Restart Failed") }
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) { self?.refreshStatus() }
        }
    }

    // MARK: - Logs

    @objc func viewLogs() {
        let logDir = readConfigValue("base-dir", fallback: "/opt/homebrew/var/seekdb/data") + "/log"
        openTerminal("tail -n 200 \(logDir)/seekdb.log \(logDir)/launchd.out.log \(logDir)/launchd.err.log 2>/dev/null; echo '\\nPress any key to close'; read -n1")
    }

    @objc func followLogs() {
        let logDir = readConfigValue("base-dir", fallback: "/opt/homebrew/var/seekdb/data") + "/log"
        openTerminal("tail -n 50 -F \(logDir)/seekdb.log \(logDir)/launchd.out.log \(logDir)/launchd.err.log 2>/dev/null")
    }

    // MARK: - Settings

    @objc func openSettings() {
        settingsController.onSaved = { [weak self] in
            self?.refreshStatus()
        }
        settingsController.showWindow()
    }

    // MARK: - Diagnostics

    @objc func runDoctor() {
        let baseDir = readConfigValue("base-dir", fallback: "/opt/homebrew/var/seekdb/data")
        let logDir = baseDir + "/log"
        let port = readConfigValue("port", fallback: "2881")
        let script = """
        echo 'SeekDB diagnostics'
        echo '------------------'
        test -x /opt/homebrew/bin/seekdb && echo 'binary     : ok' || echo 'binary     : missing'
        test -f \(SEEKDB_CONFIG) && echo 'config     : ok' || echo 'config     : missing'
        test -d \(baseDir) && echo 'base dir   : ok' || echo 'base dir   : missing'
        test -d \(logDir) && echo 'log dir    : ok' || echo 'log dir    : missing'
        nc -z 127.0.0.1 \(port) 2>/dev/null && echo 'port       : open (\(port))' || echo 'port       : closed (\(port))'
        pgrep -f /opt/homebrew/bin/seekdb >/dev/null && echo 'process    : running' || echo 'process    : not running'
        echo 'disk       :'
        df -h \(baseDir) 2>/dev/null || df -h /opt/homebrew 2>/dev/null
        echo 'memory     :' $(( $(sysctl -n hw.memsize 2>/dev/null) / 1024 / 1024 )) MB
        echo '\\nPress any key to close'; read -n1
        """
        openTerminal(script)
    }

    // MARK: - Setup / Dangerous Actions

    @objc func setupService() {
        guard confirmAction(
            message: "Setup SeekDB Service?",
            info: "This will create directories, enable boot startup, and start SeekDB."
        ) else { return }
        statusItem.button?.image = makeStatusIcon(.transitioning)
        runPrivileged(command: "setup") { [weak self] success, output in
            self?.showResult(success: success, output: output, title: "Setup")
            self?.refreshStatus()
        }
    }

    @objc func cleanData() {
        guard confirmAction(
            message: "Clean All Data?",
            info: "This will stop SeekDB and remove all config and data directories.\nThis cannot be undone."
        ) else { return }
        statusItem.button?.image = makeStatusIcon(.transitioning)
        runPrivileged(command: "clean-data", args: ["--force"]) { [weak self] success, output in
            self?.showResult(success: success, output: output, title: "Clean Data")
            self?.refreshStatus()
        }
    }

    @objc func uninstallService() {
        guard confirmAction(
            message: "Uninstall SeekDB?",
            info: "This will stop the service and remove all installed files, config, and data.\nThis cannot be undone."
        ) else { return }
        runPrivileged(command: "uninstall") { [weak self] success, output in
            self?.showResult(success: success, output: output, title: "Uninstall")
            if success {
                NSApp.terminate(nil)
            }
            self?.refreshStatus()
        }
    }

    // MARK: - Quit

    @objc func quitApp() {
        NSApp.terminate(nil)
    }
}

// MARK: - Entry Point

let app = NSApplication.shared
let delegate = SeekDBMenuBarApp()
app.delegate = delegate
app.setActivationPolicy(.accessory)
app.run()
