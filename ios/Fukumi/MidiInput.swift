import CoreMIDI
import Foundation

/// Listens to every MIDI source of the iPad (USB, network, other apps) through CoreMIDI,
/// and hands the channel messages to the main thread in batches.
final class MidiInput: @unchecked Sendable {
    /// On the main thread: [status, data1, data2] for each channel message, in order.
    var onMessages: (([[UInt8]]) -> Void)?
    /// On the main thread: the names of the connected sources, when they change.
    var onSourcesChanged: (([String]) -> Void)?
    private(set) var sourceNames: [String] = []

    private var client = MIDIClientRef()
    private var port = MIDIPortRef()
    private var connected = Set<MIDIEndpointRef>()
    private let lock = NSLock()
    private var queue: [[UInt8]] = []
    private var flushScheduled = false

    func start() {
        let created = MIDIClientCreateWithBlock("Fukumi" as CFString, &client) { [weak self] notification in
            switch notification.pointee.messageID {
            case .msgSetupChanged, .msgObjectAdded, .msgObjectRemoved:
                DispatchQueue.main.async { self?.connectSources() }
            default:
                break
            }
        }
        guard created == noErr else {
            NSLog("Fukumi: MIDI client error %d", Int(created))
            return
        }
        let opened = MIDIInputPortCreateWithProtocol(client, "Fukumi input" as CFString, ._1_0, &port) { [weak self] list, _ in
            self?.receive(list)
        }
        guard opened == noErr else {
            NSLog("Fukumi: MIDI port error %d", Int(opened))
            return
        }
        connectSources()
    }

    /// On the main thread. New sources are connected; the ones gone are forgotten.
    private func connectSources() {
        var names: [String] = []
        var present = Set<MIDIEndpointRef>()
        for index in 0..<MIDIGetNumberOfSources() {
            let source = MIDIGetSource(index)
            guard source != 0 else { continue }
            present.insert(source)
            if !connected.contains(source), MIDIPortConnectSource(port, source, nil) == noErr {
                connected.insert(source)
            }
            names.append(Self.displayName(of: source))
        }
        connected.formIntersection(present)
        sourceNames = names
        onSourcesChanged?(names)
    }

    private static func displayName(of object: MIDIObjectRef) -> String {
        var name: Unmanaged<CFString>?
        if MIDIObjectGetStringProperty(object, kMIDIPropertyDisplayName, &name) == noErr, let name {
            return name.takeRetainedValue() as String
        }
        return "MIDI"
    }

    // MARK: Receiving (CoreMIDI's thread)

    private func receive(_ list: UnsafePointer<MIDIEventList>) {
        var messages: [[UInt8]] = []
        for packet in list.unsafeSequence() {
            let count = min(Int(packet.pointee.wordCount), 64)
            withUnsafeBytes(of: packet.pointee.words) { raw in
                var index = 0
                while index < count {
                    let word = raw.load(fromByteOffset: index * 4, as: UInt32.self)
                    let type = word >> 28
                    if type == 0x2 {  // MIDI 1.0 channel voice message, one word
                        messages.append([UInt8((word >> 16) & 0xFF), UInt8((word >> 8) & 0x7F), UInt8(word & 0x7F)])
                    }
                    index += Self.wordsInMessage(type: type)
                }
            }
        }
        guard !messages.isEmpty else { return }
        lock.lock()
        queue.append(contentsOf: messages)
        let schedule = !flushScheduled
        flushScheduled = true
        lock.unlock()
        if schedule {
            DispatchQueue.main.async { [weak self] in self?.flush() }
        }
    }

    private func flush() {
        lock.lock()
        let batch = queue
        queue.removeAll(keepingCapacity: true)
        flushScheduled = false
        lock.unlock()
        onMessages?(Self.coalesce(batch))
    }

    /// A knob turned fast sends many values: in one batch each controller keeps only its last value.
    static func coalesce(_ batch: [[UInt8]]) -> [[UInt8]] {
        var out: [[UInt8]] = []
        var slot: [UInt16: Int] = [:]
        for message in batch {
            if message[0] & 0xF0 == 0xB0 {
                let key = UInt16(message[0]) << 8 | UInt16(message[1])
                if let index = slot[key] {
                    out[index] = message
                    continue
                }
                slot[key] = out.count
            }
            out.append(message)
        }
        return out
    }

    /// Universal MIDI Packet sizes, by message type.
    private static func wordsInMessage(type: UInt32) -> Int {
        switch type {
        case 0x0, 0x1, 0x2, 0x6, 0x7: return 1
        case 0x3, 0x4, 0x8, 0x9, 0xA: return 2
        case 0xB, 0xC: return 3
        default: return 4
        }
    }
}
