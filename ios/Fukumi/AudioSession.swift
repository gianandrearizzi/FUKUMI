import AVFoundation

/// The app's audio session: playing and recording at once, at 48 kHz, with a short buffer.
/// The page's audio (WebKit) goes through this same session.
enum AudioSession {
    static func configure() {
        let session = AVAudioSession.sharedInstance()
        do {
            try session.setCategory(.playAndRecord, mode: .default,
                                    options: [.defaultToSpeaker, .allowBluetoothA2DP, .allowAirPlay])
            try session.setPreferredSampleRate(48_000)
            try session.setPreferredIOBufferDuration(0.005)
            try session.setActive(true)
        } catch {
            NSLog("Fukumi: audio session: %@", String(describing: error))
        }
    }

    /// After a phone call, Siri or another app took the audio.
    static func reactivate() {
        do { try AVAudioSession.sharedInstance().setActive(true) } catch {
            NSLog("Fukumi: audio session: %@", String(describing: error))
        }
    }
}
