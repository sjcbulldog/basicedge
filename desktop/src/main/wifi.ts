/** Emulated WIFI service: the desktop is always online, downloads use the host network. */
import { promises as fs } from 'node:fs';
import type { WifiHost, WifiScanRecord } from '../core/host';

const DEFAULT_SSID = 'Desktop network';

interface WifiState {
  stored: string[];
  ssid: string;
}

export type Fetcher = (url: string) => Promise<{ status: number; arrayBuffer(): Promise<ArrayBuffer> }>;

export class Wifi implements WifiHost {
  private state: WifiState = { stored: [], ssid: DEFAULT_SSID };
  /** WIFI DISCONNECT is honoured until the next WIFI CONNECT. */
  private disconnected = false;

  constructor(private readonly stateFile: string | null, private readonly fetcher: Fetcher = (url) => fetch(url, { redirect: 'follow' })) {}

  async open(): Promise<void> {
    if (!this.stateFile) return;
    try {
      const parsed = JSON.parse(await fs.readFile(this.stateFile, 'utf8')) as Partial<WifiState>;
      if (Array.isArray(parsed.stored)) this.state.stored = parsed.stored.filter((s) => typeof s === 'string');
      if (typeof parsed.ssid === 'string' && parsed.ssid) this.state.ssid = parsed.ssid;
    } catch {
      /* first run */
    }
  }

  private async persist(): Promise<boolean> {
    if (!this.stateFile) return true;
    try {
      await fs.writeFile(this.stateFile, JSON.stringify(this.state, null, 2));
      return true;
    } catch {
      return false;
    }
  }

  async status(): Promise<string | null> {
    return this.disconnected ? null : this.state.ssid;
  }

  /** The firmware cannot scan while connected, and the emulated radio is always connected. */
  async scan(): Promise<WifiScanRecord[] | null> {
    return null;
  }

  async stored(): Promise<string[] | null> {
    return [...this.state.stored];
  }

  async clearStored(): Promise<boolean> {
    this.state.stored = [];
    return this.persist();
  }

  async disconnect(): Promise<boolean> {
    this.disconnected = true;
    return true;
  }

  async connect(ssid: string, _password: string): Promise<{ connected: boolean; saved: boolean }> {
    this.disconnected = false;
    this.state.ssid = ssid;
    if (!this.state.stored.includes(ssid)) this.state.stored.push(ssid);
    return { connected: true, saved: await this.persist() };
  }

  async download(url: string, capacity: number): Promise<string | null> {
    if (this.disconnected || !/^https?:\/\//i.test(url)) return null;
    try {
      const response = await this.fetcher(url);
      if (response.status !== 200) return null;
      const body = Buffer.from(await response.arrayBuffer());
      if (body.length > capacity) return null;
      return body.toString('latin1');
    } catch {
      return null;
    }
  }
}
