import AppKit
import Darwin
import Security

// MARK: - Constants

let SEEKDBCTL = "/opt/seekdb/bin/seekdbctl"
let STATUS_INTERVAL_STABLE: TimeInterval = 10.0
let STATUS_INTERVAL_TRANSIENT: TimeInterval = 1.0

// MARK: - Status Model

let SEEKDB_CONFIG = "/opt/seekdb/etc/seekdb/seekdb.cnf"
let MONITOR_APP_PATH = "/Applications/seekdb Monitor.app"
let SEEKDBCTL_LOCK_PID = "/tmp/seekdbctl.lock.d/pid"
let ACTIVE_PATHS_FILE = "/opt/seekdb/var/seekdb/run/active_paths"
let UNINSTALL_MARKER_NAME = "uninstalling"

enum ServiceState { case active, starting, stopping, stopped }

struct SeekDBStatus {
    var port = ""
    var processRunning = false
    var pid = ""
    var portOpen = false
    var launchdLoaded = false

    var state: ServiceState {
        if launchdLoaded {
            if processRunning && portOpen { return .active }
            return .starting
        } else {
            if processRunning || portOpen { return .stopping }
            return .stopped
        }
    }

    var summary: String {
        switch state {
        case .active:   return pid.isEmpty ? "Active" : "Active (PID \(pid))"
        case .starting: return "Starting…"
        case .stopping: return "Stopping…"
        case .stopped:  return "Stopped"
        }
    }

    static func parse(output: String) -> SeekDBStatus {
        var s = SeekDBStatus()
        for rawLine in output.components(separatedBy: .newlines) {
            let line = rawLine.trimmingCharacters(in: .whitespacesAndNewlines)
            let lower = line.lowercased()
            let value = line.split(separator: ":", maxSplits: 1)
                .dropFirst()
                .first?
                .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""

            if lower.hasPrefix("port") {
                if value == "open" {
                    s.portOpen = true
                } else if value == "closed" {
                    s.portOpen = false
                } else if !value.isEmpty {
                    s.port = value
                }
            } else if lower.hasPrefix("process") {
                let lowerValue = value.lowercased()
                s.processRunning = lowerValue.hasPrefix("running")
                if let range = line.range(of: #"\(pid [0-9]+\)"#, options: .regularExpression) {
                    s.pid = String(line[range])
                        .replacingOccurrences(of: "(pid ", with: "")
                        .replacingOccurrences(of: ")", with: "")
                }
            } else if lower.hasPrefix("launchd") {
                s.launchdLoaded = value == "loaded"
            }
        }
        return s
    }
}

func readConfigValue(_ key: String, fallback: String = "") -> String {
    return readKeyValue(from: SEEKDB_CONFIG, key: key, fallback: fallback)
}

func readActivePathValue(_ key: String, fallback: String = "") -> String {
    return readKeyValue(from: ACTIVE_PATHS_FILE, key: key, fallback: fallback)
}

func readKeyValue(from file: String, key: String, fallback: String = "") -> String {
    guard let content = try? String(contentsOfFile: file, encoding: .utf8) else {
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

func readRuntimeBaseDir() -> String {
    let activeBaseDir = readActivePathValue("base-dir")
    if !activeBaseDir.isEmpty { return activeBaseDir }
    return readConfigValue("base-dir", fallback: "/opt/seekdb/var/seekdb/data")
}

func logDetailsText() -> String {
    let logDir = readRuntimeBaseDir() + "/log"
    return "Check logs for details:\n\n\(logDir)/seekdb.log\n\(logDir)/launchd.out.log\n\(logDir)/launchd.err.log"
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

// MARK: - Admin authorization

func authorizeAdmin(prompt: String) -> Bool {
    var authRef: AuthorizationRef? = nil
    guard AuthorizationCreate(nil, nil, AuthorizationFlags(), &authRef) == errAuthorizationSuccess,
          let authRef = authRef else { return false }
    defer { AuthorizationFree(authRef, []) }

    // Hold NSStrings so their utf8 pointers remain valid for the call.
    let rightName: NSString = "system.privilege.admin"
    let promptKey: NSString = "prompt"
    let promptVal = prompt as NSString

    var promptItem = AuthorizationItem(
        name: promptKey.utf8String!,
        valueLength: prompt.utf8.count,
        value: UnsafeMutableRawPointer(mutating: promptVal.utf8String!),
        flags: 0
    )
    var rightItem = AuthorizationItem(
        name: rightName.utf8String!,
        valueLength: 0,
        value: nil,
        flags: 0
    )

    let status = withUnsafeMutablePointer(to: &promptItem) { promptPtr -> OSStatus in
        var env = AuthorizationEnvironment(count: 1, items: promptPtr)
        return withUnsafeMutablePointer(to: &rightItem) { rightPtr -> OSStatus in
            var rights = AuthorizationRights(count: 1, items: rightPtr)
            return AuthorizationCopyRights(
                authRef, &rights, &env,
                [.extendRights, .interactionAllowed],
                nil
            )
        }
    }
    return status == errAuthorizationSuccess
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

func shellQuote(_ value: String) -> String {
    return "'" + value.replacingOccurrences(of: "'", with: "'\\''") + "'"
}

func chmod(_ path: String, _ mode: UInt16) {
    Darwin.chmod(path, mode_t(mode))
}

func pathIsInTrash(_ path: String) -> Bool {
    return path.split(separator: "/").contains { component in
        component == ".Trash" || component == ".Trashes"
    }
}

func seekdbctlOperationInProgress() -> Bool {
    guard let pidText = try? String(contentsOfFile: SEEKDBCTL_LOCK_PID, encoding: .utf8),
          let pid = Int32(pidText.trimmingCharacters(in: .whitespacesAndNewlines)),
          pid > 0 else {
        return false
    }
    return kill(pid, 0) == 0 || errno == EPERM
}

func seekdbCoreInstallMissing() -> Bool {
    return !FileManager.default.fileExists(atPath: SEEKDBCTL)
        && !FileManager.default.fileExists(atPath: "/opt/seekdb")
}

func uninstallMarkerExists() -> Bool {
    let baseDir = readRuntimeBaseDir()
    return FileManager.default.fileExists(atPath: "\(baseDir)/run/\(UNINSTALL_MARKER_NAME)")
}

// MARK: - Status Icon

func makeStatusIcon(_ state: ServiceState) -> NSImage {
    let resource: String
    switch state {
    case .active:
        resource = "active"
    case .starting, .stopping:
        resource = "loading"
    case .stopped:
        resource = "stopped"
    }

    if let url = Bundle.main.url(forResource: resource, withExtension: "svg"),
       let image = NSImage(contentsOf: url) {
        image.size = NSSize(width: 18, height: 18)
        image.isTemplate = false
        return image
    }

    let size = NSSize(width: 18, height: 18)
    let image = NSImage(size: size, flipped: false) { rect in
        let color: NSColor
        switch state {
        case .active:   color = .systemGreen
        case .starting: color = .systemYellow
        case .stopping: color = .systemOrange
        case .stopped:  color = .systemRed
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
    var bootStartupSwitch: NSButton!
    var statusLabel: NSTextField!
    private var bootStartupApplying = false

    func showWindow() {
        if window != nil && window.isVisible {
            window.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            loadBootStartupState()
            return
        }

        let w: CGFloat = 460
        let h: CGFloat = 150
        window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: w, height: h),
            styleMask: [.titled, .closable],
            backing: .buffered, defer: false)
        window.title = "seekdb Settings"
        window.center()
        window.delegate = self
        window.isReleasedWhenClosed = false

        let content = window.contentView!
        let pad: CGFloat = 16

        let bootLbl = NSTextField(labelWithString: "Start at Boot")
        bootLbl.font = NSFont.systemFont(ofSize: 13, weight: .semibold)
        bootLbl.frame = NSRect(x: pad, y: h - 50, width: w - 2 * pad, height: 22)
        content.addSubview(bootLbl)

        bootStartupSwitch = NSButton(checkboxWithTitle: "Start automatically when macOS boots", target: self, action: #selector(bootStartupToggled))
        bootStartupSwitch.frame = NSRect(x: pad, y: h - 82, width: w - 2 * pad, height: 22)
        bootStartupSwitch.setButtonType(.switch)
        bootStartupSwitch.state = .off
        bootStartupSwitch.isEnabled = false
        content.addSubview(bootStartupSwitch)

        statusLabel = NSTextField(labelWithString: "")
        statusLabel.frame = NSRect(x: pad, y: pad + 5, width: 270, height: 22)
        statusLabel.textColor = .secondaryLabelColor
        content.addSubview(statusLabel)

        let closeButton = NSButton(title: "Close", target: self, action: #selector(closeSettings))
        closeButton.frame = NSRect(x: w - 96 - pad, y: pad, width: 96, height: 32)
        closeButton.bezelStyle = .rounded
        closeButton.keyEquivalent = "\r"
        content.addSubview(closeButton)

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
                self.statusLabel.stringValue = "Failed to change boot startup."
                NSApp.activate(ignoringOtherApps: true)
                let alert = NSAlert()
                alert.messageText = "Failed to change boot startup"
                alert.informativeText = logDetailsText()
                alert.alertStyle = .warning
                alert.runModal()
            }
        }
    }

    @objc func closeSettings() {
        window.close()
    }
}

// MARK: - Main Window

class MainWindowController: NSObject, NSWindowDelegate {
    weak var appDelegate: SeekDBMenuBarApp?
    var window: NSWindow!
    var statusDot: NSView!
    var statusTitleLabel: NSTextField!
    var statusDetailLabel: NSTextField!
    var startButton: NSButton!
    var stopButton: NSButton!
    var restartButton: NSButton!
    var settingsButton: NSButton!
    var initializeButton: NSButton!

    func showWindow() {
        if window == nil { buildWindow() }
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        window.orderOut(nil)
        return false
    }

    private func buildWindow() {
        let w: CGFloat = 480
        let h: CGFloat = 500
        window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: w, height: h),
            styleMask: [.titled, .closable, .miniaturizable],
            backing: .buffered, defer: false)
        window.title = "seekdb Monitor"
        window.center()
        window.delegate = self
        window.isReleasedWhenClosed = false

        let content = window.contentView!
        let pad: CGFloat = 20
        var y = h - pad

        // Status card
        let cardH: CGFloat = 72
        y -= cardH
        let card = NSView(frame: NSRect(x: pad, y: y, width: w - 2 * pad, height: cardH))
        card.wantsLayer = true
        card.layer?.backgroundColor = NSColor.controlBackgroundColor.cgColor
        card.layer?.cornerRadius = 8
        card.layer?.borderWidth = 1
        card.layer?.borderColor = NSColor.separatorColor.cgColor
        content.addSubview(card)

        let dotSize: CGFloat = 16
        statusDot = NSView(frame: NSRect(x: 18, y: (cardH - dotSize) / 2, width: dotSize, height: dotSize))
        statusDot.wantsLayer = true
        statusDot.layer?.cornerRadius = dotSize / 2
        statusDot.layer?.backgroundColor = NSColor.systemGray.cgColor
        card.addSubview(statusDot)

        statusTitleLabel = NSTextField(labelWithString: "Unknown")
        statusTitleLabel.font = NSFont.systemFont(ofSize: 16, weight: .semibold)
        statusTitleLabel.frame = NSRect(x: 46, y: cardH / 2 + 2, width: card.frame.width - 60, height: 22)
        card.addSubview(statusTitleLabel)

        statusDetailLabel = NSTextField(labelWithString: "PID —  ·  Port —")
        statusDetailLabel.font = NSFont.systemFont(ofSize: 12)
        statusDetailLabel.textColor = .secondaryLabelColor
        statusDetailLabel.frame = NSRect(x: 46, y: cardH / 2 - 20, width: card.frame.width - 60, height: 18)
        card.addSubview(statusDetailLabel)

        y -= 14

        let btnH: CGFloat = 30
        let gap: CGFloat = 8
        let totalW = w - 2 * pad

        func addSectionLabel(_ title: String) {
            let lbl = NSTextField(labelWithString: title.uppercased())
            lbl.font = NSFont.systemFont(ofSize: 10, weight: .bold)
            lbl.textColor = .secondaryLabelColor
            lbl.frame = NSRect(x: pad, y: y - 14, width: totalW, height: 14)
            content.addSubview(lbl)
            y -= 22
        }

        func makeButton(_ title: String, _ selector: Selector) -> NSButton {
            let btn = NSButton(title: title, target: appDelegate, action: selector)
            btn.bezelStyle = .rounded
            return btn
        }

        func addRow(_ buttons: [NSButton]) {
            let count = CGFloat(buttons.count)
            let btnW = (totalW - gap * (count - 1)) / count
            for (i, btn) in buttons.enumerated() {
                btn.frame = NSRect(x: pad + CGFloat(i) * (btnW + gap), y: y - btnH, width: btnW, height: btnH)
                content.addSubview(btn)
            }
            y -= btnH + 12
        }

        addSectionLabel("Service")
        startButton = makeButton("Start", #selector(SeekDBMenuBarApp.startService))
        stopButton = makeButton("Stop", #selector(SeekDBMenuBarApp.stopService))
        restartButton = makeButton("Restart", #selector(SeekDBMenuBarApp.restartService))
        addRow([startButton, stopButton, restartButton])

        addSectionLabel("Logs")
        addRow([
            makeButton("View Logs", #selector(SeekDBMenuBarApp.viewLogs)),
            makeButton("Save Logs to…", #selector(SeekDBMenuBarApp.saveLogs))
        ])

        addSectionLabel("Configuration")
        settingsButton = makeButton("Settings…", #selector(SeekDBMenuBarApp.openSettings))
        addRow([
            settingsButton,
            makeButton("Run Doctor", #selector(SeekDBMenuBarApp.runDoctor))
        ])

        addSectionLabel("Maintenance")
        initializeButton = makeButton("Initialize Database", #selector(SeekDBMenuBarApp.setupService))
        addRow([
            initializeButton,
            makeButton("Uninstall…", #selector(SeekDBMenuBarApp.uninstallService))
        ])

        let closeBtn = NSButton(title: "Close", target: self, action: #selector(closeWindow))
        closeBtn.bezelStyle = .rounded
        closeBtn.frame = NSRect(x: w - pad - 90, y: pad, width: 90, height: 30)
        content.addSubview(closeBtn)
    }

    @objc func closeWindow() {
        window?.orderOut(nil)
    }

    func update(_ status: SeekDBStatus, locked: Bool = false) {
        guard window != nil else { return }
        let color: NSColor
        switch status.state {
        case .active:   color = .systemGreen
        case .starting: color = .systemYellow
        case .stopping: color = .systemOrange
        case .stopped:  color = .systemRed
        }
        statusDot.layer?.backgroundColor = color.cgColor
        statusTitleLabel.stringValue = "seekdb: \(status.summary)"
        let pid = status.pid.isEmpty ? "—" : status.pid
        let port = status.port.isEmpty ? "—" : status.port
        statusDetailLabel.stringValue = "PID \(pid)  ·  Port \(port)"
        if locked {
            startButton.isEnabled = false
            stopButton.isEnabled = false
            restartButton.isEnabled = false
            settingsButton.isEnabled = false
            initializeButton.isEnabled = false
        } else {
            startButton.isEnabled = (status.state == .stopped)
            stopButton.isEnabled = (status.state == .active)
            restartButton.isEnabled = (status.state == .active)
            settingsButton.isEnabled = true
            initializeButton.isEnabled = true
        }
    }
}

// MARK: - App Delegate

class SeekDBMenuBarApp: NSObject, NSApplicationDelegate {
    var statusItem: NSStatusItem!
    var menu: NSMenu!
    var statusTimer: Timer?
    var appRemovalTimer: Timer?
    var currentStatus = SeekDBStatus()
    let settingsController = SettingsWindowController()
    let mainWindowController = MainWindowController()
    let launchedFromInstalledApp = Bundle.main.bundleURL.standardizedFileURL.path == MONITOR_APP_PATH
    var uninstallingAfterAppRemoval = false
    var serviceOperationInProgress = false
    var startingStateStartTime: Date? = nil
    var processEverSeenRunning = false
    var startupFailureShown = false
    let STARTUP_FAILURE_TIMEOUT: TimeInterval = 30.0

    // menu items that update dynamically
    var statusMenuItem: NSMenuItem!
    var portMenuItem: NSMenuItem!
    var startItem: NSMenuItem!
    var stopItem: NSMenuItem!
    var restartItem: NSMenuItem!
    var settingsItem: NSMenuItem!
    var setupItem: NSMenuItem!

    func applicationDidFinishLaunching(_ notification: Notification) {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        statusItem.button?.image = makeStatusIcon(.stopped)

        if let button = statusItem.button {
            button.target = self
            button.action = #selector(statusBarClicked(_:))
            button.sendAction(on: [.leftMouseUp, .rightMouseUp])
        }

        buildMenu()

        mainWindowController.appDelegate = self
        mainWindowController.showWindow()

        refreshStatus()
        startAppRemovalMonitor()
    }

    @objc func statusBarClicked(_ sender: Any?) {
        let event = NSApp.currentEvent
        let isRightClick = event?.type == .rightMouseUp
            || (event?.modifierFlags.contains(.control) ?? false)
        if isRightClick {
            statusItem.menu = menu
            statusItem.button?.performClick(nil)
            statusItem.menu = nil
        } else {
            mainWindowController.showWindow()
        }
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        mainWindowController.showWindow()
        return true
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return false
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        statusTimer?.invalidate()
        appRemovalTimer?.invalidate()
        return .terminateNow
    }

    func buildMenu() {
        menu = NSMenu()

        statusMenuItem = NSMenuItem(title: "seekdb: Unknown", action: nil, keyEquivalent: "")
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

        let saveLogsItem = NSMenuItem(title: "Save Logs to…", action: #selector(saveLogs), keyEquivalent: "")
        saveLogsItem.target = self
        menu.addItem(saveLogsItem)

        menu.addItem(.separator())

        settingsItem = NSMenuItem(title: "Settings...", action: #selector(openSettings), keyEquivalent: ",")
        settingsItem.target = self
        menu.addItem(settingsItem)

        let doctorItem = NSMenuItem(title: "Run Doctor", action: #selector(runDoctor), keyEquivalent: "")
        doctorItem.target = self
        menu.addItem(doctorItem)

        menu.addItem(.separator())

        setupItem = NSMenuItem(title: "Initialize Database", action: #selector(setupService), keyEquivalent: "")
        setupItem.target = self
        menu.addItem(setupItem)

        let uninstallItem = NSMenuItem(title: "Uninstall...", action: #selector(uninstallService), keyEquivalent: "")
        uninstallItem.target = self
        menu.addItem(uninstallItem)

        menu.addItem(.separator())

        let quitItem = NSMenuItem(title: "Quit Monitor", action: #selector(quitMonitor), keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)
    }

    var statusRefreshInFlight = false

    func refreshStatus() {
        guard !statusRefreshInFlight else { return }
        statusRefreshInFlight = true
        runPrivileged(command: "status") { [weak self] success, output in
            guard let self = self else { return }
            self.statusRefreshInFlight = false
            let status = success ? SeekDBStatus.parse(output: output) : SeekDBStatus()
            self.applyStatus(status)
        }
    }

    func applyButtonState() {
        startItem.isEnabled = false
        stopItem.isEnabled = false
        restartItem.isEnabled = false
        settingsItem.isEnabled = false
        setupItem.isEnabled = false
        mainWindowController.update(currentStatus, locked: true)
    }

    func applyStatus(_ status: SeekDBStatus) {
        currentStatus = status

        // Detect startup failure: launchd loaded but process never stays running
        if status.state == .starting && !serviceOperationInProgress {
            if startingStateStartTime == nil {
                startingStateStartTime = Date()
                processEverSeenRunning = false
                startupFailureShown = false
            }
            if status.processRunning { processEverSeenRunning = true }
            if !processEverSeenRunning && !startupFailureShown,
               let since = startingStateStartTime,
               Date().timeIntervalSince(since) > STARTUP_FAILURE_TIMEOUT {
                startupFailureShown = true
                showStartupFailure()
            }
        } else {
            startingStateStartTime = nil
            processEverSeenRunning = false
            startupFailureShown = false
        }

        statusItem.button?.image = makeStatusIcon(status.state)
        statusMenuItem.title = "seekdb: \(status.summary)"
        portMenuItem.title = "Port: \(status.port.isEmpty ? "--" : status.port)"
        if serviceOperationInProgress {
            startItem.isEnabled = false
            stopItem.isEnabled = false
            restartItem.isEnabled = false
            settingsItem.isEnabled = false
            setupItem.isEnabled = false
        } else {
            startItem.isEnabled = (status.state == .stopped)
            stopItem.isEnabled = (status.state == .active)
            restartItem.isEnabled = (status.state == .active)
            settingsItem.isEnabled = true
            setupItem.isEnabled = true
        }
        mainWindowController.update(status, locked: serviceOperationInProgress)
        scheduleNextPoll()
    }

    func scheduleNextPoll() {
        statusTimer?.invalidate()
        let interval: TimeInterval
        switch currentStatus.state {
        case .active, .stopped:
            interval = STATUS_INTERVAL_STABLE
        case .starting, .stopping:
            interval = STATUS_INTERVAL_TRANSIENT
        }
        statusTimer = Timer.scheduledTimer(withTimeInterval: interval, repeats: false) { [weak self] _ in
            self?.refreshStatus()
        }
    }

    func startAppRemovalMonitor() {
        appRemovalTimer?.invalidate()
        appRemovalTimer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { [weak self] _ in
            self?.checkAppRemoval()
        }
        checkAppRemoval()
    }

    func checkAppRemoval() {
        guard !uninstallingAfterAppRemoval else { return }

        let bundlePath = Bundle.main.bundleURL.standardizedFileURL.path
        let currentBundleInTrash = pathIsInTrash(bundlePath)
        let installedBundleMissing = launchedFromInstalledApp
            && !FileManager.default.fileExists(atPath: MONITOR_APP_PATH)

        let uninstallPending = uninstallMarkerExists()

        guard currentBundleInTrash || installedBundleMissing || uninstallPending else { return }

        if uninstallPending || (installedBundleMissing && !currentBundleInTrash
            && (seekdbctlOperationInProgress() || seekdbCoreInstallMissing())) {
            appRemovalTimer?.invalidate()
            statusTimer?.invalidate()
            NSApp.terminate(nil)
            return
        }

        uninstallingAfterAppRemoval = true
        appRemovalTimer?.invalidate()
        statusTimer?.invalidate()
        statusItem.button?.image = makeStatusIcon(.stopping)
        statusMenuItem.title = "seekdb: Uninstalling…"

        runPrivileged(command: "uninstall") { [weak self] success, output in
            if !success {
                self?.showResult(success: false, output: output, title: "Automatic Uninstall Failed")
            }
            NSApp.terminate(nil)
        }
    }

    func showResult(success: Bool, output: String, title: String = "seekdb") {
        let alert = NSAlert()
        alert.messageText = title
        if success {
            alert.informativeText = "Done."
            alert.alertStyle = .informational
        } else {
            alert.informativeText = logDetailsText()
            alert.alertStyle = .critical
        }
        alert.runModal()
    }

    func showStartupFailure() {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.messageText = "seekdb Failed to Start"
        alert.informativeText = "The service has not started after \(Int(STARTUP_FAILURE_TIMEOUT)) seconds. The process may be crashing on startup.\n\n\(logDetailsText())"
        alert.alertStyle = .critical
        alert.addButton(withTitle: "Stop Service")
        alert.addButton(withTitle: "View Logs")
        alert.addButton(withTitle: "Dismiss")
        let response = alert.runModal()
        if response == .alertFirstButtonReturn {
            stopService()
        } else if response == .alertSecondButtonReturn {
            viewLogs()
        }
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
        guard !serviceOperationInProgress else { return }
        guard authorizeAdmin(prompt: "seekdb Monitor needs your password to start the database service.") else { return }
        serviceOperationInProgress = true
        startingStateStartTime = nil
        startupFailureShown = false
        statusTimer?.invalidate()
        statusItem.button?.image = makeStatusIcon(.starting)
        statusMenuItem.title = "seekdb: Starting…"
        applyButtonState()
        runPrivileged(command: "start") { [weak self] success, output in
            guard let self = self else { return }
            self.serviceOperationInProgress = false
            if !success { self.showResult(success: false, output: output, title: "Start Failed") }
            self.refreshStatus()
        }
    }

    @objc func stopService() {
        guard !serviceOperationInProgress else { return }
        guard authorizeAdmin(prompt: "seekdb Monitor needs your password to stop the database service.") else { return }
        serviceOperationInProgress = true
        statusTimer?.invalidate()
        statusItem.button?.image = makeStatusIcon(.stopping)
        statusMenuItem.title = "seekdb: Stopping…"
        applyButtonState()
        runPrivileged(command: "stop") { [weak self] success, output in
            guard let self = self else { return }
            self.serviceOperationInProgress = false
            if !success { self.showResult(success: false, output: output, title: "Stop Failed") }
            self.refreshStatus()
        }
    }

    @objc func restartService() {
        guard !serviceOperationInProgress else { return }
        guard authorizeAdmin(prompt: "seekdb Monitor needs your password to restart the database service.") else { return }
        serviceOperationInProgress = true
        statusTimer?.invalidate()
        statusItem.button?.image = makeStatusIcon(.starting)
        statusMenuItem.title = "seekdb: Restarting…"
        applyButtonState()
        runPrivileged(command: "restart") { [weak self] success, output in
            guard let self = self else { return }
            self.serviceOperationInProgress = false
            if !success { self.showResult(success: false, output: output, title: "Restart Failed") }
            self.refreshStatus()
        }
    }

    // MARK: - Logs

    @objc func viewLogs() {
        openTerminal("\(shellQuote(SEEKDBCTL)) logs; echo '\\nPress any key to close'; read -n1")
    }

    @objc func saveLogs() {
        let panel = NSSavePanel()
        panel.title = "Save seekdb Logs"
        panel.nameFieldStringValue = "seekdb-logs"
        panel.canCreateDirectories = true
        panel.allowedContentTypes = []
        NSApp.activate(ignoringOtherApps: true)
        guard panel.runModal() == .OK, let dest = panel.url else { return }

        let logDir = readRuntimeBaseDir() + "/log"
        let src = URL(fileURLWithPath: logDir)
        do {
            if FileManager.default.fileExists(atPath: dest.path) {
                try FileManager.default.removeItem(at: dest)
            }
            try FileManager.default.copyItem(at: src, to: dest)
            NSWorkspace.shared.selectFile(nil, inFileViewerRootedAtPath: dest.path)
        } catch {
            let alert = NSAlert()
            alert.messageText = "Failed to save logs"
            alert.informativeText = error.localizedDescription
            alert.alertStyle = .warning
            alert.runModal()
        }
    }

    // MARK: - Settings

    @objc func openSettings() {
        guard !serviceOperationInProgress else { return }
        settingsController.showWindow()
    }

    // MARK: - Diagnostics

    @objc func runDoctor() {
        openTerminal("\(shellQuote(SEEKDBCTL)) doctor; echo '\\nPress any key to close'; read -n1")
    }

    // MARK: - Initialize / Dangerous Actions

    @objc func setupService() {
        guard !serviceOperationInProgress else { return }
        guard confirmAction(
            message: "Initialize Database?",
            info: "This will erase all database data and bootstrap a fresh instance.\nConfiguration and plugins will be preserved.\n\nThis cannot be undone."
        ) else { return }
        guard authorizeAdmin(prompt: "seekdb Monitor needs your password to initialize the database. All current data will be erased.") else { return }
        serviceOperationInProgress = true
        statusItem.button?.image = makeStatusIcon(.starting)
        refreshStatus()
        runPrivileged(command: "initialize") { [weak self] success, output in
            guard let self = self else { return }
            self.serviceOperationInProgress = false
            self.showResult(success: success, output: output, title: "Initialize Database")
            self.refreshStatus()
        }
    }

    @objc func uninstallService() {
        guard confirmAction(
            message: "Uninstall seekdb?",
            info: "This will stop the service and remove all installed files, config, and data.\nThis cannot be undone."
        ) else { return }
        guard authorizeAdmin(prompt: "seekdb Monitor needs your password to uninstall seekdb and remove all data.") else { return }
        runPrivileged(command: "uninstall") { [weak self] success, output in
            self?.showResult(success: success, output: output, title: "Uninstall")
            if success {
                NSApp.terminate(nil)
            }
            self?.refreshStatus()
        }
    }

    // MARK: - Window

    @objc func quitMonitor() {
        NSApp.terminate(nil)
    }

    @objc func openMainWindow() {
        mainWindowController.showWindow()
    }
}

// MARK: - Entry Point

let app = NSApplication.shared
let delegate = SeekDBMenuBarApp()
app.delegate = delegate
app.setActivationPolicy(.accessory)

let mainMenu = NSMenu()
let editMenuItem = NSMenuItem()
editMenuItem.submenu = {
    let menu = NSMenu(title: "Edit")
    menu.addItem(NSMenuItem(title: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x"))
    menu.addItem(NSMenuItem(title: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c"))
    menu.addItem(NSMenuItem(title: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v"))
    menu.addItem(NSMenuItem(title: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a"))
    return menu
}()
mainMenu.addItem(editMenuItem)
app.mainMenu = mainMenu

app.run()
