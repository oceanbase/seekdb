// Copyright (c) 2025 OceanBase.
// Licensed under the Apache License, Version 2.0.
//
// Privileged helper daemon for SeekDB Monitor.
// Runs as root via LaunchDaemon, accepts XPC commands from the menu bar app.
// Compile: swiftc -o SeekDBHelper -framework Foundation SeekDBHelper.swift

import Foundation

let SEEKDBCTL = "/opt/homebrew/bin/seekdbctl"

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
        let allowed = ["start", "stop", "restart", "setup", "config", "clean-data", "uninstall"]
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
            reply(proc.terminationStatus == 0, output)
        } catch {
            reply(false, error.localizedDescription)
        }
    }
}

let delegate = Helper()
let listener = NSXPCListener(machServiceName: "com.seekdb.helper")
listener.delegate = delegate
listener.resume()
RunLoop.current.run()
