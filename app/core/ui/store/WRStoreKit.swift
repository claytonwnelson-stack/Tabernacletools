// Tabernacle Tools v0.13 - StoreKit 2 bridge (Swift). Compiled ONLY in App Store builds (-DWR_STORE_BUILD=ON).
// StoreKit 2 (Product / Transaction / AppStore) is Swift-only, so this tiny class exposes it to Objective-C++
// (core/ui/StoreBridge_ios.mm). No bridging header and no generated "-Swift.h" header are needed: the ObjC++ side finds the
// class by name (NSClassFromString("WRStoreKitBridge")) and calls these @objc methods; every argument is an object/block.
//
// Everything handed back is plain NSDictionary / NSArray (strings + NSNumber) so the C++ side never touches StoreKit types.
// Signed data: `jws` is Transaction.jwsRepresentation - the seat service verifies it (x5c -> Apple Root CA G3) before creating
// a church team. The app itself trusts only StoreKit's local verification (VerificationResult.verified).
import Foundation
import StoreKit

@available(iOS 15.0, macOS 12.0, *)
@objc(WRStoreKitBridge)
public final class WRStoreKitBridge: NSObject {
    private var updatesTask: Task<Void, Never>?
    private var products: [String: Product] = [:]
    static let productIds = ["com.tabernacletools.guitar.individual.monthly", "com.tabernacletools.guitar.church.monthly"]

    deinit { updatesTask?.cancel() }

    // Start listening for Transaction.updates (renewals, refunds, Ask-to-Buy approvals, purchases on other devices).
    // `onChange` gets the full entitlement snapshot (same format as `entitlements:`) after every update, on the main thread.
    @objc public func start(_ onChange: @escaping (NSArray) -> Void) {
        updatesTask?.cancel()
        updatesTask = Task.detached(priority: .background) { [weak self] in
            for await update in Transaction.updates {
                if case .verified(let t) = update { await t.finish() }
                guard let self else { return }
                let snap = await self.snapshot()
                await MainActor.run { onChange(snap) }
            }
        }
        Task { let snap = await snapshot(); await MainActor.run { onChange(snap) } }
    }

    // [{id, displayName, displayPrice, trialEligible(NSNumber bool)}]
    @objc public func products(_ done: @escaping (NSArray) -> Void) {
        Task {
            var out: [[String: Any]] = []
            do {
                for p in try await Product.products(for: Self.productIds) {
                    products[p.id] = p
                    let eligible = await p.subscription?.isEligibleForIntroOffer ?? false
                    out.append(["id": p.id, "displayName": p.displayName, "displayPrice": p.displayPrice, "trialEligible": NSNumber(value: eligible)])
                }
            } catch { }
            await MainActor.run { done(out as NSArray) }
        }
    }

    // v0.13 one trial per Apple ID: both products are in ONE subscription group, so eligibility is asked per group.
    // {known(NSNumber bool), eligible(NSNumber bool), groupId?, sameGroup(NSNumber bool)}. Never "eligible" on any doubt.
    @objc public func introEligibility(_ done: @escaping (NSDictionary) -> Void) {
        Task {
            var r: [String: Any] = ["known": NSNumber(value: false), "eligible": NSNumber(value: false)]
            if let ps = try? await Product.products(for: Self.productIds), !ps.isEmpty {
                for p in ps { products[p.id] = p }
                let groups = Set(ps.compactMap { $0.subscription?.subscriptionGroupID })
                if let gid = groups.first {
                    let eligible = await Product.SubscriptionInfo.isEligibleForIntroOffer(for: gid)
                    // If App Store Connect was misconfigured with two groups, a trial could be taken twice; refuse to advertise one.
                    let same = groups.count == 1
                    if !same { NSLog("Tabernacle Tools: products are in %d subscription groups - put both in ONE group", groups.count) }
                    r = ["known": NSNumber(value: true), "eligible": NSNumber(value: eligible && same), "groupId": gid, "sameGroup": NSNumber(value: same)]
                }
            }
            await MainActor.run { done(r as NSDictionary) }
        }
    }

    // {status: "success" | "pending" | "cancelled" | "failed", jws?, error?}
    @objc public func purchase(_ productId: NSString, completion done: @escaping (NSDictionary) -> Void) {
        Task {
            var r: [String: Any] = ["status": "failed"]
            do {
                if products[productId as String] == nil { for p in try await Product.products(for: [productId as String]) { products[p.id] = p } }
                guard let p = products[productId as String] else { throw NSError(domain: "WR", code: 1, userInfo: [NSLocalizedDescriptionKey: "Product not found in the App Store"]) }
                switch try await p.purchase() {
                case .success(let v):
                    switch v {
                    case .verified(let t): await t.finish(); r = ["status": "success", "jws": v.jwsRepresentation, "productId": t.productID]
                    case .unverified(_, let e): r = ["status": "failed", "error": "Purchase could not be verified: \(e.localizedDescription)"]
                    }
                case .pending: r = ["status": "pending"]            // Ask to Buy / SCA: arrives later via Transaction.updates
                case .userCancelled: r = ["status": "cancelled"]
                @unknown default: r = ["status": "failed"]
                }
            } catch { r = ["status": "failed", "error": error.localizedDescription] }
            await MainActor.run { done(r as NSDictionary) }
        }
    }

    // Restore = AppStore.sync() (shows the Apple ID sheet if needed), then a fresh snapshot. {ok, error?, entitlements}
    @objc public func restore(_ done: @escaping (NSDictionary) -> Void) {
        Task {
            var r: [String: Any] = ["ok": NSNumber(value: true)]
            do { try await AppStore.sync() } catch { r = ["ok": NSNumber(value: false), "error": error.localizedDescription] }
            r["entitlements"] = await snapshot()
            await MainActor.run { done(r as NSDictionary) }
        }
    }

    // Current snapshot: [{productId, originalTransactionId, jws, purchasedAt, expiresAt, revoked, trial, state, graceUntil, upgraded}]
    @objc public func entitlements(_ done: @escaping (NSArray) -> Void) { Task { let s = await snapshot(); await MainActor.run { done(s) } } }

    private func snapshot() async -> NSArray {
        var out: [[String: Any]] = []
        // renewal state (grace period / billing retry) for the whole subscription group, keyed by original transaction id
        var states: [UInt64: (String, Double)] = [:]
        if let any = try? await Product.products(for: Self.productIds).first, let statuses = try? await any.subscription?.status {
            for s in statuses {
                guard case .verified(let t) = s.transaction else { continue }
                var grace = 0.0
                if case .verified(let info) = s.renewalInfo, let g = info.gracePeriodExpirationDate { grace = g.timeIntervalSince1970 }
                let name: String
                switch s.state {
                case .subscribed: name = "subscribed"
                case .inGracePeriod: name = "grace"
                case .inBillingRetryPeriod: name = "billingRetry"
                case .expired: name = "expired"
                case .revoked: name = "revoked"
                default: name = "unknown"
                }
                states[t.originalID] = (name, grace)
            }
        }
        for await result in Transaction.currentEntitlements {
            guard case .verified(let t) = result, t.productType == .autoRenewable else { continue }
            let st = states[t.originalID] ?? ("subscribed", 0)
            out.append([
                "productId": t.productID,
                "originalTransactionId": String(t.originalID),
                "jws": result.jwsRepresentation,
                "purchasedAt": NSNumber(value: t.purchaseDate.timeIntervalSince1970),
                "expiresAt": NSNumber(value: t.expirationDate?.timeIntervalSince1970 ?? 0),
                "revoked": NSNumber(value: t.revocationDate != nil),
                "trial": NSNumber(value: t.offerType == .introductory),
                "upgraded": NSNumber(value: t.isUpgraded),
                "state": st.0,
                "graceUntil": NSNumber(value: st.1),
            ])
        }
        return out as NSArray
    }
}
