/* SPDX-License-Identifier: BSD-3-Clause */

import Foundation
import SystemExtensions

private func compareSystemExtensionVersions(
    _ lhs: OSSystemExtensionProperties,
    _ rhs: OSSystemExtensionProperties
) -> ComparisonResult {
    let shortVersionComparison = lhs.bundleShortVersion.compare(
        rhs.bundleShortVersion, options: .numeric
    )
    if shortVersionComparison != .orderedSame {
        return shortVersionComparison
    }
    return lhs.bundleVersion.compare(rhs.bundleVersion, options: .numeric)
}

@MainActor
final class DriverManager: NSObject, ObservableObject {
    private enum RequestAction: Equatable {
        case none
        case activate
        case deactivate
    }

    static let driverIdentifier = "org.opentimeserver.timecard.macos.driver"

    @Published private(set) var status = "Checking driver status"
    @Published private(set) var requestInProgress = false
    private var pendingAction = RequestAction.none
    private var pendingMutationRequest: OSSystemExtensionRequest?

    override init() {
        super.init()
        refreshStatus()
    }

    func refreshStatus() {
        let request = OSSystemExtensionRequest.propertiesRequest(
            forExtensionWithIdentifier: Self.driverIdentifier,
            queue: .main
        )
        request.delegate = self
        OSSystemExtensionManager.shared.submitRequest(request)
    }

    func activate() {
        guard !requestInProgress else { return }
        pendingAction = .activate
        requestInProgress = true
        status = "Requesting driver activation"
        let request = OSSystemExtensionRequest.activationRequest(
            forExtensionWithIdentifier: Self.driverIdentifier,
            queue: .main
        )
        request.delegate = self
        pendingMutationRequest = request
        OSSystemExtensionManager.shared.submitRequest(request)
    }

    func deactivate() {
        guard !requestInProgress else { return }
        pendingAction = .deactivate
        requestInProgress = true
        status = "Requesting driver removal"
        let request = OSSystemExtensionRequest.deactivationRequest(
            forExtensionWithIdentifier: Self.driverIdentifier,
            queue: .main
        )
        request.delegate = self
        pendingMutationRequest = request
        OSSystemExtensionManager.shared.submitRequest(request)
    }
}

extension DriverManager: @preconcurrency OSSystemExtensionRequestDelegate {
    func request(
        _ request: OSSystemExtensionRequest,
        actionForReplacingExtension existing: OSSystemExtensionProperties,
        withExtension replacement: OSSystemExtensionProperties
    ) -> OSSystemExtensionRequest.ReplacementAction {
        compareSystemExtensionVersions(replacement, existing) == .orderedAscending
            ? .cancel : .replace
    }

    func requestNeedsUserApproval(
        _ request: OSSystemExtensionRequest
    ) {
        if #available(macOS 26.0, *) {
            status = "Approval required in System Settings > General > " +
                "Login Items & Extensions > Driver Extensions"
        } else {
            status = "Approval required in System Settings > Privacy & Security"
        }
    }

    func request(
        _ request: OSSystemExtensionRequest,
        didFinishWithResult result: OSSystemExtensionRequest.Result
    ) {
        guard pendingMutationRequest === request else { return }
        pendingMutationRequest = nil
        requestInProgress = false
        let action = pendingAction
        pendingAction = .none
        if result == .completed {
            status = action == .deactivate
                ? "Driver removal completed"
                : "Driver activation completed"
        } else {
            status = action == .deactivate
                ? "Driver removal will complete after restart"
                : "Driver activation will complete after restart"
        }
        if result == .completed {
            Task { @MainActor in
                try? await Task.sleep(for: .milliseconds(500))
                refreshStatus()
            }
        }
    }

    func request(
        _ request: OSSystemExtensionRequest,
        foundProperties properties: [OSSystemExtensionProperties]
    ) {
        guard pendingMutationRequest == nil else { return }
        guard !properties.isEmpty else {
            status = "Driver is not installed"
            return
        }

        let sorted = properties.sorted {
            compareSystemExtensionVersions($0, $1) == .orderedDescending
        }
        let selected = sorted.first {
            $0.isEnabled && !$0.isUninstalling
        } ?? sorted.first(where: \.isEnabled) ?? sorted[0]
        let version = "\(selected.bundleShortVersion)/\(selected.bundleVersion)"

        if selected.isUninstalling {
            status = "Driver \(version) is pending removal"
        } else if selected.isAwaitingUserApproval {
            status = "Driver \(version) is awaiting approval"
        } else if selected.isEnabled {
            status = "Driver \(version) is enabled"
        } else {
            status = "Driver \(version) is installed but disabled"
        }

        let otherVersions = sorted
            .filter { $0 !== selected }
            .map { "\($0.bundleShortVersion)/\($0.bundleVersion)" }
        if !otherVersions.isEmpty {
            status += "; \(otherVersions.joined(separator: ", ")) "
                + "pending restart cleanup"
        }
    }

    func request(
        _ request: OSSystemExtensionRequest,
        didFailWithError error: Error
    ) {
        if pendingMutationRequest === request {
            pendingMutationRequest = nil
            requestInProgress = false
            pendingAction = .none
            status = "Driver request failed: \(error.localizedDescription)"
        } else if pendingMutationRequest == nil {
            status = "Driver status unavailable: \(error.localizedDescription)"
        }
    }
}
