/** Exposes a narrow IPC bridge to both renderer windows. */
import { contextBridge, ipcRenderer } from 'electron';

const api = {
  sd: (method: string, ...args: unknown[]): Promise<unknown> => ipcRenderer.invoke('sd', method, ...args),
  wifi: (method: string, ...args: unknown[]): Promise<unknown> => ipcRenderer.invoke('wifi', method, ...args),
  sendScene: (scene: unknown): void => ipcRenderer.send('gfx:scene', scene),
  sendPointer: (kind: string, x: number, y: number): void => ipcRenderer.send('gfx:pointer', kind, x, y),
  sendKey: (data: string): void => ipcRenderer.send('gfx:key', data),
  onScene: (listener: (scene: unknown) => void): void => {
    ipcRenderer.on('gfx:scene', (_event, scene: unknown) => listener(scene));
  },
  onScale: (listener: (scale: number) => void): void => {
    ipcRenderer.on('gfx:scale', (_event, scale: number) => listener(scale));
  },
  onPointer: (listener: (kind: 'down' | 'move' | 'up', x: number, y: number) => void): void => {
    ipcRenderer.on('gfx:pointer', (_event, kind: 'down' | 'move' | 'up', x: number, y: number) => listener(kind, x, y));
  },
  onKey: (listener: (data: string) => void): void => {
    ipcRenderer.on('console:key', (_event, data: string) => listener(data));
  },
};

export type BridgeApi = typeof api;

contextBridge.exposeInMainWorld('basicedge', api);
