/* SPDX-License-Identifier: BSD-3-Clause */

import Foundation
import SystemExtensions

@MainActor
final class DriverManager: NSObject, ObservableObject {
    static let driverIdentifier = "org.opentimeserver.timecard.macos.driver"

    @Published private(set) var status = "Driver is not activated"
    @Published private(set) var requestInProgress = false

    func activate() {
        requestInProgress = true
        status = "Requesting driver activation"
        let request = OSSystemExtensionRequest.activationRequest(
            forExtensionWithIdentifier: Self.driverIdentifier,
            queue: .main
        )
        request.delegate = self
        OSSystemExtensionManager.shared.submitRequest(request)
    }

    func deactivate() {
        requestInProgress = true
        status = "Requesting driver removal"
        let request = OSSystemExtensionRequest.deactivationRequest(
            forExtensionWithIdentifier: Self.driverIdentifier,
            queue: .main
        )
        request.delegate = self
        OSSystemExtensionManager.shared.submitRequest(request)
    }
}

extension DriverManager: OSSystemExtensionRequestDelegate {
    nonisolated func request(
        _ request: OSSystemExtensionRequest,
        actionForReplacingExtension existing: OSSystemExtensionProperties,
        withExtension extension: OSSystemExtensionProperties
    ) -> OSSystemExtensionRequest.ReplacementAction {
        .replace
    }

    nonisolated func requestNeedsUserApproval(
        _ request: OSSystemExtensionRequest
    ) {
        Task { @MainActor in
            if #available(macOS 26.0, *) {
                status = "Approval required in System Settings > General > " +
                    "Login Items & Extensions > Driver Extensions"
            } else {
                status = "Approval required in System Settings > Privacy & Security"
            }
        }
    }

    nonisolated func request(
        _ request: OSSystemExtensionRequest,
        didFinishWithResult result: OSSystemExtensionRequest.Result
    ) {
        Task { @MainActor in
            requestInProgress = false
            status = result == .completed
                ? "Driver activation completed"
                : "Driver activation will complete after restart"
        }
    }

    nonisolated func request(
        _ request: OSSystemExtensionRequest,
        didFailWithError error: Error
    ) {
        Task { @MainActor in
            requestInProgress = false
            status = "Driver request failed: \(error.localizedDescription)"
        }
    }
}
