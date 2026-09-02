// ============================================================================
// tests/web/tests/trnfix_web.spec.js
// Drives tools/trnfix/web/index.html — the trainer triage tool.
//
// This is the page's ONLY gate. There used to be a native exe beside it and a
// second gate pinning that; both are gone (tools/trnfix/README.md has why), so
// nothing else checks that the page still flags and repairs the right bytes. A
// browser tool that got this wrong would be worse than no tool: it hands a
// player back a trainer while telling them it is fixed.
//
// The page carries one action: the "pad" repair. Pointer counting, --graft,
// --zero-ptrs and --disable were the exe's and went with it; the last revision
// carrying them is the parent of the commit that deleted them. Before that, both
// sides were checked to produce byte-identical "pad" output on the real matched
// pair, which is where these fixtures' byte values come from.
//
// Fixtures are synthesised because a real .trn is a player's lap data, and only
// the fixed metadata block matters here.
// ============================================================================
const { test, expect } = require('@playwright/test');
const path = require('path');
const fs = require('fs');
const os = require('os');
const zlib = require('zlib');

const PAGE = 'file://' + path.resolve(__dirname, '../../../tools/trnfix/web/index.html');

const NAME_AT = 0x62;
const NAME_PAD_END = 0x92;
const NAME = 'MX2OEM_2023_Husqvarna_FC_250';
// The slack a crashing trainer carries, verbatim from the file that a graft
// bisection pinned the fault to. 0x7f..0x85 is the fatal part.
const SLACK = [0x7f, 0x00, 0x00, 0xf0, 0xf7, 0x2f, 0x01, 0x00, 0x00, 0x00, 0x00,
               0x30, 0xfb, 0xff, 0xff, 0xe8, 0x04, 0x00, 0x00];

function trn({ ptr = 0n, len = 1845n, magic = [0x47, 0x48, 0x53, 0x00],
               slack = false, size = 0x200 } = {}) {
  const b = Buffer.alloc(size, 0x11);
  Buffer.from(magic).copy(b, 0);
  b.writeBigUInt64LE(len, 0x4a);
  b.writeBigUInt64LE(ptr, 0x52);
  b.write(NAME, NAME_AT, 'latin1');
  b.fill(0, NAME_AT + NAME.length, NAME_PAD_END);          // terminator + slack
  if (slack) Buffer.from(SLACK).copy(b, NAME_AT + NAME.length + 1);
  return b;
}

const FIXTURES = {
  // The pointer from the controlled A/B capture, plus the dirty slack.
  'bad.trn':     trn({ ptr: 0x00007ffb9ec8e430n, slack: true }),
  // A different boot's module base. Recognition must not be pinned to the
  // bases that happen to have been sampled — an earlier revision tested for
  // exactly 0xfe/0xff and silently stopped detecting when one moved.
  'bad_ff.trn':  trn({ ptr: 0x00007fffa4fee430n, slack: true }),
  // Slack cleared. NOT "a good file" — the real trainer that loads reliably
  // carries four leaked addresses, which is why the count is reported rather
  // than turned into a verdict.
  'noptr.trn':   trn({ len: 0x27fn }),
  'notatrn.trn': trn({ ptr: 0x00007ffb9ec8e430n, magic: [0x58, 0x58, 0x58, 0x58] }),
  'ignored.txt': trn({ ptr: 0x00007ffb9ec8e430n, slack: true }),
};

// A minimal zip writer, deliberately NOT the page's own: reading has to work on
// archives the page did not write. `method` picks stored (0) or deflated (8) so
// both paths through readZip are exercised -- Explorer's "Send to > Compressed
// folder" produces deflated entries, which is what players actually hand over.
function zip(files, method = 8) {
  const local = [], central = [];
  let offset = 0;
  const u16 = v => Buffer.from([v & 0xff, (v >> 8) & 0xff]);
  const u32 = v => Buffer.from([v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >>> 24) & 0xff]);
  for (const [name, data] of Object.entries(files)) {
    const nm = Buffer.from(name);
    const body = method === 8 ? zlib.deflateRawSync(data) : data;
    const crc = zlib.crc32 ? zlib.crc32(data) : crc32(data);
    const head = Buffer.concat([u32(0x04034b50), u16(20), u16(0), u16(method), u16(0), u16(0),
                                u32(crc), u32(body.length), u32(data.length), u16(nm.length), u16(0)]);
    local.push(head, nm, body);
    central.push(Buffer.concat([u32(0x02014b50), u16(20), u16(20), u16(0), u16(method), u16(0), u16(0),
                                u32(crc), u32(body.length), u32(data.length), u16(nm.length),
                                u16(0), u16(0), u16(0), u16(0), u32(0), u32(offset)]), nm);
    offset += head.length + nm.length + body.length;
  }
  const dir = Buffer.concat(central);
  return Buffer.concat([...local, dir,
    Buffer.concat([u32(0x06054b50), u16(0), u16(0), u16(Object.keys(files).length),
                   u16(Object.keys(files).length), u32(dir.length), u32(offset), u16(0)])]);
}
function crc32(buf) {           // only used if the Node build predates zlib.crc32
  let c = 0xffffffff;
  for (const b of buf) {
    c ^= b;
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
  }
  return (c ^ 0xffffffff) >>> 0;
}

let dir;
test.beforeAll(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), 'trnweb-'));
  for (const [n, d] of Object.entries(FIXTURES)) fs.writeFileSync(path.join(dir, n), d);
});
test.afterAll(() => fs.rmSync(dir, { recursive: true, force: true }));

// Everything a player can actually read. Deliberately not document.body
// .textContent, which also returns the inline <script> -- the source comments
// legitimately discuss the instruments this page does not offer.
async function visibleText(page, lower = true) {
  const t = await page.evaluate(() => {
    const c = document.body.cloneNode(true);
    c.querySelectorAll('script, style').forEach(n => n.remove());
    return c.textContent;
  });
  return lower ? t.toLowerCase() : t;
}

async function rowsByName(page) {
  await page.waitForSelector('#rows tr');
  const rows = await page.$$eval('#rows tr', trs => trs.map(tr => ({
    name: tr.children[0].textContent,
    pad:  tr.children[1].textContent.trim(),
    status: tr.children[3].textContent.trim(),
    get:    tr.children[4].textContent.trim(),
  })));
  // Rows show the path relative to the chosen folder, so key on the basename.
  return Object.fromEntries(rows.map(r => [r.name.split('/').pop(), r]));
}

async function load(page) {
  await page.goto(PAGE);
  // A webkitdirectory input takes the directory itself, which is also what a
  // player does -- they pick the folder, not the files inside it.
  await page.setInputFiles('#folder', dir);
  return rowsByName(page);
}

test('flags the leftover bytes after the bike name', async ({ page }) => {
  const by = await load(page);
  expect(by['bad.trn'].pad).toBe('11');
  expect(by['noptr.trn'].pad).toBe('none');
  expect(by['notatrn.trn'].pad).toBe('not a trainer file');
  expect(by['ignored.txt']).toBeUndefined();
  await expect(page.locator('.stat.flagged .n')).toHaveText('2');
});

test('offers one action and no instruments', async ({ page }) => {
  // A page a player is pointed at from Discord must not put a bisection tool, a
  // known-dead repair, or a choice they have no way to make in front of them.
  await page.goto(PAGE);
  expect(await page.locator('select').count()).toBe(0);
  for (const dead of ['graft', 'leaked pointer', 'experiment', 'bisect', 'strategy'])
    expect(await visibleText(page)).not.toContain(dead);
});

test('names deleting the trainer as the way out, and where the files go', async ({ page }) => {
  // The repair is not guaranteed, and the fallback needs no tool at all. Saying
  // so is the difference between a dead end and a next step. And a zip of
  // trainers is no use to someone who cannot find where they belong.
  await load(page);
  const t = await visibleText(page);
  expect(t).toContain('delete its trainer file');
  expect(t).toContain('profiles\\<your name>\\trainers');
});

test('uses no em dashes anywhere a player can see', async ({ page }) => {
  await page.goto(PAGE);
  expect(await visibleText(page, false)).not.toContain('\u2014');
});

test('pad zeroes the name slack and nothing else', async ({ page }) => {
  // This is the repair the graft bisection produced: the fatal bytes were
  // narrowed to 0x7f-0x85, inside the slack between the name's terminator and
  // the next field, and clearing just that span was confirmed in game. It must
  // not touch a single byte outside it.
  await load(page);
  const out = await page.evaluate(() => {
    const src = scanned.find(s => s.name.endsWith('bad.trn')).bytes;
    const p = patch(src);
    return {
      changed: [...src].reduce((a, v, i) => v !== p[i] ? [...a, i] : a, []),
      padAfter: namePad(p).dirty,
      nameKept: namePad(p).name,
      sameLen: p.length === src.length,
    };
  });
  expect(out.changed[0]).toBe(0x7f);
  expect(out.changed[out.changed.length - 1]).toBeLessThan(0x92);
  expect(out.padAfter).toBe(0);
  expect(out.nameKept).toBe(NAME);
  expect(out.sameLen).toBe(true);
});

test('pad leaves a trainer with clear padding alone', async ({ page }) => {
  // A no-op must be a no-op, not a rewritten copy: handing a player back a file
  // that is byte-identical except for a timestamp invites them to "repair"
  // files that were never damaged.
  await load(page);
  const same = await page.evaluate(() => {
    const src = scanned.find(s => s.name.endsWith('noptr.trn')).bytes;
    return patch(src) === null;
  });
  expect(same).toBe(true);
});

test('pad refuses a layout it does not recognise', async ({ page }) => {
  // The name offset is read off one bike on one game version. If the bytes at
  // 0x62 are not a NUL-terminated ASCII name, the tool has no idea where the
  // slack is and must decline rather than zero somebody's lap data.
  await load(page);
  const res = await page.evaluate(() => {
    const junk = new Uint8Array(0x200).fill(0x11);
    junk.set([0x47, 0x48, 0x53, 0x00], 0);
    return { pad: namePad(junk), patched: patch(junk) };
  });
  expect(res.pad).toBeNull();
  expect(res.patched).toBeNull();
});

test('never calls a file safe or clean', async ({ page }) => {
  // The detector has been wrong twice, and leftover bytes are necessary but not
  // sufficient: most dirty files load fine. Neither "safe" nor "clean" is a
  // claim this page is entitled to make about any file, only about the bytes.
  const by = await load(page);
  for (const r of Object.values(by)) {
    expect(r.pad.toLowerCase()).not.toContain('safe');
    expect(r.pad.toLowerCase()).not.toContain('clean');
  }
});

test('accepts individual files, not just a whole folder', async ({ page }) => {
  // The folder picker and the file picker are separate inputs. The file one
  // shipped with a change handler but no button opening it -- dead UI -- so
  // this pins that a player can pick a single .trn and get a verdict.
  await page.goto(PAGE);
  await page.setInputFiles('#loose', [path.join(dir, 'bad.trn')]);
  const by = await rowsByName(page);
  expect(by['bad.trn'].pad).toBe('11');
  await expect(page.locator('.stat.flagged .n')).toHaveText('1');
  // and the button that opens it must actually exist and be wired
  const opens = await page.evaluate(() => {
    let opened = false;
    const inp = document.getElementById('loose');
    inp.click = () => { opened = true; };
    document.getElementById('pickfiles').click();
    return opened;
  });
  expect(opens).toBe(true);
});

test('offers a per-file download only for files it would change', async ({ page }) => {
  // The row button and the zip both go through output(), so they cannot
  // disagree about which files are worth writing. A button on an untouched
  // trainer would hand a player back a file they never needed to replace.
  const by = await load(page);
  expect(by['bad.trn'].pad).toBe('11');
  expect(by['noptr.trn'].pad).toBe('none');
  const withButton = await page.$$eval('#rows tr',
    trs => trs.filter(t => t.querySelector('button.save'))
              .map(t => t.children[0].textContent.split('/').pop()));
  expect(withButton.sort()).toEqual(['bad.trn', 'bad_ff.trn']);
});

test('every row says where it stands, including the ones needing nothing', async ({ page }) => {
  // A blank cell reads as "not looked at yet". A player checking a folder needs
  // to tell that apart from "already fine".
  const by = await load(page);
  expect([by['bad.trn'].status, by['bad.trn'].get]).toEqual(['fixed', 'download']);
  expect([by['noptr.trn'].status, by['noptr.trn'].get]).toEqual(['skipped', '']);
  expect([by['notatrn.trn'].status, by['notatrn.trn'].get]).toEqual(['skipped', '']);
});

test('a row does not rewrite itself once taken', async ({ page }) => {
  // Status says what the file got. Redrawing it into "saved / download again"
  // is movement for its own sake, under the eyes of someone reading the table.
  await load(page);
  const row = page.locator('#rows tr', { hasText: 'bad.trn' }).first();
  const before = await row.textContent();
  const dl = page.waitForEvent('download');
  await row.locator('button.save').click();
  await dl;
  expect(await row.textContent()).toBe(before);
});

test('a folder with nothing wrong offers nothing', async ({ page }) => {
  await page.goto(PAGE);
  const f = path.join(dir, 'clean-only.trn');
  fs.writeFileSync(f, trn({}));
  await page.setInputFiles('#loose', [f]);
  await page.waitForSelector('#rows tr');
  await expect(page.locator('#actions')).toBeHidden();
  await expect(page.locator('#nothing')).toBeVisible();
  fs.rmSync(f);
});

test('a per-file download is the repaired file under its own name', async ({ page }) => {
  await load(page);
  const dl = page.waitForEvent('download');
  await page.locator('#rows tr', { hasText: 'bad.trn' }).first().locator('button.save').click();
  const d = await dl;
  expect(d.suggestedFilename()).toBe('bad.trn');
  const got = fs.readFileSync(await d.path());
  const want = Buffer.from(FIXTURES['bad.trn']);
  want.fill(0, 0x7f, 0x92);
  expect(got.equals(want)).toBe(true);
});

test('the zip holds only the files that changed', async ({ page }) => {
  // A zip that also carried the untouched trainers would invite a player to
  // overwrite files that were fine, for no reason.
  await load(page);
  const names = await page.evaluate(() =>
    scanned.map(output).filter(Boolean).map(o => o.name));
  expect(names.sort()).toEqual(['bad.trn', 'bad_ff.trn']);
});

test('the zip is real, and holds only what it changed', async ({ page }) => {
  await load(page);
  const dl = page.waitForEvent('download');
  await page.locator('#build').click();
  const d = await dl;
  expect(d.suggestedFilename()).toBe('trainers-repaired.zip');
  const buf = fs.readFileSync(await d.path());
  expect(buf.subarray(0, 4).toString('hex')).toBe('504b0304');   // PK\x03\x04
  expect(buf.includes(Buffer.from('bad.trn'))).toBe(true);
  expect(buf.includes(Buffer.from('noptr.trn'))).toBe(false);    // it was already fine
  // Trainers only. A stray .txt in there is one more thing to drop into the
  // game's trainers folder by accident.
  expect(buf.includes(Buffer.from('.txt'))).toBe(false);
});

test('reads trainers out of a deflated zip', async ({ page }) => {
  // "Zip up your trainers folder" is what people do when asked for their files,
  // and Explorer deflates. The bytes that come back out must be the originals,
  // or every verdict below them is about the wrong file.
  await page.goto(PAGE);
  const f = path.join(dir, 'deflated.zip');
  fs.writeFileSync(f, zip({ 'trainers/bad.trn': FIXTURES['bad.trn'],
                            'trainers/noptr.trn': FIXTURES['noptr.trn'],
                            'notes.txt': Buffer.from('ignored') }));
  await page.setInputFiles('#loose', [f]);
  await page.waitForSelector('#rows tr');
  const rows = await page.$$eval('#rows tr', trs => trs.map(tr => tr.children[0].textContent + '|' +
                                                                 tr.children[1].textContent.trim()));
  expect(rows).toEqual(['deflated.zip > trainers/bad.trn|11',
                        'deflated.zip > trainers/noptr.trn|none']);
  const same = await page.evaluate(() =>
    [...scanned.find(s => s.name.endsWith('bad.trn')).bytes]);
  expect(Buffer.from(same).equals(FIXTURES['bad.trn'])).toBe(true);
});

test('reads a stored (uncompressed) zip too', async ({ page }) => {
  await page.goto(PAGE);
  const f = path.join(dir, 'stored.zip');
  fs.writeFileSync(f, zip({ 'bad.trn': FIXTURES['bad.trn'] }, 0));
  await page.setInputFiles('#loose', [f]);
  await page.waitForSelector('#rows tr');
  await expect(page.locator('.stat.flagged .n')).toHaveText('1');
});

test('a repaired file from a zip keeps its plain name in the output', async ({ page }) => {
  // Entries are labelled "archive.zip > path/inside.trn" so a player can tell
  // where a row came from; that label must not leak into the zip they unpack
  // over their trainers folder.
  await page.goto(PAGE);
  const f = path.join(dir, 'stored2.zip');
  fs.writeFileSync(f, zip({ 'trainers/bad.trn': FIXTURES['bad.trn'] }, 0));
  await page.setInputFiles('#loose', [f]);
  await page.waitForSelector('#rows tr');
  const names = await page.evaluate(() =>
    scanned.map(s => s.name.split(/[\\/>]/).pop().trim()));
  expect(names).toEqual(['bad.trn']);
});

test('says so when an archive holds no trainers', async ({ page }) => {
  await page.goto(PAGE);
  const f = path.join(dir, 'empty.zip');
  fs.writeFileSync(f, zip({ 'readme.txt': Buffer.from('nothing here') }, 0));
  await page.setInputFiles('#loose', [f]);
  await expect(page.locator('#summary')).toHaveText(/No \.trn files found/);
  await expect(page.locator('#actions')).toBeHidden();
});

test('shows progress while reading and clears it after', async ({ page }) => {
  // Inflating a season's worth of trainers takes seconds, and a page that looks
  // frozen for that long reads as broken.
  await page.goto(PAGE);
  const many = {};
  for (let i = 0; i < 40; i++) many[`trainers/t${i}.trn`] = FIXTURES['bad.trn'];
  const f = path.join(dir, 'many.zip');
  fs.writeFileSync(f, zip(many));

  const seen = [];
  await page.exposeFunction('note', t => seen.push(t));
  await page.evaluate(() => {
    const el = document.getElementById('ptext');
    new MutationObserver(() => window.note(el.textContent)).observe(el, {
      childList: true, characterData: true, subtree: true });
  });
  await page.setInputFiles('#loose', [f]);
  await page.waitForSelector('#rows tr');

  expect(seen.some(t => /^reading \d+ of 1: many\.zip/.test(t))).toBe(true);
  expect(seen.some(t => /\(\d+: trainers\/t\d+\.trn\)/.test(t))).toBe(true);
  await expect(page.locator('#progress')).toBeHidden();
  await expect(page.locator('#rows tr')).toHaveCount(40);
  fs.rmSync(f);
});

test('throws nothing while doing a full pass', async ({ page }) => {
  // The page is inline JS in an .html, so tests/web/lint.sh does not see it and
  // a typo like a stale variable reference reaches the browser as a runtime
  // error. This shipped once: render() kept using a local that had been
  // deleted, and the table simply stopped drawing. Nothing else catches it.
  const errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  await load(page);
  const dl = page.waitForEvent('download');
  await page.locator('#rows button.save').first().click();
  await dl;
  const zip = page.waitForEvent('download');
  await page.locator('#build').click();
  await zip;
  expect(errors).toEqual([]);
});

test('makes no network requests — the privacy claim is literal', async ({ page }) => {
  // The page tells players it works with the internet disconnected. That is a
  // promise about their own lap data, so it is asserted rather than trusted.
  const offsite = [];
  page.on('request', r => { if (!r.url().startsWith('file:')) offsite.push(r.url()); });
  await load(page);
  expect(offsite).toEqual([]);
});
