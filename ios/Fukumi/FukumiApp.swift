import SwiftUI
import UIKit

@main
struct FukumiApp: App {
    @UIApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate

    var body: some Scene {
        WindowGroup {
            InstrumentView()
                .ignoresSafeArea()
                .background(Color("Paper"))
                .statusBarHidden(true)
                .persistentSystemOverlays(.hidden)
        }
    }
}

final class AppDelegate: NSObject, UIApplicationDelegate {
    func application(_ application: UIApplication,
                     didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]? = nil) -> Bool {
        AudioSession.configure()
        // A live instrument: the screen stays on while the app is open.
        application.isIdleTimerDisabled = true
        return true
    }
}
