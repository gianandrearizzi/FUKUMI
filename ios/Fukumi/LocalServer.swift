import Foundation
import Network

/// A small web server for the instrument's page, reachable only from this iPad (loopback).
/// The page needs an http://localhost origin: there WebKit lets it use the microphone and
/// the audio worklet. The port is fixed, so the page's saved data keeps the same origin.
final class LocalServer: @unchecked Sendable {
    static let port: UInt16 = 47816

    private let root: URL
    private let queue = DispatchQueue(label: "fukumi.server")
    private var listener: NWListener?   // touched only on `queue`
    private var onReady: ((URL) -> Void)?
    private var retries = 0

    init(root: URL) {
        self.root = root.standardizedFileURL
    }

    /// Starts listening; `ready` runs on the main thread with the page's address.
    func start(ready: @escaping (URL) -> Void) {
        queue.async {
            self.onReady = ready
            self.listen()
        }
    }

    /// iOS may close the listening socket while the app is in the background.
    func ensureRunning() {
        queue.async {
            switch self.listener?.state {
            case .some(.failed), .some(.cancelled), .none:
                self.listen()
            default:
                break
            }
        }
    }

    private func listen() {
        self.listener?.cancel()
        self.listener = nil
        let parameters = NWParameters.tcp
        parameters.requiredInterfaceType = .loopback
        parameters.allowLocalEndpointReuse = true
        guard let port = NWEndpoint.Port(rawValue: Self.port),
              let listener = try? NWListener(using: parameters, on: port) else {
            NSLog("Fukumi: the server cannot open port %d", Int(Self.port))
            return
        }
        listener.newConnectionHandler = { [weak self] connection in self?.serve(connection) }
        listener.stateUpdateHandler = { [weak self] state in
            guard let self else { return }
            switch state {
            case .ready:
                self.retries = 0
                if let ready = self.onReady {
                    self.onReady = nil
                    let url = URL(string: "http://localhost:\(Self.port)/index.html")!
                    DispatchQueue.main.async { ready(url) }
                }
            case .failed(let error):
                NSLog("Fukumi: server: %@", String(describing: error))
                listener.cancel()
                if self.retries < 5 {  // the port may still be held for a moment: try again
                    self.retries += 1
                    self.queue.asyncAfter(deadline: .now() + 0.5) { self.listen() }
                }
            default:
                break
            }
        }
        listener.start(queue: queue)
        self.listener = listener
    }

    // MARK: HTTP

    private func serve(_ connection: NWConnection) {
        connection.start(queue: queue)
        receive(on: connection, buffer: Data())
    }

    private func receive(on connection: NWConnection, buffer: Data) {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 16_384) { [weak self] data, _, isComplete, error in
            guard let self else { connection.cancel(); return }
            var received = buffer
            if let data { received.append(data) }
            if let end = received.range(of: Data("\r\n\r\n".utf8)) {
                let head = String(decoding: received[received.startIndex..<end.lowerBound], as: UTF8.self)
                self.respond(on: connection, head: head)
            } else if error != nil || isComplete || received.count > 65_536 {
                connection.cancel()
            } else {
                self.receive(on: connection, buffer: received)
            }
        }
    }

    private func respond(on connection: NWConnection, head: String) {
        let requestLine = head.components(separatedBy: "\r\n").first ?? ""
        let parts = requestLine.split(separator: " ")
        guard parts.count >= 2 else {
            send(connection, status: "400 Bad Request", body: Data())
            return
        }
        let method = String(parts[0])
        guard method == "GET" || method == "HEAD" else {
            send(connection, status: "405 Method Not Allowed", body: Data())
            return
        }
        var path = String(parts[1])
        if let cut = path.firstIndex(where: { $0 == "?" || $0 == "#" }) { path = String(path[..<cut]) }
        path = path.removingPercentEncoding ?? path
        if path.hasSuffix("/") { path += "index.html" }
        let file = root.appendingPathComponent(String(path.drop(while: { $0 == "/" }))).standardizedFileURL
        // Only files inside the page's folder.
        guard file.path.hasPrefix(root.path + "/"), let body = try? Data(contentsOf: file) else {
            send(connection, status: "404 Not Found", body: Data("Not found".utf8))
            return
        }
        send(connection, status: "200 OK", type: Self.mimeType(file.pathExtension), body: body, headOnly: method == "HEAD")
    }

    private func send(_ connection: NWConnection, status: String, type: String = "text/plain; charset=utf-8",
                      body: Data, headOnly: Bool = false) {
        let header = "HTTP/1.1 \(status)\r\nContent-Type: \(type)\r\nContent-Length: \(body.count)\r\n"
            + "Cache-Control: no-cache\r\nConnection: close\r\n\r\n"
        var out = Data(header.utf8)
        if !headOnly { out.append(body) }
        connection.send(content: out, completion: .contentProcessed { _ in connection.cancel() })
    }

    static func mimeType(_ ext: String) -> String {
        switch ext.lowercased() {
        case "html": return "text/html; charset=utf-8"
        case "js", "mjs": return "text/javascript; charset=utf-8"
        case "css": return "text/css; charset=utf-8"
        case "wasm": return "application/wasm"
        case "json": return "application/json"
        case "webmanifest": return "application/manifest+json"
        case "png": return "image/png"
        case "svg": return "image/svg+xml"
        case "woff2": return "font/woff2"
        case "md", "txt": return "text/plain; charset=utf-8"
        default: return "application/octet-stream"
        }
    }
}
