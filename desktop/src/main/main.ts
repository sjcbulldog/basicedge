/** Electron main process: owns the windows, the emulated SD card and the emulated WIFI radio. */
import { app, BrowserWindow, ipcMain, Menu, shell, type MenuItemConstructorOptions } from 'electron';
import { promises as fs } from 'node:fs';
import { join } from 'node:path';
import amazing from '../../assets/amazing.bas';
import grtest from '../../assets/grtest.bas';
import { GFX_HEIGHT, GFX_WIDTH } from '../core/graphics';
import type { SdHost, WifiHost } from '../core/host';
import { SdCard } from './sdcard';
import { Wifi } from './wifi';

const SCALES = [1, 2, 3, 4];

interface Settings {
  scale: number;
}

let settings: Settings = { scale: 2 };
let consoleWindow: BrowserWindow | null = null;
let graphicsWindow: BrowserWindow | null = null;
let lastScene: unknown = null;
let sdcard: SdCard;
let wifi: Wifi;

const settingsFile = (): string => join(app.getPath('userData'), 'settings.json');

async function loadSettings(): Promise<void> {
  try {
    const parsed = JSON.parse(await fs.readFile(settingsFile(), 'utf8')) as Partial<Settings>;
    if (typeof parsed.scale === 'number' && SCALES.includes(parsed.scale)) settings.scale = parsed.scale;
  } catch {
    /* defaults */
  }
}

async function saveSettings(): Promise<void> {
  await fs.writeFile(settingsFile(), JSON.stringify(settings, null, 2)).catch(() => undefined);
}

const preload = (): string => join(__dirname, 'preload.js');

function createConsoleWindow(): void {
  consoleWindow = new BrowserWindow({
    width: 900,
    height: 620,
    title: 'BasicEdge Console',
    backgroundColor: '#000000',
    webPreferences: { preload: preload(), contextIsolation: true, nodeIntegration: false, backgroundThrottling: false },
  });
  void consoleWindow.loadFile(join(__dirname, 'console.html'));
  consoleWindow.on('closed', () => {
    consoleWindow = null;
    graphicsWindow?.close();
  });
}

function openGraphicsWindow(): void {
  if (graphicsWindow) return;
  graphicsWindow = new BrowserWindow({
    useContentSize: true,
    width: GFX_WIDTH * settings.scale,
    height: GFX_HEIGHT * settings.scale,
    resizable: false,
    title: graphicsTitle(),
    backgroundColor: '#add8e6',
    webPreferences: { preload: preload(), contextIsolation: true, nodeIntegration: false, backgroundThrottling: false },
  });
  graphicsWindow.removeMenu();
  graphicsWindow.setContentSize(GFX_WIDTH * settings.scale, GFX_HEIGHT * settings.scale);
  void graphicsWindow.loadFile(join(__dirname, 'graphics.html'));
  graphicsWindow.webContents.on('did-finish-load', () => {
    graphicsWindow?.webContents.send('gfx:scale', settings.scale);
    if (lastScene) graphicsWindow?.webContents.send('gfx:scene', lastScene);
  });
  graphicsWindow.on('closed', () => {
    graphicsWindow = null;
  });
}

const graphicsTitle = (): string => `BasicEdge Graphics ${GFX_WIDTH}x${GFX_HEIGHT} @ ${settings.scale}x`;

function setScale(scale: number): void {
  settings.scale = scale;
  void saveSettings();
  if (graphicsWindow) {
    graphicsWindow.setResizable(true);
    graphicsWindow.setContentSize(GFX_WIDTH * scale, GFX_HEIGHT * scale);
    graphicsWindow.setResizable(false);
    graphicsWindow.setTitle(graphicsTitle());
    graphicsWindow.webContents.send('gfx:scale', scale);
  }
  buildMenu();
}

function buildMenu(): void {
  const template: MenuItemConstructorOptions[] = [
    ...(process.platform === 'darwin' ? [{ role: 'appMenu' as const }] : []),
    {
      label: 'File',
      submenu: [
        { label: 'Open SD Card Folder', click: () => void shell.openPath(sdcard.root) },
        { type: 'separator' },
        { role: 'quit' },
      ],
    },
    { role: 'editMenu' },
    {
      label: 'Graphics',
      submenu: [
        { label: 'Show Graphics Window', accelerator: 'CmdOrCtrl+G', click: () => openGraphicsWindow() },
        { type: 'separator' },
        ...SCALES.map(
          (scale): MenuItemConstructorOptions => ({
            label: `Scale ${scale}x (${GFX_WIDTH * scale} x ${GFX_HEIGHT * scale})`,
            type: 'radio',
            checked: settings.scale === scale,
            accelerator: `CmdOrCtrl+${scale}`,
            click: () => setScale(scale),
          }),
        ),
      ],
    },
    {
      label: 'View',
      submenu: [{ role: 'reload' }, { role: 'toggleDevTools' }, { type: 'separator' }, { role: 'resetZoom' }, { role: 'zoomIn' }, { role: 'zoomOut' }],
    },
  ];
  Menu.setApplicationMenu(Menu.buildFromTemplate(template));
}

/** Forwards a whitelisted method call to a host object. */
function serve<T extends object>(channel: string, target: T, methods: (keyof T)[]): void {
  ipcMain.handle(channel, async (_event, method: string, ...args: unknown[]) => {
    if (!methods.includes(method as keyof T)) throw new Error(`Unknown ${channel} method ${method}`);
    const fn = target[method as keyof T] as unknown as (...a: unknown[]) => Promise<unknown>;
    return fn.apply(target, args);
  });
}

async function main(): Promise<void> {
  await app.whenReady();
  await loadSettings();
  const userData = app.getPath('userData');
  sdcard = new SdCard(join(userData, 'sdcard'), { 'amazing.bas': amazing, 'grtest.bas': grtest });
  await sdcard.mount();
  wifi = new Wifi(join(userData, 'wifi.json'));
  await wifi.open();

  serve<SdHost>('sd', sdcard, ['directory', 'getDirectory', 'changeDirectory', 'remove', 'makeDirectory', 'load', 'save', 'status', 'freeKib', 'format']);
  serve<WifiHost>('wifi', wifi, ['status', 'scan', 'stored', 'clearStored', 'disconnect', 'connect', 'download']);

  ipcMain.on('gfx:scene', (_event, scene: unknown) => {
    lastScene = scene;
    if (!graphicsWindow) openGraphicsWindow();
    else if (!graphicsWindow.webContents.isLoading()) graphicsWindow.webContents.send('gfx:scene', scene);
  });
  ipcMain.on('gfx:pointer', (_event, kind: string, x: number, y: number) => {
    consoleWindow?.webContents.send('gfx:pointer', kind, x, y);
  });
  ipcMain.on('gfx:key', (_event, data: string) => {
    consoleWindow?.webContents.send('console:key', data);
  });

  buildMenu();
  createConsoleWindow();
  app.on('activate', () => {
    if (!consoleWindow) createConsoleWindow();
  });
}

app.on('window-all-closed', () => app.quit());
void main();
