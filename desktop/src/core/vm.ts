/**
 * Interpreter for the MY-BASIC subset emitted by the translator (and typed in immediate mode).
 * Semantics follow fw/proj_cm55/third_party/my_basic/my_basic.c as configured by the firmware:
 * int_t = int32, real_t = float, warnings are reported and execution continues.
 */
import { traceText } from './translator';
import { INT_MAX, INT_MIN, Real, Value, formatG, formatValue, num, realOrInt, strtod, strtol, toInt } from './values';

export const MB_FUNC_OK = 0;
export const MB_FUNC_ERR = 3;
export const MB_FUNC_END = 6;
export const MB_EXTENDED_ABORT = 201;

export const ERROR_DESCRIPTIONS = [
  'No error', 'Function already exists', 'Function does not exist', 'Not supported',
  'Failed to open file', 'Symbol too long', 'Invalid character', 'Invalid module', 'Duplicate IMPORT',
  'Empty program', 'Program too long', 'Syntax error', 'Out of memory', 'Overflow', 'Unexpected type',
  'Invalid string', 'Integer expected', 'Number expected', 'String expected', 'Variable expected',
  'Index out of bound', 'Cannot find with the specific index', 'Too many dimensions', 'Rank out of bound',
  'Invalid identifier usage', 'Cannot assign to reserved word', 'Duplicate identifier', 'Incomplete structure',
  'Label does not exist', 'No return point', 'Colon expected', 'Comma expected', 'Comma or semicolon expected',
  'Open bracket expected', 'Close bracket expected', 'Too many nested', 'Failed to operation',
  'Operator expected', 'Assign operator expected', 'THEN statement expected', 'ELSE statement expected',
  'ENDIF statement expected', 'TO statement expected', 'NEXT statement expected', 'UNTIL statement expected',
  'Loop variable expected', 'Jump label expected', 'Calculation error', 'Invalid expression', 'Divide by zero',
];

export const SE_SYNTAX = 11;
export const SE_OUT_OF_MEMORY = 12;
export const SE_UNEXPECTED_TYPE = 14;
export const SE_INTEGER_EXPECTED = 16;
export const SE_NUMBER_EXPECTED = 17;
export const SE_STRING_EXPECTED = 18;
export const SE_INDEX_OUT_OF_BOUND = 20;
export const SE_TOO_MANY_DIMENSIONS = 22;
export const SE_RANK_OUT_OF_BOUND = 23;
export const SE_INVALID_ID_USAGE = 24;
export const SE_LABEL_NOT_EXISTS = 28;
export const SE_NO_RETURN_POINT = 29;
export const SE_OPEN_BRACKET_EXPECTED = 33;
export const SE_CLOSE_BRACKET_EXPECTED = 34;
export const SE_TOO_MANY_NESTED = 35;
export const SE_INVALID_EXPRESSION = 48;
export const SE_DIVIDE_BY_ZERO = 49;

/** A runtime failure. code === null means a silent MB_FUNC_ERR (only "Command failed" is shown). */
export class BasicError extends Error {
  constructor(readonly code: number | null, readonly status = MB_FUNC_ERR) {
    super(code === null ? 'native failure' : ERROR_DESCRIPTIONS[code] ?? 'unknown error');
  }
}

export const fail = (code: number | null = null): never => {
  throw new BasicError(code);
};

/** Thrown to stop execution after Ctrl-C. */
class AbortSignal extends Error {}

export type Native = (args: Value[], vm: Vm) => Value | undefined;

export interface VmHost {
  write(text: string): void;
  /** Read an INPUT line; null means Ctrl-C. */
  readLine(): Promise<string | null>;
  /** Sleep; resolves false when Ctrl-C interrupted it. */
  sleep(seconds: number): Promise<boolean>;
  /** Pending key for INKEY$ ("" when none); consumes it. */
  takeKey(): string;
  interrupted(): boolean;
  /** Called with the 1-based translated row of an error/warning. */
  reportError(code: number, description: string, row: number, column: number): void;
  /** Yield to the event loop so keys and rendering are processed. */
  yieldNow(): Promise<void>;
  now(): number;
  /** Immediate-mode statement tracing hook (TRACE ON with a non-numbered program). */
  traceStep?(rowText: string): void;
}

/* ------------------------------------------------------------------ lexer */

type TokenKind = 'num' | 'str' | 'id' | 'op' | 'eol';
interface Token {
  k: TokenKind;
  v: string;
  value?: Value;
  col: number;
}

const OPERATORS = ['<=', '>=', '<>', '=', '<', '>', '+', '-', '*', '/', '^', '(', ')', ',', ':', ';'];
const WORD_OPERATORS = new Set(['AND', 'OR', 'NOT', 'MOD']);

function lex(text: string): Token[] {
  const tokens: Token[] = [];
  let i = 0;
  while (i < text.length) {
    const c = text[i];
    if (c === ' ' || c === '\t' || c === '\r') {
      i++;
      continue;
    }
    if (c === "'") break;
    const col = i + 1;
    if (c === '"') {
      const close = text.indexOf('"', i + 1);
      const end = close < 0 ? text.length : close;
      tokens.push({ k: 'str', v: text.slice(i + 1, end), col });
      i = end + 1;
      continue;
    }
    if (c === '&' && (text[i + 1] === 'H' || text[i + 1] === 'h') && /[0-9a-fA-F]/.test(text[i + 2] ?? '')) {
      let j = i + 2;
      while (/[0-9a-fA-F]/.test(text[j] ?? '')) j++;
      const parsed = parseInt(text.slice(i + 2, j), 16);
      tokens.push({ k: 'num', v: text.slice(i, j), value: parsed <= INT_MAX ? parsed : new Real(parsed), col });
      i = j;
      continue;
    }
    if (/[0-9]/.test(c) || (c === '.' && /[0-9]/.test(text[i + 1] ?? ''))) {
      let j = i;
      if (c === '0' && (text[i + 1] === 'x' || text[i + 1] === 'X')) {
        j = i + 2;
        while (/[0-9a-fA-F]/.test(text[j] ?? '')) j++;
      } else {
        while (/[0-9.]/.test(text[j] ?? '')) j++;
        if ((text[j] === 'e' || text[j] === 'E') && /[0-9+-]/.test(text[j + 1] ?? '')) {
          j++;
          if (text[j] === '+' || text[j] === '-') j++;
          while (/[0-9]/.test(text[j] ?? '')) j++;
        }
      }
      const literal = text.slice(i, j);
      const asInt = strtol(literal);
      const asNumber = strtod(literal);
      let value: Value;
      if (asInt.end === literal.length && Math.abs(parseFloat(literal)) <= INT_MAX) value = asInt.value;
      else if (asNumber.end === literal.length) value = new Real(asNumber.value);
      else value = asInt.end > 0 ? asInt.value : new Real(asNumber.value);
      tokens.push({ k: 'num', v: literal, value, col });
      i = j;
      continue;
    }
    if (/[A-Za-z_]/.test(c)) {
      let j = i;
      while (/[A-Za-z0-9_]/.test(text[j] ?? '')) j++;
      if (text[j] === '$') j++;
      const word = text.slice(i, j).toUpperCase();
      tokens.push({ k: WORD_OPERATORS.has(word) ? 'op' : 'id', v: word, col });
      i = word === 'REM' ? text.length : j;
      continue;
    }
    const op = OPERATORS.find((candidate) => text.startsWith(candidate, i));
    if (!op) throw new BasicError(SE_SYNTAX);
    tokens.push({ k: 'op', v: op, col });
    i += op.length;
  }
  tokens.push({ k: 'eol', v: '', col: text.length + 1 });
  return tokens;
}

/* ------------------------------------------------------------- statements */

type Expr = () => Value;

interface StmtBase {
  row: number;
  col: number;
}
type Stmt = StmtBase &
  (
    | { t: 'nop' }
    | { t: 'assign'; name: string; expr: Expr }
    | { t: 'assignIndex'; name: string; indices: Expr[]; expr: Expr }
    | { t: 'expr'; expr: Expr }
    | { t: 'print'; items: Expr[]; newline: boolean }
    | { t: 'if'; cond: Expr; then: Stmt[]; otherwise: Stmt[] }
    | { t: 'goto'; label: string }
    | { t: 'gosub'; label: string }
    | { t: 'return'; expr: Expr | null }
    | { t: 'end' }
    | { t: 'dim'; arrays: { name: string; bounds: Expr[] }[] }
    | { t: 'def'; name: string; params: string[]; end: number }
    | { t: 'enddef' }
    | { t: 'while'; cond: Expr; wend: number }
    | { t: 'wend'; start: number }
    | { t: 'do' }
    | { t: 'until'; cond: Expr; start: number }
    | { t: 'input'; args: Expr[] }
    | { t: 'sleep'; args: Expr[] }
  );

interface BasicArray {
  dims: number[];
  data: Value[];
  isString: boolean;
}

interface Routine {
  name: string;
  params: string[];
  start: number;
  end: number;
}

type Flow = 'next' | 'jump' | 'end' | 'return';

/* ------------------------------------------------------------------ RNG */

/** newlib rand(): 64-bit LCG, returns bits 32..62. */
class NewlibRandom {
  private next = 1n;
  seed(value: number): void {
    this.next = BigInt(value >>> 0);
  }
  rand(): number {
    this.next = (this.next * 6364136223846793005n + 1n) & 0xffffffffffffffffn;
    return Number((this.next >> 32n) & 0x7fffffffn);
  }
}

/* -------------------------------------------------------------------- VM */

export class Vm {
  private globals = new Map<string, Value>();
  private arrays = new Map<string, BasicArray>();
  private locals: Map<string, Value>[] = [];
  private stmts: Stmt[] = [];
  private labels = new Map<string, number>();
  private routines = new Map<string, Routine>();
  private rows: string[] = [];
  private pc = 0;
  private gosubStack: number[] = [];
  private routineDepth = 0;
  private returnValue: Value = null;
  private current: Stmt | null = null;
  private column = 0;
  private lastRandom = 0;
  private readonly random = new NewlibRandom();
  private readonly natives = new Map<string, Native>();
  private readonly startTime: number;
  immediateTrace = false;

  constructor(private readonly host: VmHost) {
    this.startTime = host.now();
    this.installNatives();
  }

  register(name: string, fn: Native): void {
    this.natives.set(name.toUpperCase(), fn);
  }

  /** mb_reset(..., clear variables). */
  clearVariables(): void {
    this.globals.clear();
    this.arrays.clear();
  }

  getVariable(name: string): Value {
    const key = name.toUpperCase();
    if (this.globals.has(key)) return this.globals.get(key)!;
    return key.endsWith('$') ? '' : 0;
  }

  hasVariable(name: string): boolean {
    return this.globals.has(name.toUpperCase());
  }

  /** Print through the compat printer (tracks the column for TAB). */
  print(text: string): void {
    if (text.length === 0) return;
    for (const c of text) {
      if (c === '\r' || c === '\n') this.column = 0;
      else this.column++;
    }
    this.host.write(text);
  }

  resetColumn(): void {
    this.column = 0;
  }

  /** Parse translated text. Throws BasicError on syntax problems (reported at row/col). */
  load(text: string): void {
    this.stmts = [];
    this.labels.clear();
    this.routines.clear();
    this.gosubStack = [];
    this.locals = [];
    this.routineDepth = 0;
    this.rows = text.split('\n');
    if (this.rows.length > 0 && this.rows[this.rows.length - 1] === '') this.rows.pop();
    const blocks: number[] = [];
    let openDef: number | null = null;
    this.rows.forEach((rowText, row) => {
      let tokens: Token[];
      try {
        tokens = lex(rowText);
      } catch (error) {
        throw this.located(error, row, 1);
      }
      const parser = new Parser(tokens, row, this);
      try {
        if (tokens[0].k === 'id' && tokens[1]?.v === ':' && tokens[1].k === 'op' && !KEYWORDS.has(tokens[0].v)) {
          this.labels.set(tokens[0].v, this.stmts.length);
          parser.pos = 2;
        }
        for (const stmt of parser.parseRow()) {
          const index = this.stmts.length;
          this.stmts.push(stmt);
          if (stmt.t === 'def') {
            if (openDef !== null) throw new BasicError(SE_SYNTAX);
            openDef = index;
            this.routines.set(stmt.name, { name: stmt.name, params: stmt.params, start: index + 1, end: -1 });
          } else if (stmt.t === 'enddef') {
            if (openDef === null) throw new BasicError(SE_SYNTAX);
            const def = this.stmts[openDef] as Extract<Stmt, { t: 'def' }>;
            def.end = index;
            this.routines.get(def.name)!.end = index;
            openDef = null;
          } else if (stmt.t === 'while' || stmt.t === 'do') {
            blocks.push(index);
          } else if (stmt.t === 'wend' || stmt.t === 'until') {
            const start = blocks.pop();
            const opener = start === undefined ? undefined : this.stmts[start];
            if (opener === undefined || (stmt.t === 'wend') !== (opener.t === 'while')) throw new BasicError(SE_SYNTAX);
            stmt.start = start!;
            if (opener.t === 'while') opener.wend = index;
          }
        }
      } catch (error) {
        throw this.located(error, row, tokens[parser.pos]?.col ?? 1);
      }
    });
    if (openDef !== null || blocks.length > 0) {
      throw this.located(new BasicError(SE_SYNTAX), this.rows.length - 1, 1);
    }
    this.pc = 0;
  }

  private located(error: unknown, row: number, col: number): unknown {
    if (error instanceof BasicError && error.code !== null) {
      this.host.reportError(error.code, ERROR_DESCRIPTIONS[error.code], row + 1, col);
    }
    return error instanceof BasicError ? new BasicError(null, error.status) : error;
  }

  /** Run the loaded program. Returns a MY-BASIC status code. */
  async run(): Promise<number> {
    this.pc = 0;
    let sliceStart = this.host.now();
    let counter = 0;
    try {
      while (this.pc < this.stmts.length) {
        if (++counter >= 256) {
          counter = 0;
          if (this.host.now() - sliceStart > 15) {
            await this.host.yieldNow();
            sliceStart = this.host.now();
          }
        }
        if (this.host.interrupted()) return MB_EXTENDED_ABORT;
        const stmt = this.stmts[this.pc];
        if (stmt.t === 'input' || stmt.t === 'sleep') {
          this.current = stmt;
          const ok = stmt.t === 'input' ? await this.input(stmt.args.map((arg) => arg())) : await this.sleep(stmt.args.map((arg) => arg()));
          if (!ok) return MB_EXTENDED_ABORT;
          sliceStart = this.host.now();
          this.pc++;
          continue;
        }
        const flow = this.exec(stmt);
        if (flow === 'end') return MB_FUNC_END;
        if (flow === 'next') this.pc++;
      }
      return MB_FUNC_OK;
    } catch (error) {
      return this.handleError(error);
    }
  }

  private handleError(error: unknown): number {
    if (error instanceof AbortSignal) return MB_EXTENDED_ABORT;
    if (error instanceof BasicError) {
      if (error.code !== null && !this.host.interrupted()) {
        const stmt = this.current;
        this.host.reportError(error.code, ERROR_DESCRIPTIONS[error.code], (stmt?.row ?? 0) + 1, stmt?.col ?? 1);
      }
      return error.status;
    }
    throw error;
  }

  /** Report a MY-BASIC warning (the error handler runs, execution continues). */
  warn(code: number): void {
    if (this.host.interrupted()) return;
    const stmt = this.current;
    this.host.reportError(code, ERROR_DESCRIPTIONS[code], (stmt?.row ?? 0) + 1, stmt?.col ?? 1);
  }

  private exec(stmt: Stmt): Flow {
    this.current = stmt;
    if (this.immediateTrace && this.host.traceStep && stmt.t !== 'if') {
      this.host.traceStep(this.rows[stmt.row] ?? '');
    }
    switch (stmt.t) {
      case 'nop':
      case 'do':
      case 'enddef':
        return 'next';
      case 'assign':
        this.assign(stmt.name, stmt.expr());
        return 'next';
      case 'assignIndex': {
        const array = this.array(stmt.name);
        const offset = this.offset(array, stmt.indices.map((index) => index()));
        const value = stmt.expr();
        array.data[offset] = value;
        return 'next';
      }
      case 'expr':
        stmt.expr();
        return 'next';
      case 'print':
        this.printItems(stmt.items, stmt.newline);
        return 'next';
      case 'if': {
        if (this.immediateTrace && this.host.traceStep) this.host.traceStep(this.rows[stmt.row] ?? '');
        const branch = truthy(stmt.cond()) ? stmt.then : stmt.otherwise;
        for (const inner of branch) {
          const flow = this.exec(inner);
          if (flow !== 'next') return flow;
        }
        return 'next';
      }
      case 'goto':
        this.pc = this.label(stmt.label);
        return 'jump';
      case 'gosub':
        if (this.gosubStack.length >= 4096) fail(SE_TOO_MANY_NESTED);
        this.gosubStack.push(this.pc + 1);
        this.pc = this.label(stmt.label);
        return 'jump';
      case 'return':
        if (this.routineDepth > 0 && this.gosubStack.length <= this.routineFloor()) {
          this.returnValue = stmt.expr ? stmt.expr() : null;
          return 'return';
        }
        if (this.gosubStack.length === 0) fail(SE_NO_RETURN_POINT);
        this.pc = this.gosubStack.pop()!;
        return 'jump';
      case 'end':
        return 'end';
      case 'dim':
        for (const declaration of stmt.arrays) this.dim(declaration.name, declaration.bounds.map((bound) => bound()));
        return 'next';
      case 'def':
        this.pc = stmt.end + 1;
        return 'jump';
      case 'while':
        if (!truthy(stmt.cond())) {
          this.pc = stmt.wend + 1;
          return 'jump';
        }
        return 'next';
      case 'wend':
        this.pc = stmt.start;
        return 'jump';
      case 'until':
        if (!truthy(stmt.cond())) {
          this.pc = stmt.start + 1;
          return 'jump';
        }
        return 'next';
      case 'input':
      case 'sleep':
        // Only reachable inside a routine or an inline IF; the firmware cannot suspend there either.
        return fail(SE_INVALID_EXPRESSION);
    }
  }

  private floors: number[] = [];
  private routineFloor(): number {
    return this.floors[this.floors.length - 1] ?? 0;
  }

  /** Call a DEF routine synchronously. */
  callRoutine(routine: Routine, args: Value[]): Value {
    if (routine.end < 0) fail(SE_SYNTAX);
    if (args.length !== routine.params.length) fail(args.length > routine.params.length ? SE_CLOSE_BRACKET_EXPECTED : SE_SYNTAX);
    if (this.routineDepth >= 256) fail(SE_TOO_MANY_NESTED);
    const scope = new Map<string, Value>();
    routine.params.forEach((param, index) => scope.set(param, args[index]));
    const savedPc = this.pc;
    const savedCurrent = this.current;
    this.locals.push(scope);
    this.floors.push(this.gosubStack.length);
    this.routineDepth++;
    this.returnValue = null;
    try {
      this.pc = routine.start;
      let counter = 0;
      while (this.pc < routine.end) {
        if (++counter >= 4096) {
          counter = 0;
          if (this.host.interrupted()) throw new AbortSignal();
        }
        const flow = this.exec(this.stmts[this.pc]);
        if (flow === 'return') break;
        if (flow === 'end') throw new AbortSignal();
        if (flow === 'next') this.pc++;
      }
      return this.returnValue;
    } finally {
      this.routineDepth--;
      this.floors.pop();
      this.locals.pop();
      this.pc = savedPc;
      this.current = savedCurrent;
    }
  }

  private label(name: string): number {
    const index = this.labels.get(name);
    if (index === undefined) return fail(SE_LABEL_NOT_EXISTS);
    return index;
  }

  lookup(name: string): Value {
    const scope = this.locals[this.locals.length - 1];
    if (scope?.has(name)) return scope.get(name)!;
    const value = this.globals.get(name);
    if (value !== undefined) return value;
    if (name === 'NIL') return null;
    if (name === 'TRUE') return 1;
    if (name === 'FALSE') return 0;
    if (name === 'PI') return new Real(Math.PI);
    return name.endsWith('$') ? '' : 0;
  }

  assign(name: string, value: Value): void {
    const scope = this.locals[this.locals.length - 1];
    if (scope?.has(name)) scope.set(name, value);
    else this.globals.set(name, value);
  }

  private array(name: string): BasicArray {
    const array = this.arrays.get(name);
    if (!array) return fail(SE_INVALID_ID_USAGE);
    return array;
  }

  private offset(array: BasicArray, indices: Value[]): number {
    if (indices.length !== array.dims.length) fail(SE_RANK_OUT_OF_BOUND);
    let offset = 0;
    for (let i = 0; i < indices.length; i++) {
      const index = popInt(indices[i]);
      if (index < 0 || index >= array.dims[i]) fail(SE_INDEX_OUT_OF_BOUND);
      offset = offset * array.dims[i] + index;
    }
    return offset;
  }

  readIndex(name: string, indices: Value[]): Value {
    const array = this.array(name);
    return array.data[this.offset(array, indices)];
  }

  isArray(name: string): boolean {
    return this.arrays.has(name);
  }

  private dim(name: string, bounds: Value[]): void {
    const dims: number[] = [];
    let count = 0;
    for (const bound of bounds) {
      let size: number;
      if (bound instanceof Real && bound.v >= 0 && bound.v < INT_MAX) size = Math.trunc(bound.v);
      else if (typeof bound === 'number') size = bound;
      else return fail(SE_UNEXPECTED_TYPE);
      if (size <= 0) fail(SE_INDEX_OUT_OF_BOUND);
      if (dims.length >= 4) fail(SE_TOO_MANY_DIMENSIONS);
      if (count && size > Math.floor(Math.floor(INT_MAX / 16) / count)) fail(SE_OUT_OF_MEMORY);
      dims.push(size);
      count = count ? count * size : size;
    }
    // The CM55 heap cannot hold arrays this large either.
    if (count > 4 * 1024 * 1024) fail(SE_OUT_OF_MEMORY);
    const isString = name.endsWith('$');
    this.arrays.set(name, { dims, data: new Array<Value>(count).fill(isString ? '' : 0), isString });
  }

  private printItems(items: Expr[], newline: boolean): void {
    try {
      for (const item of items) this.print(formatValue(item()));
      if (newline) this.print('\n');
    } catch (error) {
      if (error instanceof BasicError) this.print('\n');
      throw error;
    }
  }

  findRoutine(name: string): Routine | undefined {
    return this.routines.get(name);
  }

  native(name: string): Native | undefined {
    return this.natives.get(name);
  }

  /* -------------------------------------------------- async classic runtime */

  private async input(args: Value[]): Promise<boolean> {
    const prompt = args[0];
    if (typeof prompt !== 'string') return fail(SE_STRING_EXPECTED);
    if (prompt.length >= 256) fail(SE_INVALID_EXPRESSION);
    const names: string[] = [];
    for (const name of args.slice(1)) {
      if (typeof name !== 'string') return fail(SE_STRING_EXPECTED);
      if (names.length >= 8 || name.length >= 64) fail(SE_INVALID_EXPRESSION);
      names.push(name.toUpperCase());
    }
    if (names.length === 0) fail(SE_INVALID_EXPRESSION);
    this.print(`${prompt}? `);
    const line = await this.host.readLine();
    if (line === null) return false;
    this.column = 0;
    let cursor = line.slice(0, 255);
    const values: Value[] = [];
    for (let index = 0; index < names.length; index++) {
      const comma = cursor.indexOf(',');
      const field = comma >= 0 ? cursor.slice(0, comma) : cursor;
      if (names[index].endsWith('$')) {
        values.push(field);
      } else {
        const parsed = strtod(field);
        let rest = parsed.end;
        while (field[rest] === ' ' || field[rest] === '\t' || field[rest] === '\r') rest++;
        if (parsed.end === 0 || rest !== field.length || !Number.isFinite(parsed.value) || Math.abs(parsed.value) > 3.4028234663852886e38) {
          fail(SE_INVALID_EXPRESSION);
        }
        values.push(new Real(parsed.value));
      }
      if (index + 1 < names.length && comma < 0) fail(SE_INVALID_EXPRESSION);
      if (index + 1 === names.length && comma >= 0) fail(SE_INVALID_EXPRESSION);
      if (comma >= 0) cursor = cursor.slice(comma + 1);
    }
    names.forEach((name, index) => this.assign(name, values[index]));
    return true;
  }

  private async sleep(args: Value[]): Promise<boolean> {
    if (args.length !== 1) fail(SE_SYNTAX);
    const seconds = popReal(args[0]);
    if (!Number.isFinite(seconds) || seconds < 0) fail(SE_INVALID_EXPRESSION);
    return this.host.sleep(seconds);
  }

  /* ------------------------------------------------------------ natives */

  private installNatives(): void {
    const math = (fn: (x: number) => number): Native => (args) => {
      const [value] = arity(args, 1);
      if (typeof value === 'string') {
        this.warn(SE_NUMBER_EXPECTED);
        return value;
      }
      return realOrInt(fn(Math.fround(num(value))));
    };
    const toInteger = (fn: (x: number) => number): Native => (args) => {
      const [value] = arity(args, 1);
      if (typeof value === 'string') {
        this.warn(SE_NUMBER_EXPECTED);
        return value;
      }
      if (typeof value === 'number' || value === null) return num(value);
      return toInt(fn(value.v));
    };
    this.register('ABS', (args) => {
      const [value] = arity(args, 1);
      if (typeof value === 'number') return value === INT_MIN ? new Real(2147483648) : Math.abs(value);
      if (value instanceof Real) return new Real(Math.abs(value.v));
      this.warn(SE_NUMBER_EXPECTED);
      return value;
    });
    this.register('SGN', (args) => {
      const value = popReal(arity(args, 1)[0]);
      return value > 0 ? 1 : value < 0 ? -1 : 0;
    });
    this.register('SQR', math(Math.sqrt));
    this.register('FLOOR', toInteger(Math.floor));
    this.register('CEIL', toInteger(Math.ceil));
    this.register('FIX', toInteger(Math.trunc));
    this.register('ROUND', toInteger((x) => (x >= 0 ? x + 0.5 : x - 0.5)));
    this.register('SIN', math(Math.sin));
    this.register('COS', math(Math.cos));
    this.register('TAN', math(Math.tan));
    this.register('ASIN', math(Math.asin));
    this.register('ACOS', math(Math.acos));
    this.register('ATAN', math(Math.atan));
    this.register('EXP', math(Math.exp));
    this.register('LOG', math(Math.log));
    this.register('LEN', (args) => popString(arity(args, 1)[0]).length);
    this.register('VAL', (args) => {
      const [value] = arity(args, 1);
      if (typeof value !== 'string') return value;
      const asInt = strtol(value);
      if (asInt.end === value.length) return asInt.value;
      const asReal = strtod(value);
      if (asReal.end === value.length && asReal.end > 0) return new Real(asReal.value);
      return fail();
    });
    this.register('ASC', (args) => {
      const text = popString(arity(args, 1)[0]);
      return text.length ? text.charCodeAt(0) & 0xff : 0;
    });
    this.register('CHR', (args) => String.fromCharCode(popInt(arity(args, 1)[0]) & 0xff));
    this.register('STR', (args) => {
      const [value] = arity(args, 1);
      if (typeof value === 'number') return String(value);
      if (value instanceof Real) return formatG(value.v);
      if (typeof value === 'string') return value;
      return fail(SE_NUMBER_EXPECTED);
    });
    this.register('LEFT', (args) => {
      const [text, count] = arity(args, 2);
      const n = popInt(count);
      if (n < 0) fail(SE_INDEX_OUT_OF_BOUND);
      return popString(text).slice(0, n);
    });
    this.register('RIGHT', (args) => {
      const [text, count] = arity(args, 2);
      const s = popString(text);
      const n = popInt(count);
      if (n < 0) fail(SE_INDEX_OUT_OF_BOUND);
      return n === 0 ? '' : s.slice(Math.max(0, s.length - n));
    });
    this.register('MID', (args) => {
      const [text, start, count] = arity(args, 3);
      const s = popString(text);
      const first = popInt(start);
      const n = popInt(count);
      if (first < 0 || first >= s.length || n < 0) fail(SE_INDEX_OUT_OF_BOUND);
      return s.substr(first, n);
    });

    /* Classic runtime functions from basic_compat.c. */
    this.register('BCNOOP', (args) => {
      arity(args, 0);
      return undefined;
    });
    this.register('BCTRACE', (args) => {
      const [line, offset, length] = arity(args, 3).map(popInt);
      if (this.column !== 0) this.print('\r\n');
      this.print(`TRACE ${line}: ${traceText(offset, length)}\r\n`);
      this.column = 0;
      return undefined;
    });
    this.register('BCRND', (args) => {
      if (args.length > 1) fail(SE_CLOSE_BRACKET_EXPECTED);
      const x = args.length ? popReal(args[0]) : 1;
      if (!Number.isFinite(x) || x < -4294967295) return fail(SE_INVALID_EXPRESSION);
      if (x < 0) this.random.seed(Math.trunc(-x));
      if (x !== 0) this.lastRandom = Math.fround(this.random.rand() / 2147483648);
      return new Real(this.lastRandom);
    });
    this.register('BCRANDOMIZE', (args) => {
      const seed = popReal(arity(args, 1)[0]);
      if (!(seed >= 0 && seed <= 4294967295)) fail(SE_INVALID_EXPRESSION);
      this.random.seed(Math.trunc(seed));
      return undefined;
    });
    this.register('BCTIMER', (args) => {
      arity(args, 0);
      return new Real((this.host.now() - this.startTime) / 1000);
    });
    this.register('BCINKEY', (args) => {
      arity(args, 0);
      return this.host.takeKey();
    });
    this.register('BCMID', (args) => {
      if (args.length < 2 || args.length > 3) fail(args.length > 3 ? SE_CLOSE_BRACKET_EXPECTED : SE_SYNTAX);
      const s = popString(args[0]);
      const start = popInt(args[1]);
      const count = args.length === 3 ? popInt(args[2]) : s.length;
      if (start < 1 || count < 0) fail(SE_INVALID_EXPRESSION);
      if (start > s.length) return '';
      return s.substr(start - 1, count);
    });
    this.register('BCLEFT', (args) => {
      const [text, count] = arity(args, 2);
      const s = popString(text);
      const n = popInt(count);
      if (n < 0) fail(SE_INVALID_EXPRESSION);
      return s.slice(0, Math.min(n, s.length));
    });
    this.register('BCRIGHT', (args) => {
      const [text, count] = arity(args, 2);
      const s = popString(text);
      const n = popInt(count);
      if (n < 0) fail(SE_INVALID_EXPRESSION);
      return n >= s.length ? s : s.slice(s.length - n);
    });
    this.register('BCTAB', (args) => {
      const n = popInt(arity(args, 1)[0]);
      if (n < 1 || n > 255) fail(SE_INVALID_EXPRESSION);
      return ' '.repeat(Math.max(0, n - this.column - 1));
    });
  }
}

const KEYWORDS = new Set([
  'IF', 'THEN', 'ELSE', 'GOTO', 'GOSUB', 'RETURN', 'END', 'DIM', 'PRINT', 'DEF', 'ENDDEF', 'LET',
  'WHILE', 'WEND', 'DO', 'UNTIL', 'REM', 'RESTORE',
]);

/* --------------------------------------------------------- arg helpers */

export function arity(args: Value[], count: number): Value[] {
  if (args.length > count) fail(SE_CLOSE_BRACKET_EXPECTED);
  if (args.length < count) fail(SE_SYNTAX);
  return args;
}

export function popInt(value: Value): number {
  if (typeof value === 'number') return value;
  if (value instanceof Real) return toInt(value.v);
  if (value === null) return 0;
  return fail(SE_INTEGER_EXPECTED);
}

export function popReal(value: Value): number {
  if (typeof value === 'string') return fail(SE_NUMBER_EXPECTED);
  return num(value);
}

export function popString(value: Value): string {
  if (typeof value !== 'string') return fail(SE_STRING_EXPECTED);
  return value;
}

function truthy(value: Value): boolean {
  if (value === null) return false;
  if (typeof value === 'string') return true;
  return num(value) !== 0;
}

/* ------------------------------------------------------- operators */

function isNumeric(value: Value): value is number | Real | null {
  return typeof value !== 'string';
}

function arith(vm: Vm, op: string, a: Value, b: Value): Value {
  if (typeof a === 'string' || typeof b === 'string') {
    if (op === '+' && typeof a === 'string' && typeof b === 'string') return a + b;
    return fail(SE_STRING_EXPECTED);
  }
  const bothInt = !(a instanceof Real) && !(b instanceof Real);
  const x = num(a);
  const y = num(b);
  const fx = Math.fround(x);
  const fy = Math.fround(y);
  if (op === '/' || op === 'MOD') {
    const divisorZero = op === 'MOD' ? toInt(y) === 0 : y === 0;
    if (divisorZero) {
      const dividendZero = op === 'MOD' ? x === 0 : x === 0;
      vm.warn(SE_DIVIDE_BY_ZERO);
      return new Real(dividendZero ? NaN : Infinity);
    }
    if (op === 'MOD') {
      const ix = typeof a === 'number' ? a : toInt(x);
      const iy = typeof b === 'number' ? b : toInt(y);
      return (ix % iy) | 0;
    }
  }
  if (op === '^') return realOrInt(Math.pow(fx, fy));
  let real: number;
  switch (op) {
    case '+': real = Math.fround(fx + fy); break;
    case '-': real = Math.fround(fx - fy); break;
    case '*': real = Math.fround(fx * fy); break;
    default: real = Math.fround(fx / fy); break;
  }
  if (bothInt) {
    let integer: number;
    switch (op) {
      case '+': integer = (x + y) | 0; break;
      case '-': integer = (x - y) | 0; break;
      case '*': integer = Math.imul(x, y); break;
      default: integer = x === INT_MIN && y === -1 ? INT_MIN : (x / y) | 0; break;
    }
    if (Math.fround(integer) === real) return integer;
  }
  return realOrInt(real);
}

function compare(vm: Vm, op: string, a: Value, b: Value): Value {
  if (typeof a === 'string' || typeof b === 'string') {
    if (typeof a === 'string' && typeof b === 'string') {
      const c = a < b ? -1 : a > b ? 1 : 0;
      return relation(op, c, 0) ? 1 : 0;
    }
    if ((op === '=' || op === '<>') && (a === null || b === null)) return op === '=' ? 0 : 1;
    vm.warn(SE_STRING_EXPECTED);
    return op === '<>' || op === '>' || op === '>=' ? 1 : 0;
  }
  if (typeof a === 'number' && typeof b === 'number') return relation(op, a, b) ? 1 : 0;
  return relation(op, Math.fround(num(a)), Math.fround(num(b))) ? 1 : 0;
}

function relation(op: string, x: number, y: number): boolean {
  switch (op) {
    case '=': return x === y;
    case '<>': return x !== y;
    case '<': return x < y;
    case '>': return x > y;
    case '<=': return x <= y;
    default: return x >= y;
  }
}

function logical(value: Value): boolean {
  if (value === null) return false;
  if (typeof value === 'string') return true;
  return num(value) !== 0;
}

/* ---------------------------------------------------------- parser */

class Parser {
  pos = 0;
  constructor(private readonly tokens: Token[], private readonly row: number, private readonly vm: Vm) {}

  private peek(): Token {
    return this.tokens[this.pos];
  }
  private next(): Token {
    return this.tokens[this.pos++];
  }
  private isOp(value: string): boolean {
    const token = this.peek();
    return token.k === 'op' && token.v === value;
  }
  private isWord(value: string): boolean {
    const token = this.peek();
    return token.k === 'id' && token.v === value;
  }
  private expectOp(value: string, code: number): void {
    if (!this.isOp(value)) fail(code);
    this.pos++;
  }

  parseRow(): Stmt[] {
    const list: Stmt[] = [];
    while (this.peek().k !== 'eol') {
      if (this.isOp(':')) {
        this.pos++;
        continue;
      }
      if (this.isWord('REM')) break;
      list.push(this.statement());
      if (!this.isOp(':') && this.peek().k !== 'eol') fail(SE_SYNTAX);
    }
    return list;
  }

  /** Statements up to ELSE / end of row (for inline IF bodies). */
  private inlineList(): Stmt[] {
    const list: Stmt[] = [];
    while (this.peek().k !== 'eol' && !this.isWord('ELSE')) {
      if (this.isOp(':')) {
        this.pos++;
        continue;
      }
      if (this.isWord('REM')) {
        this.pos = this.tokens.length - 1;
        break;
      }
      list.push(this.statement());
      if (!this.isOp(':') && !this.isWord('ELSE') && this.peek().k !== 'eol') fail(SE_SYNTAX);
    }
    return list;
  }

  private statement(): Stmt {
    const token = this.peek();
    const base = { row: this.row, col: token.col };
    if (token.k !== 'id') {
      return { ...base, t: 'expr', expr: this.expression() };
    }
    switch (token.v) {
      case 'IF': {
        this.pos++;
        const cond = this.expression();
        if (!this.isWord('THEN')) fail(39);
        this.pos++;
        const thenList = this.inlineList();
        let otherwise: Stmt[] = [];
        if (this.isWord('ELSE')) {
          this.pos++;
          otherwise = this.inlineList();
        }
        return { ...base, t: 'if', cond, then: thenList, otherwise };
      }
      case 'GOTO':
      case 'GOSUB': {
        this.pos++;
        const target = this.next();
        if (target.k !== 'id') fail(46);
        return { ...base, t: token.v === 'GOTO' ? 'goto' : 'gosub', label: target.v };
      }
      case 'RESTORE':
        this.pos++;
        if (this.peek().k === 'id' || this.peek().k === 'num') this.pos++;
        return { ...base, t: 'nop' };
      case 'RETURN': {
        this.pos++;
        const atEnd = this.peek().k === 'eol' || this.isOp(':') || this.isWord('ELSE');
        return { ...base, t: 'return', expr: atEnd ? null : this.expression() };
      }
      case 'END':
        this.pos++;
        return { ...base, t: 'end' };
      case 'ENDDEF':
        this.pos++;
        return { ...base, t: 'enddef' };
      case 'DO':
        this.pos++;
        return { ...base, t: 'do' };
      case 'WEND':
        this.pos++;
        return { ...base, t: 'wend', start: -1 };
      case 'WHILE':
        this.pos++;
        return { ...base, t: 'while', cond: this.expression(), wend: -1 };
      case 'UNTIL':
        this.pos++;
        return { ...base, t: 'until', cond: this.expression(), start: -1 };
      case 'PRINT': {
        this.pos++;
        const items: Expr[] = [];
        let newline = true;
        while (this.peek().k !== 'eol' && !this.isOp(':') && !this.isWord('ELSE')) {
          if (this.isOp(',') || this.isOp(';')) {
            this.pos++;
            newline = false;
            continue;
          }
          items.push(this.expression());
          newline = true;
          if (!this.isOp(',') && !this.isOp(';')) break;
        }
        return { ...base, t: 'print', items, newline };
      }
      case 'DIM': {
        this.pos++;
        const arrays: { name: string; bounds: Expr[] }[] = [];
        do {
          const name = this.next();
          if (name.k !== 'id') fail(SE_INVALID_ID_USAGE);
          this.expectOp('(', SE_OPEN_BRACKET_EXPECTED);
          const bounds = this.argumentList();
          if (bounds.length === 0) fail(SE_INVALID_ID_USAGE);
          arrays.push({ name: name.v, bounds });
        } while (this.isOp(',') && ++this.pos);
        return { ...base, t: 'dim', arrays };
      }
      case 'DEF': {
        this.pos++;
        const name = this.next();
        if (name.k !== 'id') fail(SE_INVALID_ID_USAGE);
        const params: string[] = [];
        if (this.isOp('(')) {
          this.pos++;
          while (!this.isOp(')')) {
            const param = this.next();
            if (param.k !== 'id') fail(SE_INVALID_ID_USAGE);
            params.push(param.v);
            if (this.isOp(',')) this.pos++;
            else if (!this.isOp(')')) fail(SE_CLOSE_BRACKET_EXPECTED);
          }
          this.pos++;
        }
        return { ...base, t: 'def', name: name.v, params, end: -1 };
      }
      case 'LET':
        this.pos++;
        return this.assignment(base, true);
      default:
        return this.assignment(base, false);
    }
  }

  private assignment(base: StmtBase, required: boolean): Stmt {
    const start = this.pos;
    const name = this.next();
    if (name.k !== 'id') return fail(SE_SYNTAX);
    if (this.isOp('=')) {
      this.pos++;
      return { ...base, t: 'assign', name: name.v, expr: this.expression() };
    }
    if (this.isOp('(')) {
      this.pos++;
      const indices = this.argumentList();
      if (this.isOp('=')) {
        this.pos++;
        return { ...base, t: 'assignIndex', name: name.v, indices, expr: this.expression() };
      }
    }
    if (required) fail(38);
    this.pos = start;
    if (name.v === 'BCINPUT' || name.v === 'BCSLEEP') {
      this.pos++;
      this.expectOp('(', SE_OPEN_BRACKET_EXPECTED);
      const args = this.argumentList();
      return { ...base, t: name.v === 'BCINPUT' ? 'input' : 'sleep', args };
    }
    return { ...base, t: 'expr', expr: this.expression() };
  }

  /** Parses `a, b, c)` after an open bracket, consuming the close bracket. */
  private argumentList(): Expr[] {
    const args: Expr[] = [];
    if (this.isOp(')')) {
      this.pos++;
      return args;
    }
    for (;;) {
      args.push(this.expression());
      if (this.isOp(',')) {
        this.pos++;
        continue;
      }
      this.expectOp(')', SE_CLOSE_BRACKET_EXPECTED);
      return args;
    }
  }

  expression(): Expr {
    return this.logicalExpr();
  }

  private logicalExpr(): Expr {
    let left = this.comparison();
    while (this.isOp('AND') || this.isOp('OR')) {
      const op = this.next().v;
      const l = left;
      const r = this.comparison();
      left = op === 'AND' ? () => (logical(l()) && logical(r()) ? 1 : 0) : () => {
        const a = logical(l());
        const b = logical(r());
        return a || b ? 1 : 0;
      };
      if (op === 'AND') {
        const lhs = l;
        left = () => {
          const a = logical(lhs());
          const b = logical(r());
          return a && b ? 1 : 0;
        };
      }
    }
    return left;
  }

  private comparison(): Expr {
    let left = this.additive();
    for (;;) {
      const token = this.peek();
      if (token.k !== 'op' || !['=', '<>', '<', '>', '<=', '>='].includes(token.v)) return left;
      this.pos++;
      const l = left;
      const r = this.additive();
      const vm = this.vm;
      left = () => compare(vm, token.v, l(), r());
    }
  }

  private additive(): Expr {
    let left = this.multiplicative();
    while (this.isOp('+') || this.isOp('-')) {
      const op = this.next().v;
      const l = left;
      const r = this.multiplicative();
      const vm = this.vm;
      left = () => arith(vm, op, l(), r());
    }
    return left;
  }

  private multiplicative(): Expr {
    let left = this.unary();
    while (this.isOp('*') || this.isOp('/') || this.isOp('MOD')) {
      const op = this.next().v;
      const l = left;
      const r = this.unary();
      const vm = this.vm;
      left = () => arith(vm, op, l(), r());
    }
    return left;
  }

  private unary(): Expr {
    if (this.isOp('-')) {
      this.pos++;
      const operand = this.unary();
      return () => {
        const value = operand();
        if (typeof value === 'number') return value === INT_MIN ? new Real(2147483648) : -value;
        if (value instanceof Real) return new Real(-value.v);
        if (value === null) return 0;
        return fail(SE_NUMBER_EXPECTED);
      };
    }
    if (this.isOp('+')) {
      this.pos++;
      return this.unary();
    }
    if (this.isOp('NOT')) {
      this.pos++;
      const operand = this.unary();
      return () => (logical(operand()) ? 0 : 1);
    }
    return this.power();
  }

  private power(): Expr {
    const base = this.primary();
    if (this.isOp('^')) {
      this.pos++;
      const exponent = this.unary();
      const vm = this.vm;
      return () => arith(vm, '^', base(), exponent());
    }
    return base;
  }

  private primary(): Expr {
    const token = this.next();
    if (token.k === 'num') {
      const value = token.value!;
      return () => value;
    }
    if (token.k === 'str') {
      const value = token.v;
      return () => value;
    }
    if (token.k === 'op' && token.v === '(') {
      const inner = this.expression();
      this.expectOp(')', SE_CLOSE_BRACKET_EXPECTED);
      return inner;
    }
    if (token.k === 'id') {
      if (KEYWORDS.has(token.v) && token.v !== 'END') fail(SE_SYNTAX);
      const name = token.v;
      if (this.isOp('(')) {
        this.pos++;
        const args = this.argumentList();
        return this.call(name, args);
      }
      const vm = this.vm;
      return () => vm.lookup(name);
    }
    this.pos--;
    return fail(token.k === 'eol' ? SE_SYNTAX : SE_INVALID_EXPRESSION);
  }

  private call(name: string, args: Expr[]): Expr {
    const vm = this.vm;
    if (name === 'BCINPUT' || name === 'BCSLEEP') return () => fail(SE_INVALID_EXPRESSION);
    return () => {
      const native = vm.native(name);
      if (native) {
        const result = native(args.map((arg) => arg()), vm);
        return result === undefined ? null : result;
      }
      if (vm.isArray(name)) return vm.readIndex(name, args.map((arg) => arg()));
      const routine = vm.findRoutine(name);
      if (routine) return vm.callRoutine(routine, args.map((arg) => arg()));
      return fail(SE_INVALID_ID_USAGE);
    };
  }
}
