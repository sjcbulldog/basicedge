/** Emulated SD card backed by a host folder; a port of fw/common/file_service.c path handling. */
import { promises as fs } from 'node:fs';
import { join } from 'node:path';
import type { FormatResult, SdCardStatus, SdHost } from '../core/host';

export const SD_CAPACITY_MIB = 1024;
const PATH_MAX = 256;
const DIRECTORY_CAPACITY = 8192;

/** file_service_normalize_directory(): collapses "." and ".." (clamped at the root). */
export function normalizeDirectory(path: string): string | null {
  const parts: string[] = [];
  for (const component of path.split('/')) {
    if (component === '' || component === '.') continue;
    if (component === '..') parts.pop();
    else parts.push(component);
  }
  const normalized = parts.join('/');
  return normalized.length < PATH_MAX ? normalized : null;
}

export class SdCard implements SdHost {
  /** emFile represents "/" as an empty path. */
  private current = '';

  constructor(readonly root: string, private readonly seed: Record<string, string> = {}) {}

  /** Creates the card folder, seeding sample programs on first use. */
  async mount(): Promise<void> {
    const exists = await fs.stat(this.root).then((s) => s.isDirectory(), () => false);
    if (exists) return;
    await fs.mkdir(this.root, { recursive: true });
    for (const [name, text] of Object.entries(this.seed)) {
      await fs.writeFile(join(this.root, name), text, 'latin1');
    }
  }

  /**
   * file_service_resolve_path(); additionally normalized so the emulation can never
   * escape the card folder (the device has no parent of "/").
   */
  private resolve(path: string): string | null {
    if (path.includes('\\')) return null;
    let relative: string;
    if (path.startsWith('/')) relative = path.slice(1);
    else if (path === '') relative = this.current;
    else relative = this.current === '' ? path : `${this.current}/${path}`;
    if (relative.length >= PATH_MAX) return null;
    if (relative.split('/').some((part) => part === '..' || part === '.')) {
      const normalized = normalizeDirectory(relative);
      if (normalized === null) return null;
      relative = normalized;
    }
    if (/[<>:"|?*\x00-\x1f]/.test(relative)) return null;
    return relative;
  }

  private hostPath(relative: string): string {
    return relative === '' ? this.root : join(this.root, ...relative.split('/'));
  }

  async directory(): Promise<string | null> {
    const relative = this.resolve('');
    if (relative === null) return null;
    let entries;
    try {
      entries = await fs.readdir(this.hostPath(relative), { withFileTypes: true });
    } catch {
      return null;
    }
    // FAT subdirectories contain "." and ".." entries that emFile reports.
    const rows: string[] = relative === '' ? [] : [`${'.'.padEnd(32)} <DIR>\r\n`, `${'..'.padEnd(32)} <DIR>\r\n`];
    for (const entry of entries) {
      if (entry.isDirectory()) {
        rows.push(`${entry.name.padEnd(32)} <DIR>\r\n`);
      } else if (entry.isFile()) {
        const stat = await fs.stat(join(this.hostPath(relative), entry.name));
        rows.push(`${entry.name.padEnd(32)} ${stat.size}\r\n`);
      }
    }
    const listing = rows.join('');
    return listing.length < DIRECTORY_CAPACITY ? listing : null;
  }

  async getDirectory(): Promise<string | null> {
    return this.current === '' ? '/' : this.current;
  }

  async changeDirectory(path: string): Promise<boolean> {
    const relative = this.resolve(path);
    if (relative === null) return false;
    const isDirectory = await fs.stat(this.hostPath(relative)).then((s) => s.isDirectory(), () => false);
    if (!isDirectory) return false;
    const normalized = normalizeDirectory(relative);
    if (normalized === null) return false;
    this.current = normalized;
    return true;
  }

  /** FS_Remove() deletes files only. */
  async remove(path: string): Promise<boolean> {
    const relative = this.resolve(path);
    if (relative === null || relative === '') return false;
    try {
      const stat = await fs.stat(this.hostPath(relative));
      if (!stat.isFile()) return false;
      await fs.unlink(this.hostPath(relative));
      return true;
    } catch {
      return false;
    }
  }

  async makeDirectory(path: string): Promise<boolean> {
    const relative = this.resolve(path);
    if (relative === null || relative === '') return false;
    try {
      await fs.mkdir(this.hostPath(relative));
      return true;
    } catch {
      return false;
    }
  }

  async load(path: string, capacity: number): Promise<string | null> {
    const relative = this.resolve(path);
    if (relative === null || relative === '') return null;
    try {
      const target = this.hostPath(relative);
      const stat = await fs.stat(target);
      if (!stat.isFile() || stat.size >= capacity) return null;
      return await fs.readFile(target, 'latin1');
    } catch {
      return null;
    }
  }

  async save(path: string, data: string): Promise<boolean> {
    const relative = this.resolve(path);
    if (relative === null || relative === '') return false;
    const target = this.hostPath(relative);
    try {
      const existing = await fs.stat(target).then((s) => (s.isFile() ? s.size : -1), () => 0);
      if (existing < 0) return false;
      const free = await this.freeBytes();
      if (data.length - existing > free) return false;
      await fs.writeFile(target, data, 'latin1');
      return true;
    } catch {
      return false;
    }
  }

  async status(): Promise<SdCardStatus | null> {
    return { inserted: true, cardType: 'SD memory', capacity: 'SDHC', totalMib: SD_CAPACITY_MIB, filesystem: 'FAT32' };
  }

  async freeKib(): Promise<number | null> {
    return Math.floor((await this.freeBytes()) / 1024);
  }

  async format(confirmed: boolean): Promise<FormatResult> {
    if (!confirmed) return 'confirm';
    try {
      for (const entry of await fs.readdir(this.root)) {
        await fs.rm(join(this.root, entry), { recursive: true, force: true });
      }
      this.current = '';
      return 'ok';
    } catch {
      return 'error';
    }
  }

  private async usedBytes(path = this.root): Promise<number> {
    let total = 0;
    for (const entry of await fs.readdir(path, { withFileTypes: true })) {
      const child = join(path, entry.name);
      if (entry.isDirectory()) total += 4096 + (await this.usedBytes(child));
      else if (entry.isFile()) total += Math.ceil((await fs.stat(child)).size / 4096) * 4096;
    }
    return total;
  }

  private async freeBytes(): Promise<number> {
    return Math.max(0, SD_CAPACITY_MIB * 1024 * 1024 - (await this.usedBytes()));
  }
}
