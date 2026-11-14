// v0.18 the Tabernacle Tools app family. Each app is its own App Store app with its OWN subscription group and products
// (StoreKit can't share a subscription between apps); the seat service makes one purchase unlock every app.
export type Plan = "individual" | "church";
export interface AppInfo { bundleId: string; name: string }
export const APPS: AppInfo[] = [
  { bundleId: "com.tabernacletools.guitar", name: "Tabernacle Tools - Electric Guitar" },
  { bundleId: "com.tabernacletools.keys", name: "Tabernacle Tools - Keys" },
  { bundleId: "com.tabernacletools.bass", name: "Tabernacle Tools - Bass" },
  { bundleId: "com.tabernacletools.charts", name: "Tabernacle Tools - Charts" },
];
// Product ids: Guitar keeps the v0.12 ids; the others are <bundleId>.individual.monthly / <bundleId>.church.monthly.
export function productId(bundleId: string, plan: Plan) { return `${bundleId}.${plan}.monthly`; }
export function planOf(bundleId: string, product: string): Plan | null {
  for (const p of ["individual", "church"] as Plan[]) if (product === productId(bundleId, p)) return p;
  return null;
}
export const appName = (bundleId: string) => APPS.find((a) => a.bundleId === bundleId)?.name ?? bundleId;
export const FAMILY_AUDIENCE = "com.tabernacletools"; // entitlement tokens are valid in every app of the family
