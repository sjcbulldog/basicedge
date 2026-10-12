/** Services the core console needs from its environment (Electron main process or tests). */

export interface SdCardStatus {
  inserted: boolean;
  cardType: string;
  capacity: string;
  totalMib: number;
  filesystem: string;
}

export type FormatResult = 'ok' | 'confirm' | 'error';

/** Mirrors fw/common/file_service.h. File data is passed as byte strings (latin1). */
export interface SdHost {
  directory(): Promise<string | null>;
  getDirectory(): Promise<string | null>;
  changeDirectory(path: string): Promise<boolean>;
  remove(path: string): Promise<boolean>;
  makeDirectory(path: string): Promise<boolean>;
  /** Returns null when missing, unreadable or size >= capacity. */
  load(path: string, capacity: number): Promise<string | null>;
  save(path: string, data: string): Promise<boolean>;
  status(): Promise<SdCardStatus | null>;
  freeKib(): Promise<number | null>;
  format(confirmed: boolean): Promise<FormatResult>;
}

export interface WifiScanRecord {
  ssid: string;
  rssi: number;
  channel: number;
}

/** Mirrors fw/proj_cm55 wifi_service.h. */
export interface WifiHost {
  status(): Promise<string | null>;
  scan(): Promise<WifiScanRecord[] | null>;
  stored(): Promise<string[] | null>;
  clearStored(): Promise<boolean>;
  disconnect(): Promise<boolean>;
  connect(ssid: string, password: string): Promise<{ connected: boolean; saved: boolean }>;
  /** HTTP(S) GET; null on failure or when the body is longer than capacity. */
  download(url: string, capacity: number): Promise<string | null>;
}

export interface ConsoleHost {
  write(text: string): void;
  sd: SdHost;
  wifi: WifiHost;
  now(): number;
  yieldNow(): Promise<void>;
}
