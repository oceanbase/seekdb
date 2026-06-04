// Copyright (c) 2025 OceanBase.
// Licensed under the Apache License, Version 2.0.
//
// Privileged helper daemon for seekdb Monitor.
// Runs as root via LaunchDaemon, accepts XPC commands from the menu bar app.
// Compile: swiftc -o seekdb-helper -framework Foundation SeekDBHelper.swift

import Foundation

let SEEKDBCTL = "/opt/seekdb/bin/seekdbctl"
let HELPER_LABEL = "com.seekdb.helper"
let HELPER_PLIST = "/Library/LaunchDaemons/com.seekdb.helper.plist"
let HELPER_TOOL = "/Library/PrivilegedHelperTools/com.seekdb.helper"

@objc(SeekDBHelperProtocol)
protocol SeekDBHelperProtocol {
    func execute(command: String, args: [String], withReply reply: @escaping (Bool, String) -> Void)
}

class Helper: NSObject, SeekDBHelperProtocol, NSXPCListenerDelegate {

    func listener(_ listener: NSXPCListener, shouldAcceptNewConnection connection: NSXPCConnection) -> Bool {
        connection.exportedInterface = NSXPCInterface(with: SeekDBHelperProtocol.self)
        connection.exportedObject = self
        connection.resume()
        return true
    }

    func execute(command: String, args: [String], withReply reply: @escaping (Bool, String) -> Void) {
        let allowed = ["start", "stop", "restart", "setup", "initialize", "config", "enable-boot", "disable-boot", "uninstall"]
        guard allowed.contains(command) else {
            reply(false, "Command not allowed: \(command)")
            return
        }

        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: SEEKDBCTL)
        proc.arguments = [command] + args
        let pipe = Pipe()
        proc.standardOutput = pipe
        proc.standardError = pipe
        do {
            try proc.run()
            proc.waitUntilExit()
            let data = pipe.fileHandleForReading.readDataToEndOfFile()
            let output = String(data: data, encoding: .utf8) ?? ""
            let success = proc.terminationStatus == 0
            reply(success, output)
            if success && command == "uninstall" {
                scheduleSelfRemoval()
            }
        } catch {
            reply(false, error.localizedDescription)
        }
    }

    private func scheduleSelfRemoval() {
        let script = """
        (
          /bin/sleep 2
          /bin/rm -f '\(HELPER_PLIST)' '\(HELPER_TOOL)'
          /bin/launchctl bootout system/\(HELPER_LABEL) >/dev/null 2>&1 || true
        ) >/dev/null 2>&1 &
        """

        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: "/bin/sh")
        proc.arguments = ["-c", script]
        do {
            try proc.run()
        } catch {
            NSLog("Failed to schedule helper self-removal: %@", error.localizedDescription)
        }
    }
}

let delegate = Helper()
let listener = NSXPCListener(machServiceName: "com.seekdb.helper")
listener.delegate = delegate
listener.resume()
RunLoop.current.run()
