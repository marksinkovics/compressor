// Tests the WebAssembly demo module under node: node web/test/test_demo.mjs build/web/ingot_demo.js
import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const modulePath = process.argv[2];
assert.ok(modulePath, "usage: node test_demo.mjs path/to/ingot_demo.js");
const { default: createIngotDemo } = await import(pathToFileURL(modulePath).href);
const demo = await createIngotDemo();

function analyze(text) {
  return JSON.parse(demo.analyze(text));
}

function checkCodes(result, text) {
  const bytes = new TextEncoder().encode(text);
  const codes = new Map(result.symbols.map((s) => [s.byte, s.code]));
  // Counts match the bytes, and no code is a prefix of another.
  const counts = new Map();
  for (const b of bytes) counts.set(b, (counts.get(b) ?? 0) + 1);
  assert.equal(result.symbols.length, counts.size);
  for (const s of result.symbols) {
    assert.equal(s.count, counts.get(s.byte));
    assert.ok(s.code.length >= 1 && s.code.length <= 15, `length of ${s.byte}`);
  }
  const all = [...codes.values()];
  for (const a of all) {
    for (const b of all) {
      if (a !== b) assert.ok(!b.startsWith(a), `${a} is a prefix of ${b}`);
    }
  }
  // The bit stream is the codes of the bytes in order.
  let expected = "";
  let total = 0;
  for (const b of bytes) {
    total += codes.get(b).length;
    if (expected.length < 16384) expected += codes.get(b);
  }
  assert.equal(result.totalBits, total);
  assert.equal(result.bits, expected);
  assert.equal(result.size, bytes.length);
}

const corpus = join(dirname(fileURLToPath(import.meta.url)), "../../fuzz/corpus/container");

const tests = {
  // Files a native (64-bit) build wrote decode here with every CRC32 matching, so the 32-bit
  // build runs the same models: every coder has a seed (coder<N>.ingot, cm.ingot, image.ingot).
  nativeFilesDecode() {
    // Not the archive, nor the seeds that need fuzz/src/dictionary.h's dictionary.
    const skip = new Set(["archive.ingot", "coder9.ingot", "coder10.ingot"]);
    const names = readdirSync(corpus).filter((n) => n.endsWith(".ingot") && !skip.has(n));
    assert.ok(names.length >= 14);
    for (const name of names) {
      const result = JSON.parse(demo.decode(new Uint8Array(readFileSync(join(corpus, name)))));
      assert.ok(result.ok, `${name}: ${result.error}`);
    }
    const damaged = new Uint8Array(readFileSync(join(corpus, "cm.ingot")));
    damaged[damaged.length - 20] ^= 1;
    assert.equal(JSON.parse(demo.decode(damaged)).ok, false);
  },
  emptyText() {
    const result = analyze("");
    assert.equal(result.size, 0);
    assert.deepEqual(result.symbols, []);
    assert.equal(result.levels.length, 9);
    for (const level of result.levels) assert.ok(level.decoded);
  },
  singleByte() {
    const result = analyze("aaaa");
    assert.equal(result.symbols.length, 1);
    assert.equal(result.symbols[0].code.length, 1);
    checkCodes(result, "aaaa");
  },
  textAndUnicode() {
    const text = "Huffman coding in the browser: ñ, ü, 漢字, and a repeated phrase. ".repeat(40);
    const result = analyze(text);
    checkCodes(result, text);
    // Frequent bytes get codes no longer than rare ones.
    const sorted = [...result.symbols].sort((a, b) => b.count - a.count);
    assert.ok(sorted[0].code.length <= sorted.at(-1).code.length);
  },
  everyLevelDecodesAndCompresses() {
    const text = "the quick brown fox jumps over the lazy dog\n".repeat(200);
    const result = analyze(text);
    assert.deepEqual(result.levels.map((l) => l.level), [1, 2, 3, 4, 5, 6, 7, 8, 9]);
    for (const level of result.levels) {
      assert.ok(level.decoded, `level ${level.level}`);
      assert.ok(level.method.length > 0);
    }
    assert.equal(result.levels[0].method, "Huffman");
    assert.ok(result.levels[5].size < result.size / 20, "level 6 finds the repeats");
  },
};

for (const [name, test] of Object.entries(tests)) {
  test();
  console.log("ok", name);
}
console.log(`${Object.keys(tests).length} tests passed`);
