// Checks a Verifiable Tape proof bundle with WebCrypto only: book hash, leaf hash, inclusion proof, signed head.
"use strict";
(function (root) {
  const enc = new TextEncoder();
  const SPKI_ED25519 = hexBytes("302a300506032b6570032100");

  function hexBytes(hex) {
    const out = new Uint8Array(hex.length / 2);
    for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.substr(i * 2, 2), 16);
    return out;
  }

  function bytesHex(bytes) {
    return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
  }

  class Writer {
    constructor() { this.parts = []; }
    bytes(b) { this.parts.push(b instanceof Uint8Array ? b : Uint8Array.from(b)); return this; }
    text(s) { return this.bytes(enc.encode(s)); }
    uint(value, size) {
      let v = BigInt.asUintN(size * 8, BigInt(value));
      const b = new Uint8Array(size);
      for (let i = 0; i < size; i++) { b[i] = Number(v & 0xffn); v >>= 8n; }
      return this.bytes(b);
    }
    done() {
      const out = new Uint8Array(this.parts.reduce((n, p) => n + p.length, 0));
      let at = 0;
      for (const p of this.parts) { out.set(p, at); at += p.length; }
      return out;
    }
  }

  async function sha256(subtle, bytes) {
    return new Uint8Array(await subtle.digest("SHA-256", bytes));
  }

  function equal(a, b) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
    return true;
  }

  const isHex = (s, n) => typeof s === "string" && s.length === n && /^[0-9a-f]*$/.test(s);
  const isUint = (s) => typeof s === "string" && /^[0-9]{1,20}$/.test(s) && BigInt(s) < 2n ** 64n;
  const isInt = (s) => typeof s === "string" && /^-?[0-9]{1,19}$/.test(s) && BigInt(s) >= -(2n ** 63n) && BigInt(s) < 2n ** 63n;
  const isSmall = (v, max) => Number.isInteger(v) && v >= 0 && v <= max;

  function shapeProblem(b) {
    if (!b || typeof b !== "object" || b.version !== 1) return "not a version 1 bundle";
    if (typeof b.symbol !== "string" || !b.symbol || enc.encode(b.symbol).length > 255) return "symbol";
    const k = b.book;
    if (!k || !isUint(k.position) || !Array.isArray(k.bids) || !Array.isArray(k.asks)) return "book";
    for (const side of [k.bids, k.asks]) {
      if (side.length > 0xffff) return "book side";
      for (const l of side)
        if (!Array.isArray(l) || l.length !== 3 || !isInt(l[0]) || !isInt(l[1]) || !isSmall(l[2], 0xffffffff)) return "book level";
    }
    const f = b.leaf;
    if (!f || !isUint(f.index) || BigInt(f.index) < 1n || !isHex(f.entry_digest, 64)) return "leaf";
    for (const [name, max] of [["code", 255], ["type", 255], ["stream", 255], ["book_state", 255], ["flags", 0xffff], ["closes", 0xffff]])
      if (!isSmall(f[name], max)) return "leaf " + name;
    if (!Array.isArray(b.proof) || b.proof.length > 64 || !b.proof.every((h) => isHex(h, 64))) return "proof";
    const h = b.head;
    if (!h || !isUint(h.incarnation) || !isUint(h.tick) || !isUint(h.tree_size) || !isHex(h.root, 64)) return "head";
    if (!h.signatures || typeof h.signatures !== "object") return "head signatures";
    for (const [v, s] of Object.entries(h.signatures)) if (!["1", "2", "3"].includes(v) || !isHex(s, 128)) return "head signatures";
    if (!b.keys || typeof b.keys !== "object") return "keys";
    for (const [v, s] of Object.entries(b.keys)) if (!["1", "2", "3"].includes(v) || !isHex(s, 64)) return "keys";
    return null;
  }

  async function bookHash(subtle, b) {
    const w = new Writer().text("velocita-tape/v1/book");
    const symbol = enc.encode(b.symbol);
    w.uint(symbol.length, 1).bytes(symbol).uint(b.book.position, 8);
    for (const side of [b.book.bids, b.book.asks]) {
      w.uint(side.length, 2);
      for (const [price, qty, count] of side) w.uint(price, 8).uint(qty, 8).uint(count, 4);
    }
    return sha256(subtle, w.done());
  }

  async function leafHash(subtle, b, book) {
    const f = b.leaf;
    const w = new Writer().uint(0, 1).text("velocita-tape/v1/leaf").uint(f.index, 8).bytes(hexBytes(f.entry_digest));
    w.uint(0xe0, 1).uint(f.code, 1).uint(f.type, 1).uint(f.stream, 1).uint(f.book_state, 1);
    w.bytes(book).uint(f.flags, 2).uint(f.closes, 2);
    return sha256(subtle, w.done());
  }

  async function nodeHash(subtle, left, right) {
    return sha256(subtle, new Writer().uint(1, 1).bytes(left).bytes(right).done());
  }

  async function rootFromProof(subtle, leaf, index, size, proof) {
    if (index >= size) return null;
    let fn = index, sn = size - 1n, r = leaf;
    for (const hex of proof) {
      const p = hexBytes(hex);
      if (sn === 0n) return null;
      if ((fn & 1n) === 1n || fn === sn) {
        r = await nodeHash(subtle, p, r);
        if ((fn & 1n) === 0n) while (fn !== 0n && (fn & 1n) === 0n) { fn >>= 1n; sn >>= 1n; }
      } else {
        r = await nodeHash(subtle, r, p);
      }
      fn >>= 1n;
      sn >>= 1n;
    }
    return sn === 0n ? r : null;
  }

  function headMessage(h) {
    return new Writer().text("velocita-tape/v1").uint(h.incarnation, 8).uint(h.tick, 8).uint(h.tree_size, 8)
      .bytes(hexBytes(h.root)).done();
  }

  async function fingerprint(subtle, rawHex) {
    return bytesHex(await sha256(subtle, new Writer().bytes(SPKI_ED25519).bytes(hexBytes(rawHex)).done()));
  }

  async function verifyBundle(bundle, options) {
    const subtle = (options && options.subtle) || root.crypto.subtle;
    const pinned = (options && options.pinned) || null;
    const steps = [];
    const result = { ok: false, steps, fingerprints: {}, signed_by: [] };
    const step = (name, ok, detail) => { steps.push({ name, ok, detail }); return ok; };

    const problem = shapeProblem(bundle);
    if (!step("bundle", !problem, problem ? "malformed: " + problem : "well formed")) return result;

    const book = await bookHash(subtle, bundle);
    step("book hash", true, bytesHex(book));
    const leaf = await leafHash(subtle, bundle, book);
    step("leaf hash", true, bytesHex(leaf));

    const index = BigInt(bundle.leaf.index) - 1n;
    const size = BigInt(bundle.head.tree_size);
    const root2 = await rootFromProof(subtle, leaf, index, size, bundle.proof);
    const rootOk = root2 !== null && equal(root2, hexBytes(bundle.head.root));
    if (!step("inclusion", rootOk, rootOk ? "entry " + bundle.leaf.index + " is in the tree of " + bundle.head.tree_size
      : "the proof does not lead to the head's root")) return result;

    for (const [voter, raw] of Object.entries(bundle.keys)) result.fingerprints[voter] = await fingerprint(subtle, raw);
    if (pinned) {
      const wrong = Object.keys(result.fingerprints).filter((v) => pinned[v] !== result.fingerprints[v]);
      if (!step("keys", wrong.length === 0, wrong.length ? "voter " + wrong.join(", ") + " key is not the published key"
        : "all keys match the published fingerprints")) return result;
    } else {
      step("keys", true, "not pinned; compare the fingerprints with the published ones");
    }

    const message = headMessage(bundle.head);
    const seen = new Set();
    for (const voter of ["1", "2", "3"]) {
      const raw = bundle.keys[voter], sig = bundle.head.signatures[voter];
      if (!raw || !sig || seen.has(raw)) continue;
      let good = false;
      try {
        const key = await subtle.importKey("raw", hexBytes(raw), { name: "Ed25519" }, false, ["verify"]);
        good = await subtle.verify({ name: "Ed25519" }, key, hexBytes(sig), message);
      } catch (e) {
        if (e && e.name === "NotSupportedError") {
          step("signatures", false, "this browser cannot check Ed25519 signatures");
          return result;
        }
      }
      if (good) { seen.add(raw); result.signed_by.push(Number(voter)); }
    }
    const enough = result.signed_by.length >= 2;
    step("signatures", enough, result.signed_by.length + " of 3 voters signed this head" + (enough ? "" : "; at least 2 are needed"));
    result.ok = enough;
    return result;
  }

  const api = { verifyBundle, bookHash, leafHash, headMessage, fingerprint, bytesHex, hexBytes };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.TapeProof = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
