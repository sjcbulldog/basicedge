/** Console window: hosts the BASIC core, an xterm terminal and the bridge to the graphics window. */
import { FitAddon } from '@xterm/addon-fit';
import { Terminal } from '@xterm/xterm';
import fontUrl from '@fontsource/montserrat/files/montserrat-latin-500-normal.woff2';
import { BasicConsole } from '../core/console';
import { Graphics } from '../core/graphics';
import type { ConsoleHost, SdHost, WifiHost } from '../core/host';
import type { BridgeApi } from '../main/preload';

declare global {
  interface Window {
    basicedge: BridgeApi;
  }
}

const api = window.basicedge;

/** Builds a typed proxy that forwards every call over IPC. */
function remote<T extends object>(call: (method: string, ...args: unknown[]) => Promise<unknown>): T {
  return new Proxy({} as T, {
    get: (_target, method: string) => (...args: unknown[]) => call(method, ...args),
  });
}

async function loadFont(): Promise<void> {
  try {
    const face = new FontFace('Montserrat', `url(${fontUrl})`, { weight: '500' });
    document.fonts.add(await face.load());
  } catch {
    /* falls back to a system sans-serif font */
  }
}

function canvasMeasure(): (text: string, size: number) => number {
  const context = new OffscreenCanvas(1, 1).getContext('2d')!;
  const cache = new Map<string, number>();
  return (text, size) => {
    const key = `${size}\u0000${text}`;
    let width = cache.get(key);
    if (width === undefined) {
      context.font = `500 ${size}px Montserrat, sans-serif`;
      width = Math.round(context.measureText(text).width);
      if (cache.size > 4096) cache.clear();
      cache.set(key, width);
    }
    return width;
  };
}

async function main(): Promise<void> {
  await loadFont();
  const terminal = new Terminal({
    convertEol: true,
    cursorBlink: true,
    fontFamily: 'Cascadia Mono, Consolas, Menlo, monospace',
    fontSize: 15,
    scrollback: 5000,
    theme: { background: '#000000', foreground: '#d0d0d0' },
  });
  const fit = new FitAddon();
  terminal.loadAddon(fit);
  terminal.open(document.getElementById('terminal')!);
  fit.fit();
  window.addEventListener('resize', () => fit.fit());
  terminal.focus();

  const host: ConsoleHost = {
    write: (text) => terminal.write(text),
    sd: remote<SdHost>(api.sd),
    wifi: remote<WifiHost>(api.wifi),
    now: () => performance.now(),
    yieldNow: () => new Promise((resolve) => setTimeout(resolve, 0)),
  };

  const graphics = new Graphics(canvasMeasure(), () => performance.now());
  let pending = false;
  graphics.onChange = () => {
    if (pending) return;
    pending = true;
    setTimeout(() => {
      pending = false;
      api.sendScene(graphics.scene());
    }, 16);
  };
  api.onPointer((kind, x, y) => graphics.pointer(kind, x, y));

  const console = new BasicConsole(host, graphics);
  terminal.onData((data) => console.key(data));
  api.onKey((data) => console.key(data));
  void console.start();
}

void main();
