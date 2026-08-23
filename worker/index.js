const TOKEN_TTL_MS = 2 * 60 * 60 * 1000;
const NEWS_KEY = "news:items";
const MAX_ITEMS = 200;

const enc = new TextEncoder();
const dec = new TextDecoder();

const json = (data, status = 200, extra = {}) =>
  new Response(JSON.stringify(data), {
    status,
    headers: { "Content-Type": "application/json", ...extra },
  });

const empty = (status, extra = {}) => new Response(null, { status, headers: extra });

async function hmac(secret, data) {
  const key = await crypto.subtle.importKey(
    "raw",
    enc.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign", "verify"],
  );
  const sig = await crypto.subtle.sign("HMAC", key, enc.encode(data));
  return new Uint8Array(sig);
}

function b64url(bytes) {
  let s = "";
  for (const b of bytes) s += String.fromCharCode(b);
  return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}
function b64urlDecode(str) {
  const pad = str.length % 4 === 0 ? "" : "=".repeat(4 - (str.length % 4));
  const s = atob(str.replace(/-/g, "+").replace(/_/g, "/") + pad);
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
  return out;
}

function constantTimeEqualsStr(a, b) {
  if (typeof a !== "string" || typeof b !== "string") return false;
  const x = enc.encode(a), y = enc.encode(b);
  if (x.length !== y.length) return false;
  let diff = 0;
  for (let i = 0; i < x.length; i++) diff |= x[i] ^ y[i];
  return diff === 0;
}
function constantTimeEqualsBytes(a, b) {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a[i] ^ b[i];
  return diff === 0;
}

async function issueToken(secret) {
  const exp = Date.now() + TOKEN_TTL_MS;
  const nonce = crypto.getRandomValues(new Uint8Array(12));
  const payload = `${exp}.${b64url(nonce)}`;
  const sig = await hmac(secret, payload);
  return { token: `${payload}.${b64url(sig)}`, expiresAt: exp };
}

async function verifyToken(secret, token) {
  if (!token || typeof token !== "string") return false;
  const last = token.lastIndexOf(".");
  if (last <= 0) return false;
  const payload = token.slice(0, last);
  const sig = token.slice(last + 1);
  let providedSig;
  try { providedSig = b64urlDecode(sig); } catch { return false; }
  const expectedSig = await hmac(secret, payload);
  if (!constantTimeEqualsBytes(expectedSig, providedSig)) return false;
  const dot = payload.indexOf(".");
  if (dot <= 0) return false;
  const exp = Number(payload.slice(0, dot));
  if (!Number.isFinite(exp)) return false;
  return Date.now() < exp;
}

function bearer(req) {
  const h = req.headers.get("Authorization") || "";
  if (!/^Bearer\s+/i.test(h)) return null;
  return h.replace(/^Bearer\s+/i, "").trim();
}

function trim(s, max) {
  if (typeof s !== "string") return "";
  const t = s.trim();
  return t.length <= max ? t : t.slice(0, max);
}

function validSubmission(body) {
  if (!body || typeof body !== "object") return null;
  const title = trim(body.title, 140);
  const text = trim(body.body, 8000);
  const author = trim(body.author, 60);
  const category = body.category ? trim(body.category, 40) : null;
  const imageUrl = body.imageUrl ? trim(body.imageUrl, 500) : null;
  if (!title || !text || !author) return null;
  return { title, body: text, author, category: category || null, imageUrl: imageUrl || null };
}

async function readItems(env) {
  const raw = await env.NEWS_KV.get(NEWS_KEY);
  if (!raw) return [];
  try {
    const parsed = JSON.parse(raw);
    return Array.isArray(parsed.items) ? parsed.items : [];
  } catch {
    return [];
  }
}

async function writeItems(env, items) {
  await env.NEWS_KV.put(NEWS_KEY, JSON.stringify({ items }));
}

async function notifyDiscord(env, item) {
  if (!env.DISCORD_WEBHOOK_URL) return;
  const embed = {
    title: item.title.slice(0, 256),
    description: item.body.slice(0, 4000),
    color: 7002098,
    timestamp: item.createdAt,
    author: { name: item.author.slice(0, 256) },
  };
  if (item.category) embed.fields = [{ name: "Category", value: item.category, inline: true }];
  if (item.imageUrl) embed.image = { url: item.imageUrl };
  const payload = { username: "Genix News", embeds: [embed] };
  try {
    await fetch(env.DISCORD_WEBHOOK_URL, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
  } catch {}
}

function uuid() {
  if (crypto.randomUUID) return crypto.randomUUID();
  const b = crypto.getRandomValues(new Uint8Array(16));
  b[6] = (b[6] & 0x0f) | 0x40;
  b[8] = (b[8] & 0x3f) | 0x80;
  const h = [...b].map(x => x.toString(16).padStart(2, "0")).join("");
  return `${h.slice(0,8)}-${h.slice(8,12)}-${h.slice(12,16)}-${h.slice(16,20)}-${h.slice(20)}`;
}

async function handle(req, env) {
  const url = new URL(req.url);
  const path = url.pathname;

  if (path === "/api/health") return json({ ok: true });

  if (path === "/api/news" && req.method === "GET") {
    const items = await readItems(env);
    return json({ items });
  }

  if (path === "/api/auth" && req.method === "POST") {
    if (!env.NEWS_CODE || !env.AUTH_SECRET) return empty(500);
    let body;
    try { body = await req.json(); } catch { return json({ error: "bad_json" }, 400); }
    if (!body || typeof body.code !== "string") return json({ error: "missing_code" }, 400);
    if (!constantTimeEqualsStr(env.NEWS_CODE, body.code)) return empty(401);
    const issued = await issueToken(env.AUTH_SECRET);
    return json(issued);
  }

  if (path === "/api/news" && req.method === "POST") {
    if (!env.AUTH_SECRET) return empty(500);
    const token = bearer(req);
    if (!(await verifyToken(env.AUTH_SECRET, token))) return empty(401);
    let body;
    try { body = await req.json(); } catch { return json({ error: "bad_json" }, 400); }
    const clean = validSubmission(body);
    if (!clean) return json({ error: "invalid_fields" }, 400);
    const item = { id: uuid(), ...clean, createdAt: new Date().toISOString() };
    const items = await readItems(env);
    items.unshift(item);
    if (items.length > MAX_ITEMS) items.length = MAX_ITEMS;
    await writeItems(env, items);
    await notifyDiscord(env, item);
    return json({ ok: true, item });
  }

  return empty(404);
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    if (!url.pathname.startsWith("/api/")) {
      return env.ASSETS ? env.ASSETS.fetch(req) : empty(404);
    }
    try {
      return await handle(req, env);
    } catch {
      return empty(500);
    }
  },
};
