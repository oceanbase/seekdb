import AppKit
import Carbon
import Darwin
import Security

// MARK: - Constants

let SEEKDBCTL = "/opt/seekdb/bin/seekdbctl"
let STATUS_INTERVAL_STABLE: TimeInterval = 10.0
let STATUS_INTERVAL_TRANSIENT: TimeInterval = 1.0

// MARK: - Status Model

let SEEKDB_CONFIG = "/opt/seekdb/etc/seekdb/seekdb.cnf"
let SEEKDB_BIN = "/opt/seekdb/bin/seekdb"
let MONITOR_APP_PATH = "/Applications/seekdb Monitor.app"
let DEFAULT_PORT = "2881"

enum ServiceState { case active, starting, stopping, stopped }

struct SeekDBStatus {
    var port = ""
    var processRunning = false
    var pid = ""
    var portOpen = false

    var state: ServiceState {
        if processRunning && portOpen { return .active }
        if processRunning && !portOpen { return .starting }
        if !processRunning && portOpen { return .stopping }
        return .stopped
    }

    var summary: String {
        switch state {
        case .active:   return pid.isEmpty ? "Active" : "Active (PID \(pid))"
        case .starting: return "Starting…"
        case .stopping: return "Stopping…"
        case .stopped:  return "Stopped"
        }
    }

    static func detect() -> SeekDBStatus {
        var s = SeekDBStatus()
        s.port = readConfigPort()
        if let pid = installedSeekDBPid() {
            s.processRunning = true
            s.pid = pid
            s.portOpen = portIsListeningByPid(s.port, pid: pid)
        }
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

func canonicalPath(_ path: String) -> String {
    return URL(fileURLWithPath: path).resolvingSymlinksInPath().standardizedFileURL.path
}

func processExecutablePath(pid: String) -> String? {
    guard let pidValue = Int32(pid), pidValue > 0 else { return nil }
    var buffer = [CChar](repeating: 0, count: 4096)
    let result = proc_pidpath(pid_t(pidValue), &buffer, UInt32(buffer.count))
    guard result > 0 else { return nil }
    return String(cString: buffer)
}

func installedSeekDBPid() -> String? {
    let installedPath = SEEKDB_BIN
    let installedCanonicalPath = canonicalPath(installedPath)
    let pgrepResult = runCommand(["/usr/bin/pgrep", "-x", "seekdb"])
    guard pgrepResult.exitCode == 0 else { return nil }

    let pids = pgrepResult.output
        .components(separatedBy: .newlines)
        .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
        .filter { !$0.isEmpty }

    for pid in pids {
        guard let executablePath = processExecutablePath(pid: pid) else { continue }
        if executablePath == installedPath || canonicalPath(executablePath) == installedCanonicalPath {
            return pid
        }
    }
    return nil
}

func portIsListeningByPid(_ port: String, pid: String) -> Bool {
    let result = runCommand(["/usr/sbin/lsof", "-nP", "-iTCP:\(port)", "-sTCP:LISTEN", "-t"])
    let owners = result.output
        .components(separatedBy: .newlines)
        .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
        .filter { !$0.isEmpty }
    if result.exitCode == 0 && !owners.isEmpty {
        return owners.contains(pid)
    }

    // Non-root monitor processes may not be able to inspect a root LaunchDaemon
    // with lsof. Once the installed seekdb PID is confirmed, nc is the fallback
    // for detecting that the configured SQL port is reachable.
    let ncResult = runCommand(["/usr/bin/nc", "-z", "127.0.0.1", port])
    return ncResult.exitCode == 0
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

// MARK: - Port helpers

struct PortHolder {
    var command: String
    var pid: String
    var description: String { return "\(command) (PID \(pid))" }
}

func portInUse(_ port: Int) -> PortHolder? {
    let r = runCommand(["/usr/sbin/lsof", "-nP", "-iTCP:\(port)", "-sTCP:LISTEN"])
    if r.exitCode != 0 || r.output.isEmpty { return nil }
    let lines = r.output.components(separatedBy: "\n").filter { !$0.isEmpty }
    guard lines.count >= 2 else { return nil }
    let cols = lines[1].split(whereSeparator: { $0 == " " || $0 == "\t" }).filter { !$0.isEmpty }
    guard cols.count >= 2 else { return nil }
    return PortHolder(command: String(cols[0]), pid: String(cols[1]))
}

func waitForPort(_ port: String, timeout: TimeInterval, completion: @escaping (Bool) -> Void) {
    DispatchQueue.global(qos: .utility).async {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            let r = runCommand(["/usr/bin/nc", "-z", "127.0.0.1", port])
            if r.exitCode == 0 {
                DispatchQueue.main.async { completion(true) }
                return
            }
            Thread.sleep(forTimeInterval: 0.5)
        }
        DispatchQueue.main.async { completion(false) }
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

func chmod(_ path: String, _ mode: UInt16) {
    Darwin.chmod(path, mode_t(mode))
}

func pathIsInTrash(_ path: String) -> Bool {
    return path.split(separator: "/").contains { component in
        component == ".Trash" || component == ".Trashes"
    }
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
    var portField: NSTextField!
    var baseDirField: NSTextField!
    var dataDirField: NSTextField!
    var redoDirField: NSTextField!
    var pluginDirField: NSTextField!
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
        let h: CGFloat = 340
        window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: w, height: h),
            styleMask: [.titled, .closable],
            backing: .buffered, defer: false)
        window.title = "seekdb Settings"
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
        baseDirField = addRow(label: "Base Dir:", value: readConfigValue("base-dir", fallback: "/opt/seekdb/var/seekdb/data"), withBrowse: true)
        dataDirField = addRow(label: "Data Dir:", value: readConfigValue("data-dir", fallback: ""), withBrowse: true)
        redoDirField = addRow(label: "Redo Dir:", value: readConfigValue("redo-dir", fallback: ""), withBrowse: true)
        pluginDirField = addRow(label: "Plugin Dir:", value: readConfigValue("plugin-dir", fallback: ""), withBrowse: true)

        y -= 8

        let hint = NSTextField(wrappingLabelWithString: "Save & Restart applies all settings on restart.")
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
                self.statusLabel.stringValue = "Failed to change boot startup."
                let logDir = readConfigValue("base-dir", fallback: "/opt/seekdb/var/seekdb/data") + "/log"
                NSApp.activate(ignoringOtherApps: true)
                let alert = NSAlert()
                alert.messageText = "Failed to change boot startup"
                alert.informativeText = "Check logs for details:\n\n\(logDir)/seekdb.log\n\(logDir)/launchd.err.log"
                alert.alertStyle = .warning
                alert.runModal()
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
        let port = portField.stringValue.trimmingCharacters(in: .whitespaces)
        let baseDir = baseDirField.stringValue.trimmingCharacters(in: .whitespaces)
        let dataDir = dataDirField.stringValue.trimmingCharacters(in: .whitespaces)
        let redoDir = redoDirField.stringValue.trimmingCharacters(in: .whitespaces)
        let pluginDir = pluginDirField.stringValue.trimmingCharacters(in: .whitespaces)

        // 1a. Validate port
        guard !port.isEmpty, let portNum = Int(port), portNum >= 1024, portNum <= 65535 else {
            return showValidationError("Port must be a number between 1024 and 65535.")
        }

        // 1b. Validate paths (must be absolute when provided)
        for (label, value) in [("Base Dir", baseDir), ("Data Dir", dataDir),
                                ("Redo Dir", redoDir), ("Plugin Dir", pluginDir)] {
            if !value.isEmpty && !value.hasPrefix("/") {
                return showValidationError("\(label) must be an absolute path (start with `/`).")
            }
        }

        // 2. Occupancy precheck (only when port is changing)
        let oldPort = readConfigValue("port", fallback: DEFAULT_PORT)
        if port != oldPort, let holder = portInUse(portNum), holder.command != "seekdb" {
            NSApp.activate(ignoringOtherApps: true)
            let alert = NSAlert()
            alert.messageText = "Port \(port) is already in use"
            alert.informativeText = "It is held by \(holder.description). seekdb will not be able to bind to this port. Save anyway?"
            alert.alertStyle = .warning
            alert.addButton(withTitle: "Cancel")
            alert.addButton(withTitle: "Save Anyway")
            if alert.runModal() != .alertSecondButtonReturn {
                statusLabel.stringValue = "Cancelled."
                return
            }
        }

        // 3. Admin password gate
        guard authorizeAdmin(prompt: "seekdb Monitor needs your password to apply new settings and restart the database service.") else {
            statusLabel.stringValue = "Cancelled."
            return
        }

        saveButton.isEnabled = false
        statusLabel.stringValue = "Saving…"

        var args: [String] = ["--port", port]
        if !baseDir.isEmpty { args += ["--base-dir", baseDir] }
        if !dataDir.isEmpty { args += ["--data-dir", dataDir] }
        if !redoDir.isEmpty { args += ["--redo-dir", redoDir] }
        if !pluginDir.isEmpty { args += ["--plugin-dir", pluginDir] }
        args += ["--restart"]

        let logDir = (baseDir.isEmpty ? "/opt/seekdb/var/seekdb/data" : baseDir) + "/log"
        runPrivileged(command: "config", args: args) { [weak self] success, output in
            guard let self = self else { return }
            if !success {
                self.saveButton.isEnabled = true
                self.statusLabel.stringValue = "Failed to apply settings."
                NSApp.activate(ignoringOtherApps: true)
                let alert = NSAlert()
                alert.messageText = "Failed to apply settings"
                alert.informativeText = "Check logs for details:\n\n\(logDir)/seekdb.log\n\(logDir)/launchd.err.log"
                alert.alertStyle = .warning
                alert.runModal()
                return
            }
            // 3. Wait for the new port to actually accept connections.
            self.statusLabel.stringValue = "Restarting (waiting for service)…"
            waitForPort(port, timeout: 30) { ok in
                self.saveButton.isEnabled = true
                if ok {
                    self.statusLabel.stringValue = "Saved and restarted."
                    self.onSaved?()
                    DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                        self.window.close()
                    }
                } else {
                    self.statusLabel.stringValue = "Service did not come up on port \(port)."
                    NSApp.activate(ignoringOtherApps: true)
                    let alert = NSAlert()
                    alert.messageText = "seekdb did not come up on port \(port)"
                    alert.informativeText = "Check logs for details:\n\n\(logDir)/seekdb.log\n\(logDir)/launchd.err.log"
                    alert.alertStyle = .warning
                    alert.runModal()
                }
            }
        }
    }

    private func showValidationError(_ message: String) {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.messageText = "Invalid setting"
        alert.informativeText = message
        alert.alertStyle = .warning
        alert.runModal()
        statusLabel.stringValue = message
    }

    @objc func cancelSettings() {
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
            makeButton("Follow Logs", #selector(SeekDBMenuBarApp.followLogs))
        ])

        addSectionLabel("Configuration")
        addRow([
            makeButton("Settings…", #selector(SeekDBMenuBarApp.openSettings)),
            makeButton("Run Doctor", #selector(SeekDBMenuBarApp.runDoctor))
        ])

        addSectionLabel("Maintenance")
        addRow([
            makeButton("Initialize Database", #selector(SeekDBMenuBarApp.setupService)),
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

    func update(_ status: SeekDBStatus) {
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
        startButton.isEnabled = (status.state == .stopped)
        stopButton.isEnabled = (status.state == .active)
        restartButton.isEnabled = (status.state == .active)
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

    // menu items that update dynamically
    var statusMenuItem: NSMenuItem!
    var portMenuItem: NSMenuItem!
    var startItem: NSMenuItem!
    var stopItem: NSMenuItem!
    var restartItem: NSMenuItem!

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

        let setupItem = NSMenuItem(title: "Initialize Database", action: #selector(setupService), keyEquivalent: "")
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

    func refreshStatus() {
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let status = SeekDBStatus.detect()
            DispatchQueue.main.async {
                guard let self = self else { return }
                self.currentStatus = status
                self.statusItem.button?.image = makeStatusIcon(status.state)
                self.statusMenuItem.title = "seekdb: \(status.summary)"
                self.portMenuItem.title = "Port: \(status.port.isEmpty ? "--" : status.port)"
                self.startItem.isEnabled = (status.state == .stopped)
                self.stopItem.isEnabled = (status.state == .active)
                self.restartItem.isEnabled = (status.state == .active)
                self.mainWindowController.update(status)
                self.scheduleNextPoll()
            }
        }
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

        guard currentBundleInTrash || installedBundleMissing else { return }

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
            let logDir = readConfigValue("base-dir", fallback: "/opt/seekdb/var/seekdb/data") + "/log"
            alert.informativeText = "Check logs for details:\n\n\(logDir)/seekdb.log\n\(logDir)/launchd.err.log"
            alert.alertStyle = .critical
        }
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
        guard authorizeAdmin(prompt: "seekdb Monitor needs your password to start the database service.") else { return }
        statusItem.button?.image = makeStatusIcon(.starting)
        statusMenuItem.title = "seekdb: Starting…"
        runPrivileged(command: "start") { [weak self] success, output in
            if !success { self?.showResult(success: false, output: output, title: "Start Failed") }
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) { self?.refreshStatus() }
        }
    }

    @objc func stopService() {
        guard authorizeAdmin(prompt: "seekdb Monitor needs your password to stop the database service.") else { return }
        statusItem.button?.image = makeStatusIcon(.stopping)
        statusMenuItem.title = "seekdb: Stopping…"
        runPrivileged(command: "stop") { [weak self] success, output in
            if !success { self?.showResult(success: false, output: output, title: "Stop Failed") }
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) { self?.refreshStatus() }
        }
    }

    @objc func restartService() {
        guard authorizeAdmin(prompt: "seekdb Monitor needs your password to restart the database service.") else { return }
        statusItem.button?.image = makeStatusIcon(.starting)
        statusMenuItem.title = "seekdb: Restarting…"
        runPrivileged(command: "restart") { [weak self] success, output in
            if !success { self?.showResult(success: false, output: output, title: "Restart Failed") }
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) { self?.refreshStatus() }
        }
    }

    // MARK: - Logs

    @objc func viewLogs() {
        let logDir = readConfigValue("base-dir", fallback: "/opt/seekdb/var/seekdb/data") + "/log"
        openTerminal("tail -n 200 \(logDir)/seekdb.log \(logDir)/launchd.out.log \(logDir)/launchd.err.log 2>/dev/null; echo '\\nPress any key to close'; read -n1")
    }

    @objc func followLogs() {
        let logDir = readConfigValue("base-dir", fallback: "/opt/seekdb/var/seekdb/data") + "/log"
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
        let baseDir = readConfigValue("base-dir", fallback: "/opt/seekdb/var/seekdb/data")
        let logDir = baseDir + "/log"
        let port = readConfigValue("port", fallback: "2881")
        let script = """
        echo 'seekdb diagnostics'
        echo '------------------'
        test -x /opt/seekdb/bin/seekdb && echo 'binary     : ok' || echo 'binary     : missing'
        test -f \(SEEKDB_CONFIG) && echo 'config     : ok' || echo 'config     : missing'
        test -d \(baseDir) && echo 'base dir   : ok' || echo 'base dir   : missing'
        test -d \(logDir) && echo 'log dir    : ok' || echo 'log dir    : missing'
        nc -z 127.0.0.1 \(port) 2>/dev/null && echo 'port       : open (\(port))' || echo 'port       : closed (\(port))'
        pgrep -f /opt/seekdb/bin/seekdb >/dev/null && echo 'process    : running' || echo 'process    : not running'
        echo 'disk       :'
        df -h \(baseDir) 2>/dev/null || df -h /opt/seekdb 2>/dev/null
        echo 'memory     :' $(( $(sysctl -n hw.memsize 2>/dev/null) / 1024 / 1024 )) MB
        echo '\\nPress any key to close'; read -n1
        """
        openTerminal(script)
    }

    // MARK: - Initialize / Dangerous Actions

    @objc func setupService() {
        guard confirmAction(
            message: "Initialize Database?",
            info: "This will erase all database data and bootstrap a fresh instance.\nConfiguration and plugins will be preserved.\n\nThis cannot be undone."
        ) else { return }
        guard authorizeAdmin(prompt: "seekdb Monitor needs your password to initialize the database. All current data will be erased.") else { return }
        statusItem.button?.image = makeStatusIcon(.starting)
        runPrivileged(command: "initialize") { [weak self] success, output in
            self?.showResult(success: success, output: output, title: "Initialize Database")
            self?.refreshStatus()
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
