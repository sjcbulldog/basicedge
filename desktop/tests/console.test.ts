import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { after, test } from 'node:test';
import { BasicConsole } from '../src/core/console';
import { Graphics } from '../src/core/graphics';
import type { ConsoleHost } from '../src/core/host';
import { SdCard } from '../src/main/sdcard';
import { Wifi } from '../src/main/wifi';

const root = join(__dirname, '..');
const temp = mkdtempSync(join(tmpdir(), 'basicedge-'));
after(() => rmSync(temp, { recursive: true, force: true }));

let counter = 0;

async function setup(pages: Record<string, string> = {}): Promise<{ console: BasicConsole; out: () => string; graphics: Graphics; sd: SdCard }> {
  const sd = new SdCard(join(temp, `card${counter++}`), { 'amazing.bas': readFileSync(join(root, 'assets', 'amazing.bas'), 'latin1') });
  await sd.mount();
  const wifi = new Wifi(null, async (url) => {
    const body = pages[url];
    return { status: body === undefined ? 404 : 200, arrayBuffer: async () => new Uint8Array(Buffer.from(body ?? '', 'latin1')).buffer };
  });
  let output = '';
  const host: ConsoleHost = {
    write: (text) => (output += text),
    sd,
    wifi,
    now: () => Date.now(),
    yieldNow: () => new Promise((resolve) => setImmediate(resolve)),
  };
  const graphics = new Graphics();
  const console = new BasicConsole(host, graphics);
  return {
    console,
    graphics,
    sd,
    out: () => {
      const value = output;
      output = '';
      return value;
    },
  };
}

test('program entry, LIST, RUN and NEW', async () => {
  const { console, out } = await setup();
  await console.execute('20 PRINT "B"');
  await console.execute('10 PRINT "A";');
  await console.execute('LIST');
  assert.equal(out(), '10 PRINT "A";\n20 PRINT "B"\n');
  await console.execute('RUN');
  assert.equal(out(), 'AB\n');
  await console.execute('20');
  await console.execute('LIST');
  assert.equal(out(), '10 PRINT "A";\n');
  await console.execute('NEW');
  await console.execute('LIST');
  assert.doesNotMatch(out(), /PRINT/);
});

test('immediate mode keeps variables, RUN clears them', async () => {
  const { console, out } = await setup();
  await console.execute('X=5');
  await console.execute('PRINT X*2');
  assert.equal(out(), '10\n');
  await console.execute('10 PRINT X');
  await console.execute('RUN');
  assert.equal(out(), '0\n');
});

test('MEM reports program memory and dynamic variable memory', async () => {
  const { console, out } = await setup();
  await console.execute('MEM');
  assert.match(out(), /Program memory:\r\n  Stored program:\s+0 bytes\r\n  Available space:\s+32768 bytes\r\n  Total capacity:\s+32768 bytes \(32 KiB\)\r\nVariable memory:\r\n  Available space:\s+dynamic \(host memory\)\r\n/);
  await console.execute('10 PRINT "HELLO"');
  await console.execute('MEM');
  const text = out();
  assert.match(text, /Stored program:\s+17 bytes/);
  assert.match(text, /Available space:\s+32751 bytes/);
  await console.execute('MEM invalid');
  assert.equal(out(), 'Usage: MEM\r\n');
  await console.execute('HELP');
  assert.match(out(), /MEM - Show memory available for programs and variables\./);
});

test('errors report the source line', async () => {
  const { console, out } = await setup();
  await console.execute('10 X=1');
  await console.execute('20 RETURN');
  await console.execute('RUN');
  assert.match(out(), /MY-BASIC error 29 at 20:\d+: No return point\r\nCommand failed: 3\r\n/);
});

test('SD card: SAVE, DIR, LOAD, MKDIR, CD, PWD, DEL', async () => {
  const { console, out, sd } = await setup();
  await console.execute('10 PRINT "HI"');
  await console.execute('SAVE "hello.bas"');
  assert.equal(out(), 'Saved program: hello.bas\r\n');
  await console.execute('DIR');
  const listing = out();
  assert.match(listing, /amazing\.bas\s+\d+\r\n/);
  assert.match(listing, /hello\.bas {24}14\r\n/);
  await console.execute('NEW');
  await console.execute('LOAD "hello.bas"');
  assert.match(out(), /Loaded program/);
  await console.execute('RUN');
  assert.equal(out(), 'HI\n');
  await console.execute('MKDIR "games"');
  await console.execute('CD "games"');
  await console.execute('PWD');
  assert.equal(out(), 'Created directory: games\r\nDirectory changed.\r\ngames\r\n');
  await console.execute('DIR');
  assert.equal(out(), `${'.'.padEnd(32)} <DIR>\r\n${'..'.padEnd(32)} <DIR>\r\n`);
  await console.execute('CD ".."');
  await console.execute('CD "../.."');
  await console.execute('PWD');
  assert.equal(out(), 'Directory changed.\r\nDirectory changed.\r\n/\r\n');
  await console.execute('DEL "hello.bas"');
  assert.equal(out(), 'Deleted file: hello.bas\r\n');
  assert.equal(await sd.load('../../outside.bas', 100), null);
  assert.equal(await sd.save('../escape.bas', 'x'), true);
  assert.notEqual(await sd.load('/escape.bas', 100), null);
});

test('LOAD normalizes CRLF and runs Amazing from the card', async () => {
  const { console, out, sd } = await setup();
  writeFileSync(join(sd.root, 'crlf.bas'), '10 PRINT "OK"\r\n\r\n20 END\r\n');
  await console.execute('LOAD "crlf.bas"');
  out();
  await console.execute('LIST');
  assert.equal(out(), '10 PRINT "OK"\n20 END\n');
});

test('SDCARD STATUS and FORMAT confirmation', async () => {
  const { console, out } = await setup();
  await console.execute('SDCARD STATUS');
  assert.match(out(), /SD card: SD memory, SDHC, 1024 MiB, filesystem: FAT32/);
  await console.execute('SDCARD FORMAT');
  assert.match(out(), /YES/);
  await console.execute('SDCARD FORMAT YES');
  out();
  await console.execute('DIR');
  assert.equal(out(), '');
});

test('WIFI status, disconnect, connect and download', async () => {
  const program = '10 PRINT "NET"\n';
  const { console, out } = await setup({ 'http://example.test/net.bas': program });
  await console.execute('WIFI STATUS');
  assert.match(out(), /WIFI connected to "/);
  await console.execute('WIFI LOAD "http://example.test/net.bas"');
  out();
  await console.execute('RUN');
  assert.equal(out(), 'NET\n');
  await console.execute('WIFI DISCONNECT');
  await console.execute('WIFI STATUS');
  assert.equal(out(), 'WIFI disconnected.\r\nWIFI is not connected.\r\n');
  await console.execute('WIFI CONNECT "Lab" "secret"');
  await console.execute('WIFI STATUS');
  assert.equal(out(), 'WIFI connected; credentials saved.\r\nWIFI connected to "Lab".\r\n');
});

test('graphics showcase builds a scene', async () => {
  const { console, out, graphics, sd } = await setup();
  let changes = 0;
  graphics.onChange = () => changes++;
  writeFileSync(join(sd.root, 'grtest.bas'), readFileSync(join(root, 'assets', 'grtest.bas')));
  await console.execute('LOAD "grtest.bas"');
  out();
  await console.execute('RUN');
  assert.equal(out(), 'Graphics showcase ready: 800 X 480\n');
  const scene = graphics.scene();
  assert.equal(scene.width, 800);
  assert.ok(scene.nodes.some((node) => node.type === 'SWITCH'));
  assert.ok(scene.nodes.some((node) => node.type === 'BUTTON'));
  assert.ok(scene.nodes.some((node) => node.lines.includes('LAUNCH DIAGNOSTICS')));
  assert.ok(changes > 0);
  for (const node of scene.nodes) {
    assert.ok(node.x >= 0 && node.y >= 0 && node.x + node.w <= 800 && node.y + node.h <= 480, `${node.type} ${node.x},${node.y} ${node.w}x${node.h}`);
  }
  await console.execute('GFX LIST');
  assert.match(out(), /14: BUTTON parent=2/);
  const toggle = scene.nodes.find((node) => node.type === 'SWITCH')!;
  graphics.pointer('down', toggle.x + 5, toggle.y + 5);
  graphics.pointer('up', toggle.x + 5, toggle.y + 5);
  await console.execute('PRINT GFX_CHECKED(TOGGLE)');
  assert.equal(out(), '1\n');
  await console.execute('GFX CLEAR');
  assert.equal(out(), 'Graphics cleared.\r\n');
  assert.equal(graphics.scene().nodes.length, 1);
});

test('grtrek builds its display, plays and cleans up', async () => {
  const { console, out, graphics, sd } = await setup();
  writeFileSync(join(sd.root, 'grtrek.bas'), readFileSync(join(root, 'assets', 'grtrek.bas')));
  await console.execute('LOAD "grtrek.bas"');
  out();
  const run = console.execute('RUN');
  const settle = (ms = 400) => new Promise((resolve) => setTimeout(resolve, ms));
  const labels = () => graphics.scene().nodes.filter((node) => node.type === 'LABEL').map((node) => node.lines.join('|')).join('\n');
  try {
    await settle();
    assert.match(out(), /GFX CLEAR/);
    assert.equal(graphics.list().length, 62);
    for (const node of graphics.scene().nodes) {
      assert.ok(node.x >= 0 && node.y >= 0 && node.x + node.w <= 800 && node.y + node.h <= 480, `${node.type} ${node.x},${node.y} ${node.w}x${node.h}`);
    }
    assert.match(labels(), /YOUR ORDERS/);
    for (const key of ['D', 'A', 'S', 'P', 'T', 'N', '?']) {
      console.key(key);
      await settle(1200);
    }
    assert.doesNotMatch(out(), /error/i);
    assert.match(labels(), /KEYS: N=NAV/);
    const switches = graphics.scene().nodes.filter((node) => node.type === 'SWITCH');
    assert.equal(switches.length, 8);
    graphics.pointer('down', switches[1].x + 5, switches[1].y + 5);
    graphics.pointer('up', switches[1].x + 5, switches[1].y + 5);
    await settle(600);
    assert.match(labels(), /LONG RANGE (SCAN|SENSORS ARE INOPERABLE)/);
    console.key('X');
    await settle(600);
    console.key('X');
    await settle(600);
    console.key(' ');
    await run;
    assert.match(out(), /GRTREK ENDED/);
    assert.equal(graphics.list().length, 1);
  } finally {
    console.key('\x03');
    await run;
  }
});