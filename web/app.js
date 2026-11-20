import createIngotDemo from "./ingot_demo.js";

// Typing stays responsive: level 9's context mixing codes about 1-2 MB/s.
const maxBytes = 64 * 1024;
const sample =
  "Huffman coding gives frequent bytes short codes and rare bytes long ones. " +
  "ingot limits every code to 15 bits and writes them in canonical order, " +
  "so a block only needs to store each byte's code length.";

const demo = await createIngotDemo();
const textArea = document.getElementById("text");
const encoder = new TextEncoder();

function show(byte) {
  if (byte === 32) return "␠";
  if (byte === 10) return "⏎";
  if (byte === 9) return "⇥";
  if (byte >= 33 && byte < 127) return String.fromCharCode(byte);
  return "0x" + byte.toString(16).padStart(2, "0");
}

function element(tag, attributes = {}, text = "") {
  const node = document.createElementNS(
    tag === "svg" || ["g", "line", "circle", "rect", "text", "title"].includes(tag)
      ? "http://www.w3.org/2000/svg"
      : "http://www.w3.org/1999/xhtml",
    tag,
  );
  for (const [name, value] of Object.entries(attributes)) node.setAttribute(name, value);
  if (text) node.textContent = text;
  return node;
}

function entropyBits(symbols, size) {
  let bits = 0;
  for (const s of symbols) bits -= s.count * Math.log2(s.count / size);
  return bits;
}

function renderStats(result) {
  const stats = document.getElementById("stats");
  const huffmanBytes = Math.ceil(result.totalBits / 8);
  const average = result.size ? result.totalBits / result.size : 0;
  const entropy = result.size ? entropyBits(result.symbols, result.size) / result.size : 0;
  const items = [
    [result.size.toLocaleString(), "bytes in"],
    [result.symbols.length.toString(), "different bytes"],
    [`${average.toFixed(3)} bits`, "per byte, Huffman"],
    [`${entropy.toFixed(3)} bits`, "per byte, entropy (the bound)"],
    [`${huffmanBytes.toLocaleString()} bytes`, "Huffman payload (+128 for the lengths)"],
  ];
  stats.replaceChildren(
    ...items.map(([value, label]) => {
      const div = element("div", { class: "stat" });
      div.append(element("b", {}, value), element("span", {}, label));
      return div;
    }),
  );
}

// The code tree from the codes themselves: a trie, leaves spaced evenly, parents centred.
function renderTree(symbols) {
  const svg = document.getElementById("tree");
  const root = { children: [null, null] };
  for (const s of symbols) {
    let node = root;
    for (const bit of s.code) {
      const b = Number(bit);
      node.children[b] ??= { children: [null, null] };
      node = node.children[b];
    }
    node.leaf = s;
  }
  const levelHeight = 46;
  const leafWidth = 42;
  let nextX = 0;
  let depth = 0;
  (function place(node, d) {
    depth = Math.max(depth, d);
    node.y = 20 + d * levelHeight;
    if (node.leaf) {
      node.x = 24 + nextX++ * leafWidth;
      return;
    }
    const kids = node.children.filter(Boolean);
    kids.forEach((k) => place(k, d + 1));
    node.x = kids.length ? (kids[0].x + kids.at(-1).x) / 2 : 24;
  })(root, 0);

  const width = Math.max(320, 48 + Math.max(0, nextX - 1) * leafWidth);
  const height = 60 + depth * levelHeight;
  svg.setAttribute("width", width);
  svg.setAttribute("height", height);
  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
  const edges = element("g");
  const nodes = element("g");
  (function draw(node) {
    node.children.forEach((child, bit) => {
      if (!child) return;
      edges.append(element("line", { class: "edge", x1: node.x, y1: node.y, x2: child.x, y2: child.y }));
      nodes.append(
        element("text", { class: `bit${bit}`, x: (node.x + child.x) / 2 + (bit ? 4 : -10), y: (node.y + child.y) / 2 }, String(bit)),
      );
      draw(child);
    });
    if (node.leaf) {
      const g = element("g");
      g.append(
        element("rect", { class: "leaf", x: node.x - 17, y: node.y - 10, width: 34, height: 30, rx: 5 }),
        element("text", { x: node.x, y: node.y + 3, "text-anchor": "middle" }, show(node.leaf.byte)),
        element("text", { x: node.x, y: node.y + 16, "text-anchor": "middle" }, String(node.leaf.count)),
        element("title", {}, `${show(node.leaf.byte)}: ${node.leaf.count}×, code ${node.leaf.code}`),
      );
      nodes.append(g);
    } else {
      nodes.append(element("circle", { class: "inner", cx: node.x, cy: node.y, r: 4 }));
    }
  })(root);
  svg.replaceChildren(edges, nodes);
}

function renderBits(result, bytes) {
  const container = document.getElementById("bits");
  const codes = new Map(result.symbols.map((s) => [s.byte, s.code]));
  const spans = [];
  let shown = 0;
  for (const byte of bytes) {
    const code = codes.get(byte);
    if (shown + code.length > result.bits.length) break;
    spans.push(element("span", { title: `${show(byte)} → ${code}` }, code));
    shown += code.length;
  }
  container.replaceChildren(...spans);
  if (shown < result.totalBits) {
    container.append(element("p", { class: "hint" }, `… ${(result.totalBits - shown).toLocaleString()} more bits`));
  }
}

function renderCodes(symbols) {
  const body = document.querySelector("#codes tbody");
  const rows = [...symbols]
    .sort((a, b) => b.count - a.count || a.byte - b.byte)
    .map((s) => {
      const row = element("tr");
      row.append(
        element("td", {}, show(s.byte)),
        element("td", {}, s.count.toLocaleString()),
        element("td", { class: "code" }, s.code),
        element("td", {}, String(s.code.length)),
      );
      return row;
    });
  body.replaceChildren(...rows);
}

function renderLevels(result) {
  const container = document.getElementById("levels");
  const largest = Math.max(result.size + 30, ...result.levels.map((l) => l.size));
  container.replaceChildren(
    ...result.levels.map((l) => {
      const row = element("div", { class: "row" });
      const bar = element("div");
      bar.append(
        element("div", { class: "bar", style: `width: ${(100 * l.size) / largest}%` }),
        element(
          "div",
          { class: l.decoded ? "label" : "label bad" },
          `${l.size.toLocaleString()} bytes · ${l.method}${l.decoded ? "" : " · did not decode!"}`,
        ),
      );
      row.append(element("b", {}, `-L ${l.level}`), bar);
      return row;
    }),
  );
}

function update() {
  let bytes = encoder.encode(textArea.value);
  const note = document.getElementById("note");
  note.textContent = "";
  if (bytes.length > maxBytes) {
    bytes = bytes.slice(0, maxBytes);
    note.textContent = `Only the first ${maxBytes.toLocaleString()} bytes are coded.`;
  }
  const text = new TextDecoder().decode(bytes);
  bytes = encoder.encode(text);
  const result = JSON.parse(demo.analyze(text));
  renderStats(result);
  renderTree(result.symbols);
  renderBits(result, bytes);
  renderCodes(result.symbols);
  renderLevels(result);
}

let timer = 0;
textArea.addEventListener("input", () => {
  clearTimeout(timer);
  timer = setTimeout(update, 150);
});
textArea.value = sample;
update();
