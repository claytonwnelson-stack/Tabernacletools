// Tiny DB interface so the same SQL runs on Cloudflare D1 (production) and node:sqlite (tests / local dev).
export interface Db {
  run(sql: string, ...params: unknown[]): Promise<{ changes: number }>;
  first<T = Record<string, unknown>>(sql: string, ...params: unknown[]): Promise<T | null>;
  all<T = Record<string, unknown>>(sql: string, ...params: unknown[]): Promise<T[]>;
}
// Minimal structural type for D1 (avoids a hard dependency on workers-types at runtime)
interface D1Like { prepare(sql: string): { bind(...v: unknown[]): { run(): Promise<{ meta: { changes: number } }>; first<T>(): Promise<T | null>; all<T>(): Promise<{ results: T[] }> } } }
export function d1Db(d1: D1Like): Db {
  return {
    async run(sql, ...p) { const r = await d1.prepare(sql).bind(...p).run(); return { changes: r.meta.changes }; },
    async first(sql, ...p) { return await d1.prepare(sql).bind(...p).first(); },
    async all(sql, ...p) { return (await d1.prepare(sql).bind(...p).all()).results as never; },
  };
}
