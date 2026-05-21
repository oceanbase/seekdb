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

func readConfigPort() -> String {
    guard let content = try? String(contentsOfFile: SEEKDB_CONFIG, encoding: .utf8) else {
        return DEFAULT_PORT
    }
    for line in content.components(separatedBy: "\n") {
        let trimmed = line.trimmingCharacters(in: .whitespaces)
        if trimmed.hasPrefix("#") || trimmed.hasPrefix(";") || trimmed.isEmpty { continue }
        let parts = trimmed.split(separator: "=", maxSplits: 1)
        if parts.count == 2 && parts[0].trimmingCharacters(in: .whitespaces) == "port" {
            return parts[1].trimmingCharacters(in: .whitespaces)
        }
    }
    return DEFAULT_PORT
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
    let escaped = command.replacingOccurrences(of: "\\", with: "\\\\")
                        .replacingOccurrences(of: "\"", with: "\\\"")
    let script = """
    tell application "Terminal"
        activate
        do script "\(escaped)"
    end tell
    """
    if let appleScript = NSAppleScript(source: script) {
        var error: NSDictionary?
        appleScript.executeAndReturnError(&error)
    }
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

// MARK: - App Delegate

class SeekDBMenuBarApp: NSObject, NSApplicationDelegate {
    var statusItem: NSStatusItem!
    var menu: NSMenu!
    var statusTimer: Timer?
    var currentStatus = SeekDBStatus()

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

        // Configuration submenu
        let configSub = NSMenu()
        let showConfig = NSMenuItem(title: "Show Current Config", action: #selector(showConfig), keyEquivalent: "")
        showConfig.target = self
        configSub.addItem(showConfig)
        configSub.addItem(.separator())

        let portChange = NSMenuItem(title: "Change Port...", action: #selector(changePort), keyEquivalent: "")
        portChange.target = self
        configSub.addItem(portChange)

        let baseDirChange = NSMenuItem(title: "Change Base Dir...", action: #selector(changeBaseDir), keyEquivalent: "")
        baseDirChange.target = self
        configSub.addItem(baseDirChange)

        let dataDirChange = NSMenuItem(title: "Change Data Dir...", action: #selector(changeDataDir), keyEquivalent: "")
        dataDirChange.target = self
        configSub.addItem(dataDirChange)

        let redoDirChange = NSMenuItem(title: "Change Redo Dir...", action: #selector(changeRedoDir), keyEquivalent: "")
        redoDirChange.target = self
        configSub.addItem(redoDirChange)

        let configItem = NSMenuItem(title: "Configuration", action: nil, keyEquivalent: "")
        configItem.submenu = configSub
        menu.addItem(configItem)

        // Diagnostics submenu
        let diagSub = NSMenu()
        let doctorItem = NSMenuItem(title: "Run Doctor", action: #selector(runDoctor), keyEquivalent: "")
        doctorItem.target = self
        diagSub.addItem(doctorItem)

        let pathsItem = NSMenuItem(title: "Show Paths", action: #selector(showPaths), keyEquivalent: "")
        pathsItem.target = self
        diagSub.addItem(pathsItem)

        let diagItem = NSMenuItem(title: "Diagnostics", action: nil, keyEquivalent: "")
        diagItem.submenu = diagSub
        menu.addItem(diagItem)

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
        openTerminal("\(SEEKDBCTL) logs")
    }

    @objc func followLogs() {
        openTerminal("\(SEEKDBCTL) logs -f")
    }

    // MARK: - Configuration

    @objc func showConfig() {
        openTerminal("\(SEEKDBCTL) config")
    }

    @objc func changePort() {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.messageText = "Change SeekDB Port"
        alert.informativeText = "Enter new port number (requires restart):"
        let input = NSTextField(frame: NSRect(x: 0, y: 0, width: 200, height: 24))
        input.stringValue = currentStatus.port.isEmpty ? "2881" : currentStatus.port
        alert.accessoryView = input
        alert.addButton(withTitle: "Update & Restart")
        alert.addButton(withTitle: "Cancel")
        alert.window.initialFirstResponder = input
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let newPort = input.stringValue.trimmingCharacters(in: .whitespaces)
        guard !newPort.isEmpty else { return }
        statusItem.button?.image = makeStatusIcon(.transitioning)
        runPrivileged(command: "config", args: ["--port", newPort, "--restart"]) { [weak self] success, output in
            self?.showResult(success: success, output: output, title: "Change Port")
            self?.refreshStatus()
        }
    }

    func changeDirWithPanel(title: String, flag: String) {
        NSApp.activate(ignoringOtherApps: true)
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.prompt = "Select"
        panel.title = title
        guard panel.runModal() == .OK, let url = panel.url else { return }

        let confirmAlert = NSAlert()
        confirmAlert.messageText = title
        confirmAlert.informativeText = "New path: \(url.path)\nService will restart."
        confirmAlert.addButton(withTitle: "Update & Restart")
        confirmAlert.addButton(withTitle: "Cancel")
        guard confirmAlert.runModal() == .alertFirstButtonReturn else { return }

        statusItem.button?.image = makeStatusIcon(.transitioning)
        runPrivileged(command: "config", args: [flag, url.path, "--restart"]) { [weak self] success, output in
            self?.showResult(success: success, output: output, title: title)
            self?.refreshStatus()
        }
    }

    @objc func changeBaseDir() {
        changeDirWithPanel(title: "Change Base Directory", flag: "--base-dir")
    }

    @objc func changeDataDir() {
        changeDirWithPanel(title: "Change Data Directory", flag: "--data-dir")
    }

    @objc func changeRedoDir() {
        changeDirWithPanel(title: "Change Redo Directory", flag: "--redo-dir")
    }

    // MARK: - Diagnostics

    @objc func runDoctor() {
        openTerminal("\(SEEKDBCTL) doctor")
    }

    @objc func showPaths() {
        openTerminal("\(SEEKDBCTL) paths")
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
