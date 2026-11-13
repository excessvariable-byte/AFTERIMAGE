import AppKit
import Carbon.HIToolbox
import Foundation

private struct Result { let title, path, type, passage: String; let confidence: Int }

private final class ResultCell: NSTableCellView {
    let title = NSTextField(labelWithString: "")
    let detail = NSTextField(labelWithString: "")
    let kind = NSTextField(labelWithString: "")
    override init(frame: NSRect) {
        super.init(frame: frame); wantsLayer = true; layer?.cornerRadius = 10
        title.font = .systemFont(ofSize: 14, weight: .semibold); detail.font = .systemFont(ofSize: 11)
        title.textColor = .labelColor; detail.textColor = .secondaryLabelColor
        kind.font = .monospacedSystemFont(ofSize: 10, weight: .medium)
        kind.textColor = NSColor(red: 0.73, green: 0.94, blue: 0.4, alpha: 1); kind.alignment = .right
        [title, detail, kind].forEach { $0.translatesAutoresizingMaskIntoConstraints = false; $0.lineBreakMode = .byTruncatingTail; addSubview($0) }
        NSLayoutConstraint.activate([
            title.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 16), title.topAnchor.constraint(equalTo: topAnchor, constant: 10), title.trailingAnchor.constraint(equalTo: kind.leadingAnchor, constant: -10),
            detail.leadingAnchor.constraint(equalTo: title.leadingAnchor), detail.topAnchor.constraint(equalTo: title.bottomAnchor, constant: 5), detail.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -16),
            kind.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -16), kind.centerYAnchor.constraint(equalTo: title.centerYAnchor)
        ])
    }
    required init?(coder: NSCoder) { nil }
    override var backgroundStyle: NSView.BackgroundStyle { didSet { layer?.backgroundColor = backgroundStyle == .emphasized ? NSColor.white.withAlphaComponent(0.11).cgColor : NSColor.clear.cgColor } }
    func set(_ result: Result) { title.stringValue = result.title; detail.stringValue = result.passage.replacingOccurrences(of: "\n", with: " ").isEmpty ? result.path : result.passage.replacingOccurrences(of: "\n", with: " "); kind.stringValue = "\(result.type.uppercased())  \(result.confidence)%" }
}

private final class Target: NSObject { let block: () -> Void; init(_ block: @escaping () -> Void) { self.block = block }; @objc func invoke() { block() } }

private final class Palette: NSObject, NSSearchFieldDelegate, NSTableViewDataSource, NSTableViewDelegate {
    let panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 760, height: 82), styleMask: [.borderless, .fullSizeContentView], backing: .buffered, defer: false)
    let field = NSSearchField(), hint = NSTextField(labelWithString: "Search your memory with meaning, dates, formats, and phrases")
    let table = NSTableView(), scroll = NSScrollView(); var results: [Result] = []; var pending: DispatchWorkItem?
    let compact: CGFloat = 82, expanded: CGFloat = 430
    override init() { super.init(); configure() }
    private func button(_ symbol: String, _ tip: String, _ action: @escaping () -> Void) -> NSButton {
        let b = NSButton(image: NSImage(systemSymbolName: symbol, accessibilityDescription: tip)!, target: nil, action: nil)
        b.bezelStyle = .texturedRounded; b.contentTintColor = .secondaryLabelColor; b.toolTip = tip; b.translatesAutoresizingMaskIntoConstraints = false
        b.widthAnchor.constraint(equalToConstant: 28).isActive = true; b.heightAnchor.constraint(equalToConstant: 28).isActive = true
        let target = Target(action); b.target = target; b.action = #selector(Target.invoke); objc_setAssociatedObject(b, Unmanaged.passUnretained(b).toOpaque(), target, .OBJC_ASSOCIATION_RETAIN_NONATOMIC); return b
    }
    private func configure() {
        panel.isFloatingPanel = true; panel.level = .statusBar; panel.isOpaque = false; panel.backgroundColor = .clear; panel.hasShadow = true; panel.hidesOnDeactivate = false; panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        let backdrop = NSVisualEffectView(); backdrop.material = .hudWindow; backdrop.blendingMode = .behindWindow; backdrop.state = .active; backdrop.wantsLayer = true; backdrop.layer?.cornerRadius = 18; backdrop.layer?.borderWidth = 1; backdrop.layer?.borderColor = NSColor.white.withAlphaComponent(0.14).cgColor; panel.contentView = backdrop
        let icon = NSImageView(image: NSImage(systemSymbolName: "magnifyingglass", accessibilityDescription: nil)!); icon.contentTintColor = NSColor(red: 0.78, green: 0.96, blue: 0.4, alpha: 1); icon.translatesAutoresizingMaskIntoConstraints = false
        field.placeholderString = "Search files, memories, dates, or formats"; field.font = .systemFont(ofSize: 18); field.focusRingType = .none; field.isBordered = false; field.drawsBackground = false; field.delegate = self; field.translatesAutoresizingMaskIntoConstraints = false
        let app = button("arrow.up.forward.app", "Open full Afterimage") { [weak self] in self?.openWeb(nil) }
        let library = button("books.vertical", "Open Library") { [weak self] in self?.openWeb("library") }
        let console = button("slider.horizontal.3", "Open Developer Console") { [weak self] in self?.openWeb("developer") }
        let header = NSView(); header.translatesAutoresizingMaskIntoConstraints = false; [icon, field, app, library, console].forEach(header.addSubview)
        NSLayoutConstraint.activate([icon.leadingAnchor.constraint(equalTo: header.leadingAnchor, constant: 22), icon.centerYAnchor.constraint(equalTo: header.centerYAnchor), icon.widthAnchor.constraint(equalToConstant: 18), icon.heightAnchor.constraint(equalToConstant: 18), field.leadingAnchor.constraint(equalTo: icon.trailingAnchor, constant: 14), field.centerYAnchor.constraint(equalTo: header.centerYAnchor), console.trailingAnchor.constraint(equalTo: header.trailingAnchor, constant: -16), console.centerYAnchor.constraint(equalTo: header.centerYAnchor), library.trailingAnchor.constraint(equalTo: console.leadingAnchor, constant: -6), library.centerYAnchor.constraint(equalTo: header.centerYAnchor), app.trailingAnchor.constraint(equalTo: library.leadingAnchor, constant: -6), app.centerYAnchor.constraint(equalTo: header.centerYAnchor), field.trailingAnchor.constraint(equalTo: app.leadingAnchor, constant: -10)])
        hint.font = .systemFont(ofSize: 11, weight: .medium); hint.textColor = .secondaryLabelColor; hint.translatesAutoresizingMaskIntoConstraints = false
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("result")); column.width = 720; table.addTableColumn(column); table.headerView = nil; table.rowHeight = 62; table.intercellSpacing = NSSize(width: 0, height: 5); table.backgroundColor = .clear; table.selectionHighlightStyle = .regular; table.delegate = self; table.dataSource = self
        scroll.documentView = table; scroll.hasVerticalScroller = true; scroll.drawsBackground = false; scroll.translatesAutoresizingMaskIntoConstraints = false
        [header, hint, scroll].forEach(backdrop.addSubview)
        NSLayoutConstraint.activate([header.topAnchor.constraint(equalTo: backdrop.topAnchor, constant: 7), header.leadingAnchor.constraint(equalTo: backdrop.leadingAnchor), header.trailingAnchor.constraint(equalTo: backdrop.trailingAnchor), header.heightAnchor.constraint(equalToConstant: 54), hint.leadingAnchor.constraint(equalTo: backdrop.leadingAnchor, constant: 24), hint.topAnchor.constraint(equalTo: header.bottomAnchor, constant: 2), scroll.topAnchor.constraint(equalTo: hint.bottomAnchor, constant: 12), scroll.leadingAnchor.constraint(equalTo: backdrop.leadingAnchor, constant: 12), scroll.trailingAnchor.constraint(equalTo: backdrop.trailingAnchor, constant: -8), scroll.bottomAnchor.constraint(equalTo: backdrop.bottomAnchor, constant: -13)])
    }
    var visible: Bool { panel.isVisible }
    func show() { guard let screen = NSScreen.main else { return }; let box = screen.visibleFrame; panel.setFrameOrigin(NSPoint(x: box.midX - 380, y: box.maxY - 165)); panel.alphaValue = 0; panel.makeKeyAndOrderFront(nil); NSAnimationContext.runAnimationGroup { $0.duration = 0.18; panel.animator().alphaValue = 1 }; NSApp.activate(ignoringOtherApps: true); panel.makeFirstResponder(field) }
    func hide() { pending?.cancel(); panel.orderOut(nil) }
    func controlTextDidChange(_ notification: Notification) { pending?.cancel(); let q = field.stringValue.trimmingCharacters(in: .whitespacesAndNewlines); guard !q.isEmpty else { results = []; table.reloadData(); hint.stringValue = "Search your memory with meaning, dates, formats, and phrases"; resize(false); return }; hint.stringValue = "Searching locally…"; let work = DispatchWorkItem { [weak self] in self?.search(q) }; pending = work; DispatchQueue.main.asyncAfter(deadline: .now() + 0.16, execute: work) }
    private func search(_ query: String) {
        var request = URLRequest(url: URL(string: "http://127.0.0.1:8765/api/search")!); request.httpMethod = "POST"; request.setValue("application/json", forHTTPHeaderField: "Content-Type"); request.httpBody = try? JSONSerialization.data(withJSONObject: ["query": query, "filters": ["date_range": "all", "file_type": "all"]])
        URLSession.shared.dataTask(with: request) { [weak self] data, _, _ in guard let self, let data, let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any], let items = root["results"] as? [[String: Any]] else { return }; let mapped = items.map { Result(title: $0["title"] as? String ?? "Untitled", path: $0["file_path"] as? String ?? "", type: $0["file_type"] as? String ?? "file", passage: $0["passage"] as? String ?? "", confidence: $0["confidence_pct"] as? Int ?? 0) }; DispatchQueue.main.async { guard self.field.stringValue.trimmingCharacters(in: .whitespacesAndNewlines) == query else { return }; self.results = mapped; self.table.reloadData(); self.hint.stringValue = mapped.isEmpty ? "No matching memories" : "\(mapped.count) locally ranked results · Click to open · Esc to close"; self.resize(true) } }.resume()
    }
    private func resize(_ showResults: Bool) { let target = showResults ? expanded : compact; guard abs(panel.frame.height - target) > 1 else { return }; var box = panel.frame; box.origin.y -= target - box.height; box.size.height = target; NSAnimationContext.runAnimationGroup { context in context.duration = 0.18; context.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut); panel.animator().setFrame(box, display: true) } }
    private func openWeb(_ view: String?) { NSWorkspace.shared.open(URL(string: "http://127.0.0.1:8765/\(view.map { "?view=\($0)" } ?? "")")!); hide() }
    func numberOfRows(in tableView: NSTableView) -> Int { results.count }
    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? { let id = NSUserInterfaceItemIdentifier("AfterimageResult"); let cell = tableView.makeView(withIdentifier: id, owner: self) as? ResultCell ?? ResultCell(); cell.identifier = id; cell.set(results[row]); return cell }
    func tableViewSelectionDidChange(_ notification: Notification) { let row = table.selectedRow; guard row >= 0, row < results.count else { return }; NSWorkspace.shared.open(URL(fileURLWithPath: results[row].path)); hide() }
}

private func hotKeyHandler(_ next: EventHandlerCallRef?, _ event: EventRef?, _ data: UnsafeMutableRawPointer?) -> OSStatus { guard let data else { return noErr }; let launcher = Unmanaged<AfterimageLauncher>.fromOpaque(data).takeUnretainedValue(); DispatchQueue.main.async { launcher.toggle() }; return noErr }

final class AfterimageLauncher: NSObject, NSApplicationDelegate {
    private let palette = Palette(); var hotKey: EventHotKeyRef?; private var serverProcess: Process?
    func applicationDidFinishLaunching(_ notification: Notification) { startServer(); var spec = EventTypeSpec(eventClass: OSType(kEventClassKeyboard), eventKind: UInt32(kEventHotKeyPressed)); InstallEventHandler(GetApplicationEventTarget(), hotKeyHandler, 1, &spec, Unmanaged.passUnretained(self).toOpaque(), nil); var id = EventHotKeyID(signature: OSType(0x41465452), id: 1); RegisterEventHotKey(UInt32(kVK_Space), UInt32(optionKey), id, GetApplicationEventTarget(), 0, &hotKey) }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }
    func toggle() { palette.visible ? palette.hide() : palette.show() }
    private func startServer() { guard let root = Bundle.main.object(forInfoDictionaryKey: "AfterimageRoot") as? String else { return }; let task = Process(); task.executableURL = URL(fileURLWithPath: "/usr/bin/env"); task.arguments = ["python3", URL(fileURLWithPath: root).appendingPathComponent("web/server.py").path]; task.currentDirectoryURL = URL(fileURLWithPath: root); task.standardOutput = FileHandle.nullDevice; task.standardError = FileHandle.nullDevice; try? task.run(); serverProcess = task }
}

let app = NSApplication.shared; let delegate = AfterimageLauncher(); app.delegate = delegate; app.setActivationPolicy(.accessory); app.run()
