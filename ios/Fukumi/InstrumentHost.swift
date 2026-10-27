import AVFoundation
import SwiftUI
import UIKit
import WebKit

/// The instrument's page, full screen, served from inside the app.
struct InstrumentView: UIViewRepresentable {
    func makeCoordinator() -> InstrumentHost {
        InstrumentHost(pageFolder: Bundle.main.resourceURL!.appendingPathComponent("app", isDirectory: true))
    }

    func makeUIView(context: Context) -> WKWebView { context.coordinator.webView }

    func updateUIView(_ webView: WKWebView, context: Context) {}
}

/// Holds the web view, the local server and the MIDI input, and joins them:
/// MIDI from CoreMIDI reaches the page as window.fukumiMidi([[status, data1, data2], ...]).
@MainActor
final class InstrumentHost: NSObject, WKUIDelegate, WKNavigationDelegate, WKScriptMessageHandler {
    let webView: WKWebView
    private let server: LocalServer
    private let midi = MidiInput()
    private var observers: [NSObjectProtocol] = []

    init(pageFolder: URL) {
        server = LocalServer(root: pageFolder)

        let contents = WKUserContentController()
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        // The page knows it runs inside the app before any of its own scripts start.
        contents.addUserScript(WKUserScript(
            source: "window.fukumiNative = { platform: 'ios', version: '\(version)' };",
            injectionTime: .atDocumentStart, forMainFrameOnly: true))

        let config = WKWebViewConfiguration()
        config.userContentController = contents
        config.allowsInlineMediaPlayback = true
        config.mediaTypesRequiringUserActionForPlayback = []
        // Settings and recorded loops stay between launches (same origin: http://localhost:47816).
        config.websiteDataStore = .default()

        webView = WKWebView(frame: .zero, configuration: config)
        super.init()

        contents.add(WeakScriptHandler(self), name: "fukumi")
        webView.uiDelegate = self
        webView.navigationDelegate = self
        webView.isOpaque = false
        webView.backgroundColor = UIColor(named: "Paper")
        webView.scrollView.backgroundColor = UIColor(named: "Paper")
        webView.scrollView.bounces = false
        webView.scrollView.contentInsetAdjustmentBehavior = .never
        webView.allowsLinkPreview = false
        if #available(iOS 16.4, *) {
            // Safari on a Mac can inspect the page (menu Sviluppo), useful while developing.
            webView.isInspectable = true
        }

        midi.onMessages = { [weak self] messages in self?.deliver(messages) }
        midi.onSourcesChanged = { [weak self] names in self?.sendSources(names) }
        midi.start()

        server.start { [weak self] url in
            self?.webView.load(URLRequest(url: url))
        }
        observeLifecycle()
    }

    // MARK: Microphone

    func webView(_ webView: WKWebView, requestMediaCapturePermissionFor origin: WKSecurityOrigin,
                 initiatedByFrame frame: WKFrameInfo, type: WKMediaCaptureType,
                 decisionHandler: @escaping (WKPermissionDecision) -> Void) {
        // Only the instrument's own page asks. iOS still asks the player once (Info.plist).
        decisionHandler(origin.host == "localhost" ? .grant : .deny)
    }

    // MARK: Page

    func webViewWebContentProcessDidTerminate(_ webView: WKWebView) {
        // iOS may close the page to free memory: it reloads, with its settings and loops saved.
        server.ensureRunning()
        webView.reload()
    }

    func userContentController(_ controller: WKUserContentController, didReceive message: WKScriptMessage) {
        guard let body = message.body as? [String: Any], let kind = body["t"] as? String else { return }
        switch kind {
        case "midiSources": sendSources(midi.sourceNames)
        default: break
        }
    }

    // MARK: MIDI to the page

    private func deliver(_ messages: [[UInt8]]) {
        guard !messages.isEmpty else { return }
        let list = messages.map { "[" + $0.map { String($0) }.joined(separator: ",") + "]" }.joined(separator: ",")
        webView.evaluateJavaScript("window.fukumiMidi && window.fukumiMidi([\(list)])", completionHandler: nil)
    }

    private func sendSources(_ names: [String]) {
        guard let data = try? JSONSerialization.data(withJSONObject: names),
              let json = String(data: data, encoding: .utf8) else { return }
        webView.evaluateJavaScript("window.fukumiMidiSources && window.fukumiMidiSources(\(json))", completionHandler: nil)
    }

    // MARK: Coming back

    private func observeLifecycle() {
        let center = NotificationCenter.default
        observers.append(center.addObserver(forName: UIApplication.didBecomeActiveNotification,
                                            object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in self?.resume() }
        })
        observers.append(center.addObserver(forName: AVAudioSession.interruptionNotification,
                                            object: nil, queue: .main) { [weak self] note in
            let raw = note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt
            guard raw.flatMap(AVAudioSession.InterruptionType.init(rawValue:)) == .ended else { return }
            Task { @MainActor in self?.resume() }
        })
    }

    private func resume() {
        server.ensureRunning()
        AudioSession.reactivate()
        webView.evaluateJavaScript("window.fukumiResume && window.fukumiResume()", completionHandler: nil)
    }
}

/// The content controller keeps its handlers alive: this one points back without holding on.
private final class WeakScriptHandler: NSObject, WKScriptMessageHandler {
    weak var target: WKScriptMessageHandler?

    init(_ target: WKScriptMessageHandler) {
        self.target = target
        super.init()
    }

    func userContentController(_ controller: WKUserContentController, didReceive message: WKScriptMessage) {
        target?.userContentController(controller, didReceive: message)
    }
}
