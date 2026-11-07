// Local dev server: node:sqlite file DB + FAKE Apple (no Apple account needed).
//   npm run dev            -> http://localhost:8787
//   GET  /dev/siwa?sub=alice                 -> a fake SIWA identity token for that user
//   GET  /dev/transaction?otid=OT1&days=30  -> a fake signed church transaction (also registers it with the fake Server API)
//   POST /dev/notify {type, subtype, otid, days} -> sends a fake signed App Store notification
// The fake Apple root is trusted ONLY in this process. Never deploy this file.
import { createServer } from "node:http";
import { world, txFor } from "../test/helpers.ts";
import { handle } from "../src/app.ts";
const w = await world(); w.deps.now = () => Math.floor(Date.now() / 1000);
const port = Number(process.env.PORT ?? 8787);
createServer(async (req, res) => {
  const body = await new Promise<string>((r) => { let b = ""; req.on("data", (c) => (b += c)); req.on("end", () => r(b)); });
  const url = new URL(req.url ?? "/", `http://localhost:${port}`); let out: Response;
  const now = Math.floor(Date.now() / 1000);
  if (url.pathname === "/dev/siwa") out = Response.json({ identityToken: await w.siwa.token({ sub: url.searchParams.get("sub") ?? "dev" }) });
  else if (url.pathname === "/dev/transaction") {
    const otid = url.searchParams.get("otid") ?? "OT1", exp = now + Number(url.searchParams.get("days") ?? 30) * 86400;
    w.apple.subs.set(otid, { status: 1, tx: txFor(otid, exp), renewal: {} }); out = Response.json({ signedTransaction: await w.chain.sign(txFor(otid, exp)) });
  } else if (url.pathname === "/dev/notify") {
    const j = JSON.parse(body || "{}"), exp = now + Number(j.days ?? 30) * 86400, tx = txFor(j.otid ?? "OT1", exp);
    const signedPayload = await w.chain.sign({ notificationType: j.type, subtype: j.subtype, notificationUUID: crypto.randomUUID(), data: { bundleId: "com.tabernacletools.guitar", signedTransactionInfo: await w.chain.sign(tx), signedRenewalInfo: await w.chain.sign({ originalTransactionId: tx.originalTransactionId }) } });
    out = await handle(new Request(url.origin + "/v1/appstore/notifications", { method: "POST", body: JSON.stringify({ signedPayload }) }), w.deps);
  } else out = await handle(new Request(url, { method: req.method, headers: req.headers as Record<string, string>, body: req.method === "GET" || req.method === "HEAD" ? undefined : body }), w.deps);
  res.writeHead(out.status, Object.fromEntries(out.headers)); res.end(await out.text());
}).listen(port, () => console.log(`seat-service dev (FAKE Apple, in-memory DB) on http://localhost:${port}`));
