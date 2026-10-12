/**
 * Classic-to-MY-BASIC translator: a 1:1 port of basic_compat_translate() in
 * fw/proj_cm55/source/basic_compat.c. All positions are indices into the source string.
 */

export const RUNTIME_CAPACITY = 96 * 1024;
export const RUNTIME_ROWS = 4096;
const IDENTIFIER_CAPACITY = 64;

export class TranslateError extends Error {}

export interface Translation {
  text: string;
  /** map[row] = source line (numbered) or source row for each translated row (0-based). */
  map: number[];
}

const isDigit = (c: string): boolean => c >= '0' && c <= '9';
const isAlpha = (c: string): boolean => (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
const isAlnum = (c: string): boolean => isDigit(c) || isAlpha(c);
const isSpace = (c: string): boolean => c === ' ' || c === '\t' || c === '\n' || c === '\r' || c === '\v' || c === '\f';

/** Module state shared across translations, like the firmware's statics. */
let arrays: string[] = [];
let traceEnabled = false;
let traceSource = '';

export function setTrace(enabled: boolean): void {
  traceEnabled = enabled;
}

/** Source of the most recent numbered translation, used by BCTRACE. */
export function traceText(offset: number, length: number): string {
  return traceSource.slice(offset, offset + length);
}

class Writer {
  out: string[] = [];
  length = 0;
  map: number[] = [];
  row = 0;
  line = 0;
  label = 0;
  loops: { variable: string; label: number }[] = [];
  hoist = false;
  trace = false;
  constructor(readonly src: string, readonly capacity: number, readonly mapCapacity: number) {}

  ch(index: number): string {
    return this.src[index] ?? '\0';
  }

  put(text: string): void {
    if (text.length >= this.capacity - this.length) {
      throw new TranslateError('Translated BASIC program exceeds runtime capacity.');
    }
    for (const c of text) {
      if (this.row >= this.mapCapacity) {
        throw new TranslateError('Translated BASIC program has too many statements.');
      }
      this.map[this.row] = this.line;
      if (c === '\n') this.row++;
    }
    this.out.push(text);
    this.length += text.length;
  }

  slice(begin: number, end: number): void {
    this.put(this.src.slice(begin, end));
  }

  spaces(index: number): number {
    while (this.ch(index) === ' ' || this.ch(index) === '\t' || this.ch(index) === '\r') index++;
    return index;
  }

  identifierEnd(index: number): number {
    while (isAlnum(this.ch(index)) || this.ch(index) === '_' || this.ch(index) === '$') index++;
    return index;
  }

  equal(begin: number, end: number, word: string): boolean {
    return end - begin === word.length && this.src.slice(begin, end).toUpperCase() === word;
  }

  trimEnd(begin: number, end: number): number {
    while (end > begin && isSpace(this.ch(end - 1))) end--;
    return end;
  }
}

function arrayName(name: string): boolean {
  return arrays.includes(name.toUpperCase());
}

function matchingClose(w: Writer, open: number, end: number): number {
  let quoted = false;
  let depth = 0;
  for (let cursor = open; cursor < end; cursor++) {
    const c = w.ch(cursor);
    if (c === '"') quoted = !quoted;
    if (quoted) continue;
    if (c === '(') depth++;
    else if (c === ')' && --depth === 0) return cursor;
  }
  return -1;
}

function constantBound(w: Writer, begin: number, end: number): boolean {
  begin = w.spaces(begin);
  end = w.trimEnd(begin, end);
  if (begin === end) return false;
  for (let cursor = begin; cursor < end; cursor++) if (!isDigit(w.ch(cursor))) return false;
  return true;
}

/** C strtoul(text, NULL, 10) on a digit run, saturating. */
function strtoul(w: Writer, index: number): { value: number; end: number } {
  let value = 0;
  const start = index;
  while (isDigit(w.ch(index))) {
    value = Math.min(value * 10 + (w.ch(index).charCodeAt(0) - 48), 0xffffffff);
    index++;
  }
  return { value, end: index === start ? start : index };
}

function traceMarker(w: Writer, begin: number, end: number): void {
  if (!w.trace) return;
  begin = w.spaces(begin);
  end = w.trimEnd(begin, end);
  if (begin === end) return;
  w.put(`BCTRACE(${w.line},${begin},${end - begin})\n`);
}

function find(w: Writer, begin: number, end: number, word: string): number {
  let quoted = false;
  let depth = 0;
  const length = word.length;
  for (let cursor = begin; cursor < end; cursor++) {
    const c = w.ch(cursor);
    if (c === '"') quoted = !quoted;
    if (quoted) continue;
    if (c === "'") return -1;
    if (c === '(') depth++;
    else if (c === ')' && depth > 0) depth--;
    if (
      depth === 0 &&
      end - cursor >= length &&
      w.equal(cursor, cursor + length, word) &&
      (length === 1 || ((cursor === begin || !isAlnum(w.ch(cursor - 1))) && !isAlnum(w.ch(cursor + length))))
    ) {
      return cursor;
    }
  }
  return -1;
}

function expression(w: Writer, begin: number, end: number): void {
  while (begin < end) {
    const c = w.ch(begin);
    if (c === '"') {
      let close = begin + 1;
      while (close < end && w.ch(close) !== '"') close++;
      if (close === end) throw new TranslateError('Unterminated BASIC string.');
      w.slice(begin, close + 1);
      begin = close + 1;
    } else if (isAlpha(c) || c === '_') {
      const finish = w.identifierEnd(begin);
      const name = w.src.slice(begin, finish).toUpperCase();
      const call = w.ch(w.spaces(finish)) === '(';
      let replacement: string | null = null;
      if (name === 'INT') replacement = 'FLOOR';
      else if (name === 'RND') replacement = call ? 'BCRND' : 'BCRND()';
      else if (name === 'MID$') replacement = 'BCMID';
      else if (name === 'LEFT$') replacement = 'BCLEFT';
      else if (name === 'RIGHT$') replacement = 'BCRIGHT';
      else if (name === 'STR$') replacement = 'STR';
      else if (name === 'CHR$') replacement = 'CHR';
      else if (name === 'TAB') replacement = 'BCTAB';
      else if (name === 'TIMER') replacement = call ? 'BCTIMER' : 'BCTIMER()';
      else if (name === 'INKEY$') replacement = call ? 'BCINKEY' : 'BCINKEY()';
      if (replacement === null && call && arrayName(name)) w.put('BCA_');
      if (replacement !== null) w.put(replacement);
      else w.slice(begin, finish);
      begin = finish;
    } else {
      w.slice(begin, begin + 1);
      begin++;
    }
  }
}

function target(w: Writer, begin: number, end: number): void {
  begin = w.spaces(begin);
  if (begin === end) throw new TranslateError('Missing BASIC branch target.');
  if (isDigit(w.ch(begin))) w.put('L');
  w.slice(begin, end);
}

function statement(w: Writer, begin: number, end: number): void {
  begin = w.spaces(begin);
  end = w.trimEnd(begin, end);
  if (begin === end) return;
  const nameEnd = w.identifierEnd(begin);
  const is = (word: string): boolean => w.equal(begin, nameEnd, word);
  let args = w.spaces(nameEnd);
  if (args > end) args = end;
  if (w.hoist && !is('DIM')) return;
  if (is('REM')) {
    w.slice(begin, end);
    w.put('\n');
    return;
  }
  if (is('DEF')) {
    const assignment = find(w, args, end, '=');
    if (assignment < 0) {
      expression(w, begin, end);
      w.put('\n');
      return;
    }
    w.put('DEF ');
    w.slice(args, assignment);
    w.put('\nRETURN ');
    expression(w, assignment + 1, end);
    w.put('\nENDDEF\n');
    return;
  }
  if (is('ON')) {
    let branch = find(w, args, end, 'GOTO');
    let operation = 'GOTO ';
    if (branch < 0) {
      branch = find(w, args, end, 'GOSUB');
      operation = 'GOSUB ';
    }
    if (branch < 0) throw new TranslateError('ON requires GOTO or GOSUB.');
    w.put('BCON = FLOOR(');
    expression(w, args, branch);
    w.put(')\n');
    let cursor = w.spaces(w.identifierEnd(branch));
    let index = 1;
    while (cursor < end) {
      const comma = find(w, cursor, end, ',');
      const finish = comma >= 0 ? comma : end;
      w.put(`IF BCON = ${index++} THEN ${operation}`);
      target(w, cursor, finish);
      w.put('\n');
      cursor = comma >= 0 ? w.spaces(comma + 1) : end;
    }
    return;
  }
  if (is('FOR')) {
    const variableEnd = w.identifierEnd(args);
    const assignment = w.spaces(variableEnd);
    const to = find(w, assignment, end, 'TO');
    const step = to >= 0 ? find(w, to + 2, end, 'STEP') : -1;
    const nameLength = variableEnd - args;
    if (nameLength === 0 || nameLength >= IDENTIFIER_CAPACITY || w.ch(assignment) !== '=' || to < 0 || w.loops.length >= 32) {
      throw new TranslateError('Invalid or excessively nested FOR loop.');
    }
    const label = ++w.label;
    const variable = w.src.slice(args, variableEnd).toUpperCase();
    w.loops.push({ variable, label });
    expression(w, args, to);
    w.put('\n');
    w.put(`BCLIMIT${label}=`);
    expression(w, to + 2, step >= 0 ? step : end);
    w.put('\n');
    w.put(`BCSTEP${label}=`);
    if (step >= 0) expression(w, step + 4, end);
    else w.put('1');
    w.put('\n');
    w.put(
      `BCLOOP${label}: IF (BCSTEP${label}>=0 AND ${variable}>BCLIMIT${label}) OR ` +
        `(BCSTEP${label}<0 AND ${variable}<BCLIMIT${label}) THEN GOTO BCEXIT${label}\n`,
    );
    return;
  }
  if (is('NEXT')) {
    if (w.loops.length === 0) throw new TranslateError('NEXT without FOR.');
    const loop = w.loops.pop()!;
    if (args < end && !w.equal(args, w.identifierEnd(args), loop.variable)) {
      throw new TranslateError('NEXT variable does not match FOR.');
    }
    const { variable, label } = loop;
    w.put(`${variable}=${variable}+BCSTEP${label}\nGOTO BCLOOP${label}\nBCEXIT${label}: BCNOOP()\n`);
    return;
  }
  if (is('DIM')) {
    let cursor = args;
    while (cursor < end) {
      const nameEnd2 = w.identifierEnd(cursor);
      const open = w.spaces(nameEnd2);
      const close = open < end && w.ch(open) === '(' ? matchingClose(w, open, end) : -1;
      if (close < 0 || nameEnd2 === cursor) throw new TranslateError('Invalid classic DIM declaration.');
      let computed = false;
      let bounds = 0;
      for (let bound = open + 1; bound < close; ) {
        const comma = find(w, bound, close, ',');
        const finish = comma >= 0 ? comma : close;
        if (constantBound(w, bound, finish)) {
          if (strtoul(w, w.spaces(bound)).value >= 65535) throw new TranslateError('Classic DIM bound is too large.');
        } else if (w.spaces(bound) === finish) {
          throw new TranslateError('Invalid classic DIM bound.');
        } else {
          computed = true;
        }
        bounds++;
        bound = comma >= 0 ? comma + 1 : close;
      }
      if (bounds === 0) throw new TranslateError('Invalid classic DIM bound.');
      const name = w.src.slice(cursor, nameEnd2);
      if (w.hoist && !arrayName(name)) {
        if (name.length >= IDENTIFIER_CAPACITY || arrays.length >= 32) {
          throw new TranslateError('Too many or invalid classic arrays.');
        }
        arrays.push(name.toUpperCase());
      }
      if (w.hoist || computed) {
        w.put('DIM BCA_');
        w.put(name);
        w.put('(');
        for (let bound = open + 1; bound < close; ) {
          const comma = find(w, bound, close, ',');
          const finish = comma >= 0 ? comma : close;
          if (w.hoist && computed) {
            w.put('1');
          } else if (constantBound(w, bound, finish)) {
            w.put(String(strtoul(w, w.spaces(bound)).value + 1));
          } else {
            w.put('(');
            expression(w, bound, finish);
            w.put(')+1');
          }
          if (comma >= 0) w.put(',');
          bound = comma >= 0 ? comma + 1 : close;
        }
        w.put(')\n');
      }
      cursor = w.spaces(close + 1);
      if (cursor < end) {
        if (w.ch(cursor) !== ',') throw new TranslateError('Invalid classic DIM list.');
        cursor = w.spaces(cursor + 1);
      }
    }
    return;
  }
  if (is('INPUT')) {
    w.put('BCINPUT(');
    if (args < end && w.ch(args) === '"') {
      const close = w.src.indexOf('"', args + 1);
      if (close < 0 || close >= end) throw new TranslateError('Invalid INPUT prompt.');
      w.slice(args, close + 1);
      args = w.spaces(close + 1);
      if (w.ch(args) !== ';' && w.ch(args) !== ',') throw new TranslateError('INPUT prompt requires a separator.');
      args = w.spaces(args + 1);
    } else {
      w.put('""');
    }
    while (args < end) {
      const finish = w.identifierEnd(args);
      if (finish === args) throw new TranslateError('INPUT requires variable names.');
      w.put(',"');
      w.slice(args, finish);
      w.put('"');
      args = w.spaces(finish);
      if (args < end) {
        if (w.ch(args) !== ',') throw new TranslateError('Invalid INPUT variable list.');
        args = w.spaces(args + 1);
      }
    }
    w.put(')\n');
    return;
  }
  if (is('SLEEP') || is('RANDOMIZE')) {
    w.put(is('SLEEP') ? 'BCSLEEP(' : 'BCRANDOMIZE(');
    expression(w, args, end);
    w.put(')\n');
    return;
  }
  if (is('STOP')) {
    w.put('END\n');
    return;
  }
  if (is('PRINT')) {
    w.put('PRINT ');
    if (args === end) {
      w.put('""\n');
      return;
    }
    let cursor = args;
    while (cursor < end) {
      let separator = find(w, cursor, end, ';');
      const comma = find(w, cursor, end, ',');
      if (comma >= 0 && (separator < 0 || comma < separator)) separator = comma;
      const finish = separator >= 0 ? separator : end;
      expression(w, cursor, finish);
      if (separator >= 0) w.put(',');
      cursor = separator >= 0 ? separator + 1 : end;
    }
    w.put('\n');
    return;
  }
  if (is('GOTO') || is('GOSUB') || is('RESTORE')) {
    w.slice(begin, nameEnd);
    w.put(' ');
    target(w, args, end);
    w.put('\n');
    return;
  }
  expression(w, begin, end);
  w.put('\n');
}

function matchingElse(w: Writer, begin: number, end: number): number {
  let nested = 0;
  let quoted = false;
  for (let cursor = begin; cursor < end; cursor++) {
    const c = w.ch(cursor);
    if (c === '"') quoted = !quoted;
    if (quoted) continue;
    if (c === "'") break;
    if (isAlpha(c)) {
      const finish = w.identifierEnd(cursor);
      if (w.equal(cursor, finish, 'REM')) break;
      if (w.equal(cursor, finish, 'IF')) nested++;
      if (w.equal(cursor, finish, 'ELSE')) {
        if (nested === 0) return cursor;
        nested--;
      }
      cursor = finish - 1;
    }
  }
  return -1;
}

function statements(w: Writer, begin: number, end: number, nesting: number): void {
  if (nesting > 16) throw new TranslateError('BASIC IF nesting exceeds 16 levels.');
  while (begin < end) {
    begin = w.spaces(begin);
    const nameEnd = w.identifierEnd(begin);
    if (w.equal(begin, nameEnd, 'REM')) {
      traceMarker(w, begin, end);
      statement(w, begin, end);
      return;
    }
    if (w.equal(begin, nameEnd, 'IF')) {
      const then = find(w, nameEnd, end, 'THEN');
      if (then < 0) throw new TranslateError('IF requires THEN.');
      let body = w.spaces(then + 4);
      const otherwise = matchingElse(w, body, end);
      const finish = otherwise >= 0 ? otherwise : end;
      if (w.hoist) {
        if (body < finish && isDigit(w.ch(body))) {
          const colon = find(w, body, finish, ':');
          body = colon >= 0 ? colon + 1 : finish;
        }
        statements(w, body, finish, nesting + 1);
        if (otherwise >= 0) statements(w, otherwise + 4, end, nesting + 1);
        return;
      }
      const label = ++w.label;
      let jumpEnd = -1;
      if (body < finish && isDigit(w.ch(body))) {
        const colon = find(w, body, finish, ':');
        jumpEnd = colon >= 0 ? colon : finish;
      }
      traceMarker(w, begin, jumpEnd >= 0 ? jumpEnd : then + 4);
      w.put('IF NOT(');
      expression(w, nameEnd, then);
      w.put(`) THEN GOTO BCSKIP${label}\n`);
      if (jumpEnd >= 0) {
        w.put('GOTO ');
        target(w, body, jumpEnd);
        w.put('\n');
        body = jumpEnd < finish ? jumpEnd + 1 : finish;
      }
      statements(w, body, finish, nesting + 1);
      if (otherwise >= 0) {
        w.put(`GOTO BCEND${label}\nBCSKIP${label}: BCNOOP()\n`);
        statements(w, otherwise + 4, end, nesting + 1);
        w.put(`BCEND${label}: BCNOOP()\n`);
      } else {
        w.put(`BCSKIP${label}: BCNOOP()\n`);
      }
      return;
    }
    const colon = find(w, begin, end, ':');
    const finish = colon >= 0 ? colon : end;
    traceMarker(w, begin, finish);
    if (isDigit(w.ch(begin))) {
      if (!w.hoist) {
        w.put('GOTO ');
        target(w, begin, finish);
        w.put('\n');
      }
    } else {
      statement(w, begin, finish);
    }
    begin = colon >= 0 ? colon + 1 : end;
  }
}

/** Translate classic source. Throws TranslateError with the firmware's message on failure. */
export function translate(source: string, numbered: boolean, capacity = RUNTIME_CAPACITY, mapCapacity = RUNTIME_ROWS): Translation {
  const w = new Writer(source, capacity, mapCapacity);
  if (numbered) {
    arrays = [];
    traceSource = source;
  }
  for (let pass = 0; pass < 3; pass++) {
    let cursor = 0;
    let row = 0;
    while (cursor < source.length) {
      row++;
      let end = source.indexOf('\n', cursor);
      if (end < 0) end = source.length;
      let body = w.spaces(cursor);
      w.line = row;
      if (numbered) {
        const number = strtoul(w, body);
        if (number.end === body || number.value === 0 || number.value > 65535) {
          throw new TranslateError('Invalid BASIC line number.');
        }
        w.line = number.value;
        body = w.spaces(number.end);
      }
      const definition = w.equal(body, w.identifierEnd(body), 'DEF');
      w.hoist = pass === 0;
      w.trace = traceEnabled && numbered && pass === 2;
      if (pass === 0 || (definition && pass === 1) || (!definition && pass === 2)) {
        if (pass === 2 && numbered) w.put(`L${w.line}: BCNOOP()\n`);
        statements(w, body, end, 0);
      } else if (pass === 2 && numbered) {
        w.put(`L${w.line}: BCNOOP()\n`);
        traceMarker(w, body, end);
      }
      cursor = source[end] === '\n' ? end + 1 : end;
    }
  }
  if (w.loops.length !== 0) throw new TranslateError('FOR without NEXT.');
  return { text: w.out.join(''), map: w.map };
}
