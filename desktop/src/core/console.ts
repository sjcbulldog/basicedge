/**
 * Port of the firmware console: basic_task.c (REPL loop, line editor, error handler)
 * and basic_console.c (program storage and commands).
 */
import trekProgram from '../../assets/trek.bas';
import { CatalogProgram, findProgram, parseCatalog } from './catalog';
import { GFX_HEIGHT, GFX_MAX_OBJECTS, GFX_WIDTH, Graphics } from './graphics';
import { ConsoleHost } from './host';
import { TranslateError, setTrace, translate } from './translator';
import { MB_EXTENDED_ABORT, MB_FUNC_END, MB_FUNC_ERR, MB_FUNC_OK, Vm } from './vm';

const PROGRAM_CAPACITY = 32 * 1024;
const MAX_LINES = 640;
const STORED_LINE_SIZE = 272;
const MAX_LINE_NUMBER = 65535;
const INPUT_SIZE = 256;
const PATH_MAX = 256;
const URL_MAX = 512;
const CATALOG_FILENAME_SIZE = 64;
const CATALOG_BASE_URL = 'https://www.mewserver.org/basic/';
const CATALOG_URL = `${CATALOG_BASE_URL}progs.json`;
const CTRL_C = '\x03';
const VERSION = '1.2.2';

const isSpace = (c: string | undefined): boolean => c !== undefined && c !== '' && ' \t\n\v\f\r'.includes(c);
const isDigit = (c: string | undefined): boolean => c !== undefined && c >= '0' && c <= '9';
const isAlpha = (c: string | undefined): boolean => c !== undefined && /^[A-Za-z]$/.test(c);
const isAlnum = (c: string | undefined): boolean => isAlpha(c) || isDigit(c);
const skipSpaces = (text: string): string => text.replace(/^[ \t\n\v\f\r]+/, '');

/** strtoul(text, &end, 10) after the caller has checked for a digit. */
function strtoul(text: string): { value: number; rest: string } {
  const match = /^[ \t\n\v\f\r]*([+-]?)(\d+)/.exec(text);
  if (!match) return { value: 0, rest: text };
  let value = Number(match[2]);
  if (!Number.isSafeInteger(value) || value > 0xffffffff) value = 0xffffffff;
  if (match[1] === '-') value = (0x100000000 - value) % 0x100000000;
  return { value, rest: text.slice(match[0].length) };
}

/** command_match(): case-insensitive keyword followed by whitespace or end of line. */
function commandMatch(input: string, command: string): string | null {
  const text = skipSpaces(input);
  if (text.slice(0, command.length).toUpperCase() !== command) return null;
  const after = text.slice(command.length);
  if (after !== '' && !isSpace(after[0])) return null;
  return skipSpaces(after);
}

function parseLineNumber(text: string): { number: number; body: string } | null {
  const trimmed = skipSpaces(text);
  if (!isDigit(trimmed[0])) return null;
  const { value, rest } = strtoul(trimmed);
  if (value < 1 || value > MAX_LINE_NUMBER) return null;
  return { number: value, body: skipSpaces(rest) };
}

const sameName = (text: string, name: string): boolean => text.toUpperCase() === name.toUpperCase();

/** The desktop accepts CRLF files and blank lines that firmware users would have stripped. */
function normalizeProgram(text: string): string {
  const lines = text.replace(/\r\n?/g, '\n').split('\n').filter((line) => line.trim() !== '');
  return lines.length ? `${lines.join('\n')}\n` : '';
}

export class BasicConsole {
  readonly vm: Vm;
  private program = '';
  private traceEnabled = false;
  private runtimeNumbered = false;
  private runtimeMap: number[] = [];
  private catalog: CatalogProgram[] = [];
  private interruptRequested = false;
  private pendingKey = '';
  private readonly keys: string[] = [];
  private keyWaiter: (() => void) | null = null;
  private discardLineFeed = false;
  /** True while a BASIC program executes (keys go to INKEY$ / Ctrl-C). */
  private running = false;
  private readingLine = false;
  private sleepWake: (() => void) | null = null;
  private started = false;

  constructor(private readonly host: ConsoleHost, readonly graphics: Graphics) {
    this.vm = new Vm({
      write: (text) => host.write(text),
      readLine: () => this.readProgramInput(),
      sleep: (seconds) => this.sleep(seconds),
      takeKey: () => {
        const key = this.pendingKey;
        this.pendingKey = '';
        return key;
      },
      interrupted: () => this.interruptRequested,
      reportError: (code, description, row, column) => this.errorHandler(code, description, row, column),
      yieldNow: () => host.yieldNow(),
      now: () => host.now(),
      traceStep: (rowText) => {
        if (this.traceEnabled && !this.runtimeNumbered) host.write(`TRACE ${rowText}\r\n`);
      },
    });
    graphics.register(this.vm);
  }

  private print(text: string): void {
    this.host.write(text);
  }

  /* ------------------------------------------------------------ keys */

  /** Terminal input (may contain several characters, e.g. a paste). */
  key(data: string): void {
    for (const c of data) {
      if (this.running && !this.readingLine) {
        if (c === CTRL_C) {
          this.interruptRequested = true;
        } else {
          this.pendingKey = c;
        }
        this.sleepWake?.();
        continue;
      }
      this.keys.push(c);
    }
    const waiter = this.keyWaiter;
    this.keyWaiter = null;
    waiter?.();
  }

  private async readChar(): Promise<string> {
    while (this.keys.length === 0) {
      await new Promise<void>((resolve) => (this.keyWaiter = resolve));
    }
    return this.keys.shift()!;
  }

  /** basic_uart_read_line(); returns null when Ctrl-C was pressed. */
  private async readLine(): Promise<string | null> {
    let line = '';
    while (line.length < INPUT_SIZE - 1) {
      const c = await this.readChar();
      if (c === CTRL_C) {
        this.interruptRequested = true;
        this.print('^C\r\n');
        return null;
      }
      if (this.discardLineFeed) {
        this.discardLineFeed = false;
        if (c === '\n') continue;
      }
      if (c === '\r' || c === '\n') {
        this.discardLineFeed = c === '\r';
        this.print('\r\n');
        break;
      }
      if ((c === '\b' || c === '\x7f') && line.length > 0) {
        line = line.slice(0, -1);
        this.print('\b \b');
      } else if (c >= ' ' && c <= '~') {
        line += c;
        this.print(c);
      }
    }
    return line;
  }

  private async readProgramInput(): Promise<string | null> {
    this.readingLine = true;
    try {
      // Keys typed before the prompt appeared were consumed by the runtime, as on the device.
      return await this.readLine();
    } finally {
      this.readingLine = false;
    }
  }

  private sleep(seconds: number): Promise<boolean> {
    return new Promise((resolve) => {
      const finish = (): void => {
        clearTimeout(timer);
        this.sleepWake = null;
        resolve(!this.interruptRequested);
      };
      const timer = setTimeout(finish, seconds * 1000);
      this.sleepWake = () => {
        if (this.interruptRequested || this.pendingKey !== '') finish();
      };
      if (this.interruptRequested) finish();
    });
  }

  /** basic_error_handler(). */
  private errorHandler(code: number, description: string, row: number, column: number): void {
    if (this.interruptRequested || code === 0) return;
    const line = this.runtimeNumbered && row > 0 && row <= this.runtimeMap.length ? this.runtimeMap[row - 1] : row;
    this.print(`MY-BASIC error ${code} at ${line}:${column}: ${description}\r\n`);
  }

  /* ------------------------------------------------------------ REPL */

  /** basic_task(): runs forever. */
  async start(): Promise<void> {
    if (this.started) return;
    this.started = true;
    this.print(`Starting MY-BASIC ${VERSION} on CM55\r\n`);
    this.initialize();
    for (;;) {
      this.print('BASIC> ');
      this.interruptRequested = false;
      this.pendingKey = '';
      const input = await this.readLine();
      if (!input) continue;
      await this.execute(input);
    }
  }

  /** Runs one console line and reports its status like basic_task(). */
  async execute(input: string): Promise<void> {
    let status = await this.process(input);
    if (status === null) status = await this.runImmediate(input);
    if (status === MB_FUNC_END) status = MB_FUNC_OK;
    if (this.interruptRequested) {
      this.print('Break\r\n');
      this.interruptRequested = false;
    } else if (status !== MB_FUNC_OK) {
      this.print(`Command failed: ${status}\r\n`);
    }
  }

  private async runImmediate(input: string): Promise<number> {
    this.runtimeNumbered = false;
    this.runtimeMap = [];
    let text: string;
    try {
      text = translate(input, false).text;
    } catch (error) {
      if (error instanceof TranslateError) {
        this.print(`BASIC parser: ${error.message}\r\n`);
        return MB_FUNC_ERR;
      }
      throw error;
    }
    return this.runTranslated(text, this.traceEnabled);
  }

  private async runTranslated(text: string, immediateTrace: boolean): Promise<number> {
    try {
      this.vm.load(text);
    } catch {
      return MB_FUNC_ERR;
    }
    this.vm.immediateTrace = immediateTrace;
    this.vm.resetColumn();
    this.running = true;
    try {
      const status = await this.vm.run();
      return status === MB_EXTENDED_ABORT && !this.interruptRequested ? MB_FUNC_ERR : status;
    } finally {
      this.running = false;
      this.vm.immediateTrace = false;
    }
  }

  /** basic_console_initialize(). */
  initialize(): void {
    this.program = '';
    this.traceEnabled = false;
  }

  get programText(): string {
    return this.program;
  }

  /* -------------------------------------------------------- storage */

  private findLine(lineNumber: number): { offset: number; found: boolean } {
    let offset = 0;
    while (offset < this.program.length) {
      const stored = parseLineNumber(this.program.slice(offset, offset + 16))?.number ?? 0;
      if (stored >= lineNumber) return { offset, found: stored === lineNumber };
      const end = this.program.indexOf('\n', offset);
      if (end < 0) break;
      offset = end + 1;
    }
    return { offset, found: false };
  }

  private storeProgramLine(input: string): void {
    const parsed = parseLineNumber(input);
    if (!parsed) {
      this.print('Invalid line number.\r\n');
      return;
    }
    const { offset, found } = this.findLine(parsed.number);
    let oldLength = 0;
    if (found) {
      const end = this.program.indexOf('\n', offset);
      oldLength = end >= 0 ? end - offset + 1 : this.program.length - offset;
    }
    if (parsed.body === '') {
      if (found) this.program = this.program.slice(0, offset) + this.program.slice(offset + oldLength);
      return;
    }
    const stored = `${parsed.number} ${parsed.body}\n`;
    if (stored.length >= STORED_LINE_SIZE) {
      this.print('Program line is too long.\r\n');
      return;
    }
    if (this.program.length - oldLength + stored.length >= PROGRAM_CAPACITY) {
      this.print('Out of BASIC program memory.\r\n');
      return;
    }
    this.program = this.program.slice(0, offset) + stored + this.program.slice(offset + oldLength);
  }

  private nextLineOffset(offset: number): number {
    const end = this.program.indexOf('\n', offset);
    return end >= 0 ? end + 1 : this.program.length;
  }

  private listProgram(argumentsText: string): void {
    const usage = (): void => this.print('Usage: LIST [line|first-last]\r\n');
    let first = 1;
    let last = MAX_LINE_NUMBER;
    let cursor = skipSpaces(argumentsText);
    if (cursor !== '') {
      if (!/^[ \t]*[+-]?\d/.test(cursor)) return usage();
      let parsed = strtoul(cursor);
      if (parsed.value > MAX_LINE_NUMBER) return usage();
      first = parsed.value;
      last = first;
      cursor = skipSpaces(parsed.rest);
      if (cursor[0] === '-') {
        const rangeStart = skipSpaces(cursor.slice(1));
        if (!/^[+-]?\d/.test(rangeStart)) return usage();
        parsed = strtoul(rangeStart);
        if (parsed.value > MAX_LINE_NUMBER) return usage();
        last = parsed.value;
        if (first > last) return usage();
        cursor = parsed.rest;
      }
      if (skipSpaces(cursor) !== '') return usage();
    }
    let offset = 0;
    let listed = false;
    while (offset < this.program.length) {
      const parsed = parseLineNumber(this.program.slice(offset, offset + 16));
      if (!parsed) {
        this.print('Malformed BASIC program text.\r\n');
        return;
      }
      const next = this.nextLineOffset(offset);
      if (parsed.number >= first && parsed.number <= last) {
        this.print(this.program.slice(offset, next));
        listed = true;
      }
      offset = next;
    }
    if (!listed && this.program.length === 0) this.print('No BASIC program is stored.\r\n');
  }

  private renumber(argumentsText: string): void {
    let start = 10;
    let step = 10;
    if (argumentsText !== '') {
      let parsed = strtoul(argumentsText);
      start = parsed.value;
      let end = skipSpaces(parsed.rest);
      if (!isDigit(skipSpaces(argumentsText)[0]) && !/^[+-]\d/.test(skipSpaces(argumentsText))) start = 0;
      if (end[0] === ',') {
        const stepText = skipSpaces(end.slice(1));
        parsed = strtoul(stepText);
        step = /^[+-]?\d/.test(stepText) ? parsed.value : 0;
        end = parsed.rest;
      }
      if (start < 1 || step < 1 || skipSpaces(end) !== '') {
        this.print('Usage: RENUM [start[,step]]\r\n');
        return;
      }
    }
    const map: { oldNumber: number; newNumber: number }[] = [];
    let offset = 0;
    while (offset < this.program.length && map.length < MAX_LINES) {
      const parsed = parseLineNumber(this.program.slice(offset, offset + 16));
      if (!parsed) {
        this.print('Cannot renumber malformed program text.\r\n');
        return;
      }
      const newNumber = start + map.length * step;
      if (newNumber > MAX_LINE_NUMBER) {
        this.print('RENUM exceeds maximum line number.\r\n');
        return;
      }
      map.push({ oldNumber: parsed.number, newNumber });
      offset = this.nextLineOffset(offset);
    }
    if (offset < this.program.length) {
      this.print('Too many BASIC program lines.\r\n');
      return;
    }
    const mapped = (old: number): number => map.find((entry) => entry.oldNumber === old)?.newNumber ?? old;
    let output = '';
    offset = 0;
    for (const entry of map) {
      const next = this.nextLineOffset(offset);
      const line = this.program.slice(offset, next);
      output += `${entry.newNumber} ${renumberBody(parseLineNumber(line)!.body, mapped)}`;
      offset = next;
    }
    if (output.length >= PROGRAM_CAPACITY) {
      this.print('Renumbered program exceeds available memory.\r\n');
      return;
    }
    this.program = output;
    this.print('Program renumbered.\r\n');
  }

  /** parse_filename(): a quoted string or the value of a string variable. */
  private parseFilename(argumentsText: string, capacity: number): string | null {
    const cursor = skipSpaces(argumentsText);
    let filename: string;
    if (cursor[0] === '"') {
      const end = cursor.indexOf('"', 1);
      if (end < 0) return null;
      filename = cursor.slice(1, end);
      if (filename.length === 0 || filename.length >= capacity || skipSpaces(cursor.slice(end + 1)) !== '') return null;
      if (filename.includes('\\')) return null;
    } else {
      const match = /^[A-Za-z0-9_$]*/.exec(cursor)![0];
      if (match.length === 0 || match.length >= 32 || skipSpaces(cursor.slice(match.length)) !== '') return null;
      if (!this.vm.hasVariable(match)) return null;
      const value = this.vm.getVariable(match);
      if (typeof value !== 'string' || value.length >= capacity) return null;
      filename = value;
    }
    return filename;
  }

  private storeDownloaded(text: string): void {
    this.program = normalizeProgram(text);
  }

  private async downloadCatalog(): Promise<boolean> {
    const json = await this.host.wifi.download(CATALOG_URL, PROGRAM_CAPACITY - 1);
    if (json === null) {
      this.print('Unable to download BASIC program catalog.\r\n');
      return false;
    }
    const programs = parseCatalog(json);
    if (!programs) {
      this.print('BASIC program catalog is invalid.\r\n');
      return false;
    }
    this.catalog = programs;
    return true;
  }

  private async listCatalog(): Promise<void> {
    if (!(await this.downloadCatalog())) return;
    if (this.catalog.length === 0) {
      this.print('No BASIC programs available.\r\n');
      return;
    }
    const field = (text: string, width: number): string =>
      text.replace(/[\x00-\x1f\x7f]/g, ' ').padEnd(width, ' ');
    this.print(`${'NAME'.padEnd(24)} ${'TYPE'.padEnd(10)} ${'SIZE'.padStart(6)}  DESCRIPTION\r\n`);
    for (const program of this.catalog) {
      this.print(`${field(program.filename, 24)} ${field(program.type, 10)} ${String(program.size).padStart(6)}  ${field(program.description, 0)}\r\n`);
    }
    this.print(`${this.catalog.length} program(s) available.\r\n`);
  }

  private async loadCatalogProgram(name: string): Promise<void> {
    if (!(await this.downloadCatalog())) return;
    const program = findProgram(this.catalog, name);
    if (!program) {
      this.print(`Program "${name}" is not in the catalog.\r\n`);
      return;
    }
    if (program.size >= PROGRAM_CAPACITY) {
      this.print(`Program "${program.filename}" is too large.\r\n`);
      return;
    }
    const url = CATALOG_BASE_URL + program.filename;
    if (url.length >= URL_MAX + 1) {
      this.print('Program URL is too long.\r\n');
      return;
    }
    this.print(`WIFI BLOAD: downloading ${program.filename}.\r\n`);
    const text = await this.host.wifi.download(url, PROGRAM_CAPACITY - 1);
    if (text === null) {
      this.print(`Unable to download BASIC program "${program.filename}".\r\n`);
      return;
    }
    if (text.length !== program.size) {
      this.print(`Downloaded size ${text.length} does not match catalog size ${program.size}.\r\n`);
      return;
    }
    this.storeDownloaded(text);
    this.print(`Loaded program: ${program.filename}\r\n`);
  }

  /* -------------------------------------------------------- commands */

  /** basic_console_process(): returns a status, or null when the line is immediate BASIC. */
  async process(input: string): Promise<number | null> {
    let args: string | null;
    if (parseLineNumber(input)) {
      this.storeProgramLine(input);
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'LIST')) !== null) {
      this.listProgram(args);
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'TRACE')) !== null) {
      if (commandMatch(args, 'ON') !== null) {
        this.traceEnabled = true;
        this.print('Trace is ON.\r\n');
      } else if (commandMatch(args, 'OFF') !== null) {
        this.traceEnabled = false;
        this.print('Trace is OFF.\r\n');
      } else {
        this.print('Usage: TRACE ON | TRACE OFF\r\n');
      }
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'GFX')) !== null) {
      if (!this.graphicsCommand(args)) this.print('Usage: GFX INIT | CLEAR | RESET | UPDATE | INFO | HELP\r\n');
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'DIR')) !== null) {
      if (args !== '') {
        this.print('Usage: DIR\r\n');
        return MB_FUNC_OK;
      }
      const listing = await this.host.sd.directory();
      this.print(listing ?? 'Unable to read current directory.\r\n');
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'PWD')) !== null) {
      if (args !== '') {
        this.print('Usage: PWD\r\n');
      } else {
        const directory = await this.host.sd.getDirectory();
        this.print(directory !== null ? `${directory}\r\n` : 'Unable to read current directory.\r\n');
      }
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'SDCARD')) !== null) {
      await this.sdcardCommand(args);
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'CD')) !== null) {
      let directory = '';
      if (args !== '') {
        const parsed = this.parseFilename(args, PATH_MAX);
        if (parsed === null) {
          this.print('Usage: CD ["path"]\r\n');
          return MB_FUNC_OK;
        }
        directory = parsed;
      }
      this.print((await this.host.sd.changeDirectory(directory || '/')) ? 'Directory changed.\r\n' : 'Directory not found.\r\n');
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'DEL')) !== null) {
      const filename = this.parseFilename(args, PATH_MAX);
      if (filename === null) this.print('Usage: DEL "filename"\r\n');
      else if (await this.host.sd.remove(filename)) this.print(`Deleted file: ${filename}\r\n`);
      else this.print(`Unable to delete "${filename}".\r\n`);
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'MKDIR')) !== null) {
      const directory = this.parseFilename(args, PATH_MAX);
      if (directory === null) this.print('Usage: MKDIR "path"\r\n');
      else if (await this.host.sd.makeDirectory(directory)) this.print(`Created directory: ${directory}\r\n`);
      else this.print(`Unable to create directory "${directory}".\r\n`);
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'RUN')) !== null) {
      return this.runProgram();
    }
    if ((args = commandMatch(input, 'WIFI')) !== null) {
      await this.wifiCommand(args);
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'NEW')) !== null) {
      this.initialize();
      this.vm.clearVariables();
      this.print('New program.\r\n');
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'CLEAR')) !== null) {
      this.vm.clearVariables();
      this.print('Variables cleared.\r\n');
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'RENUM')) !== null) {
      this.renumber(args);
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'LOAD')) !== null) {
      const filename = this.parseFilename(args, PATH_MAX);
      if (filename === null) {
        this.print('Usage: LOAD "filename"\r\n');
        return MB_FUNC_OK;
      }
      this.initialize();
      if (sameName(filename, 'TREK')) {
        this.program = normalizeProgram(trekProgram);
        this.print('Loaded program: TREK\r\n');
        return MB_FUNC_OK;
      }
      const text = await this.host.sd.load(filename, PROGRAM_CAPACITY);
      if (text === null) {
        this.print(`Unable to load "${filename}".\r\n`);
        return MB_FUNC_OK;
      }
      this.program = normalizeProgram(text);
      this.print(`Loaded program: ${filename}\r\n`);
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'SAVE')) !== null) {
      const filename = this.parseFilename(args, PATH_MAX);
      if (filename === null) this.print('Usage: SAVE "filename"\r\n');
      else if (await this.host.sd.save(filename, this.program)) this.print(`Saved program: ${filename}\r\n`);
      else this.print(`Unable to save "${filename}".\r\n`);
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'HELP')) !== null) {
      this.print(HELP_TEXT);
      return MB_FUNC_OK;
    }
    if ((args = commandMatch(input, 'CLS')) !== null) {
      this.print('\x1b[2J\x1b[;H');
      return MB_FUNC_OK;
    }
    return null;
  }

  private async runProgram(): Promise<number> {
    if (this.program.length === 0) {
      this.print('No BASIC program is stored.\r\n');
      return MB_FUNC_OK;
    }
    this.runtimeNumbered = true;
    let text: string;
    try {
      setTrace(this.traceEnabled);
      const translation = translate(this.program, true);
      text = translation.text;
      this.runtimeMap = translation.map;
    } catch (error) {
      if (error instanceof TranslateError) {
        this.print(`BASIC parser: ${error.message}\r\n`);
        return MB_FUNC_ERR;
      }
      throw error;
    }
    this.vm.clearVariables();
    return this.runTranslated(text, false);
  }

  private graphicsCommand(args: string): boolean {
    const word = args.toUpperCase();
    switch (word) {
      case 'INIT':
        this.print('Graphics: ready\r\n');
        return true;
      case 'INFO':
        this.print(`Graphics: ready, ${GFX_WIDTH} x ${GFX_HEIGHT}, max handles ${GFX_MAX_OBJECTS}\r\n`);
        return true;
      case 'CLEAR':
      case 'RESET':
        this.graphics.clear();
        this.print('Graphics cleared.\r\n');
        return true;
      case 'LIST':
        this.print('Graphics handles:\r\n');
        for (const row of this.graphics.list()) this.print(`  ${row.handle}: ${row.type} parent=${row.parent}\r\n`);
        return true;
      case 'UPDATE':
        this.print('Graphics update is handled by the graphics task.\r\n');
        return true;
      case 'HELP':
        this.print('GFX INIT | CLEAR | RESET | UPDATE | INFO | LIST | HELP\r\n');
        return true;
      default:
        return false;
    }
  }

  private async sdcardCommand(args: string): Promise<void> {
    let rest: string | null;
    if ((rest = commandMatch(args, 'STATUS')) !== null && rest === '') {
      const status = await this.host.sd.status();
      if (!status) this.print('Unable to read SD card status.\r\n');
      else if (!status.inserted) this.print('No SD card inserted.\r\n');
      else this.print(`SD card: ${status.cardType}, ${status.capacity}, ${status.totalMib} MiB, filesystem: ${status.filesystem || 'not mounted'}\r\n`);
      return;
    }
    if ((rest = commandMatch(args, 'FREE')) !== null && rest === '') {
      const free = await this.host.sd.freeKib();
      this.print(free !== null ? `SD card free space: ${free} KiB (${Math.floor(free / 1024)} MiB).\r\n` : 'Unable to read SD card free space.\r\n');
      return;
    }
    if ((rest = commandMatch(args, 'FORMAT')) !== null) {
      if (rest === '') {
        const result = await this.host.sd.format(false);
        if (result === 'confirm') this.print('A valid filesystem exists. Type SDCARD FORMAT YES to erase it.\r\n');
        else if (result === 'ok') this.print('SD card formatted.\r\n');
        else this.print('Unable to format SD card.\r\n');
      } else if (sameName(rest, 'YES')) {
        this.print('Formatting, please wait .... ');
        const result = await this.host.sd.format(true);
        this.print(result === 'ok' ? 'complete\r\n' : '\r\nUnable to format SD card.\r\n');
      } else {
        this.print('Usage: SDCARD FORMAT [YES]\r\n');
      }
      return;
    }
    this.print('Usage: SDCARD STATUS | SDCARD FORMAT [YES] | SDCARD FREE\r\n');
  }

  private async wifiCommand(args: string): Promise<void> {
    let rest: string | null;
    const wifi = this.host.wifi;
    if ((rest = commandMatch(args, 'STATUS')) !== null && rest === '') {
      const ssid = await wifi.status();
      this.print(ssid !== null ? `WIFI connected to "${ssid}".\r\n` : 'WIFI is not connected.\r\n');
      return;
    }
    if ((rest = commandMatch(args, 'SCAN')) !== null && rest === '') {
      const records = await wifi.scan();
      if (!records) {
        this.print('WIFI scan unavailable while connected or not ready.\r\n');
        return;
      }
      this.print(`${'SSID'.padEnd(32)}  ${'RSSI dBm'.padStart(8)}  ${'CH'.padStart(3)}\r\n`);
      for (const record of records) {
        this.print(`${record.ssid.slice(0, 32).padEnd(32)}  ${String(record.rssi).padStart(8)}  ${String(record.channel).padStart(3)}\r\n`);
      }
      this.print(`${records.length} network(s) found.\r\n`);
      return;
    }
    if ((rest = commandMatch(args, 'STORED')) !== null && rest === '') {
      const stored = await wifi.stored();
      if (!stored) this.print('Unable to read stored WIFI credentials.\r\n');
      else if (stored.length === 0) this.print('No stored WIFI credentials.\r\n');
      else {
        this.print('Stored WIFI credentials:\r\n');
        for (const ssid of stored) this.print(`  ${ssid}\r\n`);
      }
      return;
    }
    if ((rest = commandMatch(args, 'CLEAR')) !== null && rest === '') {
      this.print((await wifi.clearStored()) ? 'Stored WIFI credentials cleared.\r\n' : 'Unable to clear stored WIFI credentials.\r\n');
      return;
    }
    if ((rest = commandMatch(args, 'DISCONNECT')) !== null && rest === '') {
      this.print((await wifi.disconnect()) ? 'WIFI disconnected.\r\n' : 'WIFI disconnect failed.\r\n');
      return;
    }
    if ((rest = commandMatch(args, 'CONNECT')) !== null) {
      const parsed = parseConnectArguments(rest);
      if (!parsed) {
        this.print('Usage: WIFI CONNECT "SSID" "PASSWORD"\r\n');
        return;
      }
      const result = await wifi.connect(parsed.ssid, parsed.password);
      if (!result.connected) this.print('WIFI connection failed.\r\n');
      else this.print(result.saved ? 'WIFI connected; credentials saved.\r\n' : 'WIFI connected; credentials not saved.\r\n');
      return;
    }
    if ((rest = commandMatch(args, 'LOAD')) !== null) {
      const url = this.parseFilename(rest, URL_MAX + 1);
      if (url === null) {
        this.print('Usage: WIFI LOAD "URL"\r\n');
        return;
      }
      this.print('WIFI LOAD: starting download.\r\n');
      const text = await wifi.download(url, PROGRAM_CAPACITY - 1);
      if (text === null) {
        this.print('Unable to download BASIC program from URL.\r\n');
        return;
      }
      this.storeDownloaded(text);
      this.print(`Loaded program: ${url}\r\n`);
      return;
    }
    if ((rest = commandMatch(args, 'LIST')) !== null && rest === '') {
      await this.listCatalog();
      return;
    }
    if ((rest = commandMatch(args, 'BLOAD')) !== null) {
      const name = this.parseFilename(rest, CATALOG_FILENAME_SIZE);
      if (name === null) {
        this.print('Usage: WIFI BLOAD "program"\r\n');
        return;
      }
      await this.loadCatalogProgram(name);
      return;
    }
    this.print('Usage: WIFI STATUS | WIFI SCAN | WIFI STORED | WIFI CLEAR | WIFI CONNECT "SSID" "PASSWORD" | WIFI DISCONNECT | WIFI LOAD "URL" | WIFI LIST | WIFI BLOAD "program"\r\n');
  }
}

function parseConnectArguments(text: string): { ssid: string; password: string } | null {
  const match = /^[ \t\n\v\f\r]*"([^"]*)"[ \t\n\v\f\r]*"([^"]*)"[ \t\n\v\f\r]*$/.exec(text);
  if (!match) return null;
  const [, ssid, password] = match;
  if (ssid.length === 0 || ssid.length >= 33 || password.length === 0 || password.length >= 65) return null;
  return { ssid, password };
}

/** append_translated_body() with plain numeric references (RENUM). */
export function renumberBody(body: string, mapped: (old: number) => number): string {
  let output = '';
  let inString = false;
  let expectReference = false;
  let i = 0;
  while (i < body.length && body[i] !== '\n') {
    const c = body[i];
    if (c === '"') {
      inString = !inString;
      output += c;
      i++;
      continue;
    }
    if (!inString && isAlpha(c)) {
      const start = i;
      while (isAlnum(body[i]) || body[i] === '$') i++;
      const token = body.slice(start, i);
      output += token;
      const upper = token.toUpperCase().slice(0, 15);
      if (token.length === 3 && upper === 'REM') {
        const end = body.indexOf('\n', i);
        return `${output}${body.slice(i, end < 0 ? body.length : end)}\n`;
      }
      expectReference = token.length < 16 && ['GOTO', 'GOSUB', 'THEN', 'ELSE', 'RESTORE', 'RUN'].includes(upper);
      continue;
    }
    if (!inString && expectReference && isDigit(c)) {
      const match = /^\d+/.exec(body.slice(i))![0];
      output += String(mapped(Number(match) > 0xffffffff ? 0xffffffff : Number(match)));
      i += match.length;
      continue;
    }
    if (!isSpace(c) && c !== ',') expectReference = false;
    output += c;
    i++;
  }
  return `${output}\n`;
}

const HELP_TEXT = [
  'CLS - Clear the terminal screen.',
  'DIR - List files in the current SD-card directory.',
  'PWD - Print the current SD-card directory.',
  'CD ["path"] - Change the current SD-card directory.',
  'MKDIR "path" - Create an SD-card directory.',
  'DEL "filename" - Delete an SD-card file.',
  'SDCARD STATUS - Show SD-card presence and information.',
  'SDCARD FORMAT [YES] - Format the SD card.',
  'SDCARD FREE - Show available SD-card space.',
  'LIST [line|first-last] - List stored BASIC lines.',
  'RUN - Run the stored BASIC program.',
  'WIFI SCAN - Scan for nearby WiFi networks.',
  'WIFI STATUS - Show WiFi connection status.',
  'WIFI STORED - List SSIDs with stored credentials.',
  'WIFI CLEAR - Clear all stored WiFi credentials.',
  'WIFI CONNECT "SSID" "PASSWORD" - Connect to WiFi.',
  'WIFI DISCONNECT - Disconnect from WiFi.',
  'WIFI LOAD "URL" - Download and load a BASIC program.',
  'WIFI LIST - List programs in the online catalog.',
  'WIFI BLOAD "program" - Download and load a catalog program.',
  'LOAD "filename" - Load a BASIC program from the SD card.',
  'SAVE "filename" - Save the BASIC program to the SD card.',
  'NEW - Clear the stored program and variables.',
  'CLEAR - Clear variables without deleting the program.',
  'RENUM [start[,step]] - Renumber the BASIC program.',
  'HELP - Show this command list.',
  'Enter a numbered line to store it; enter its number alone to delete it.',
].map((line) => `${line}\r\n`).join('');
