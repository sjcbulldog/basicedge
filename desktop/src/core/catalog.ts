/** Port of fw/proj_cm55/source/basic_catalog.c: bounded parser for progs.json. */

export const CATALOG_MAX_PROGRAMS = 32;
const MAX_DEPTH = 16;
const FILENAME_CAPACITY = 64;
const TYPE_CAPACITY = 32;
const DESCRIPTION_CAPACITY = 192;
const KEY_CAPACITY = 16;

export interface CatalogProgram {
  filename: string;
  type: string;
  description: string;
  size: number;
}

class Parser {
  cursor = 0;
  depth = 0;
  constructor(readonly text: string) {}

  skipWhitespace(): void {
    while (this.cursor < this.text.length && ' \t\r\n'.includes(this.text[this.cursor])) this.cursor++;
  }

  consume(expected: string): boolean {
    this.skipWhitespace();
    if (this.cursor >= this.text.length || this.text[this.cursor] !== expected) return false;
    this.cursor++;
    return true;
  }

  peek(expected: string): boolean {
    this.skipWhitespace();
    return this.cursor < this.text.length && this.text[this.cursor] === expected;
  }

  /** Returns the string (null on failure). capacity 0 validates without a length limit. */
  parseString(capacity: number): string | null {
    let output = '';
    if (!this.consume('"')) return null;
    while (this.cursor < this.text.length) {
      let value = this.text[this.cursor++];
      if (value === '"') return output;
      if (value.charCodeAt(0) < 0x20) return null;
      if (value === '\\') {
        if (this.cursor >= this.text.length) return null;
        value = this.text[this.cursor++];
        switch (value) {
          case '"': case '\\': case '/': break;
          case 'b': value = '\b'; break;
          case 'f': value = '\f'; break;
          case 'n': value = '\n'; break;
          case 'r': value = '\r'; break;
          case 't': value = '\t'; break;
          case 'u': {
            if (this.text.length - this.cursor < 4) return null;
            const digits = this.text.slice(this.cursor, this.cursor + 4);
            if (!/^[0-9a-fA-F]{4}$/.test(digits)) return null;
            this.cursor += 4;
            const code = parseInt(digits, 16);
            if (code < 0x20 || code > 0x7e) return null;
            value = String.fromCharCode(code);
            break;
          }
          default:
            return null;
        }
      }
      if (capacity > 0) {
        if (output.length + 1 >= capacity) return null;
        output += value;
      }
    }
    return null;
  }

  parseSize(): number | null {
    this.skipWhitespace();
    const isDigit = (index: number): boolean => index < this.text.length && /[0-9]/.test(this.text[index]);
    if (!isDigit(this.cursor)) return null;
    if (this.text[this.cursor] === '0' && isDigit(this.cursor + 1)) return null;
    let value = 0;
    while (isDigit(this.cursor)) {
      const digit = this.text.charCodeAt(this.cursor++) - 48;
      if (value > Math.floor((0xffffffff - digit) / 10)) return null;
      value = value * 10 + digit;
    }
    if (this.cursor < this.text.length && '.eE'.includes(this.text[this.cursor])) return null;
    return value;
  }

  private skipDigits(): boolean {
    const start = this.cursor;
    while (this.cursor < this.text.length && /[0-9]/.test(this.text[this.cursor])) this.cursor++;
    return this.cursor !== start;
  }

  private skipNumber(): boolean {
    if (this.text[this.cursor] === '-') this.cursor++;
    if (this.text[this.cursor] === '0') this.cursor++;
    else if (!this.skipDigits()) return false;
    if (this.text[this.cursor] === '.') {
      this.cursor++;
      if (!this.skipDigits()) return false;
    }
    if (this.text[this.cursor] === 'e' || this.text[this.cursor] === 'E') {
      this.cursor++;
      if (this.text[this.cursor] === '+' || this.text[this.cursor] === '-') this.cursor++;
      if (!this.skipDigits()) return false;
    }
    return true;
  }

  private skipLiteral(word: string): boolean {
    if (!this.text.startsWith(word, this.cursor)) return false;
    this.cursor += word.length;
    return true;
  }

  private skipContainer(close: string, object: boolean): boolean {
    if (++this.depth > MAX_DEPTH) return false;
    this.cursor++;
    if (this.consume(close)) {
      this.depth--;
      return true;
    }
    do {
      if (object && (this.parseString(0) === null || !this.consume(':'))) return false;
      if (!this.skipValue()) return false;
    } while (this.consume(','));
    if (!this.consume(close)) return false;
    this.depth--;
    return true;
  }

  skipValue(): boolean {
    this.skipWhitespace();
    if (this.cursor >= this.text.length) return false;
    switch (this.text[this.cursor]) {
      case '"': return this.parseString(0) !== null;
      case '{': return this.skipContainer('}', true);
      case '[': return this.skipContainer(']', false);
      case 't': return this.skipLiteral('true');
      case 'f': return this.skipLiteral('false');
      case 'n': return this.skipLiteral('null');
      default: return this.skipNumber();
    }
  }
}

function validFilename(filename: string): boolean {
  return /^[A-Za-z0-9]/.test(filename) && !filename.includes('..') && /^[A-Za-z0-9._-]*$/.test(filename);
}

function parseProgram(parser: Parser): CatalogProgram | null {
  const program: CatalogProgram = { filename: '', type: '', description: '', size: 0 };
  const seen = new Set<string>();
  if (!parser.consume('{')) return null;
  if (parser.consume('}')) return null;
  do {
    const key = parser.parseString(KEY_CAPACITY);
    if (key === null || !parser.consume(':')) return null;
    const fields: Record<string, number> = { filename: FILENAME_CAPACITY, type: TYPE_CAPACITY, description: DESCRIPTION_CAPACITY };
    if (key in fields) {
      const value = seen.has(key) ? null : parser.parseString(fields[key]);
      if (value === null) return null;
      program[key as 'filename' | 'type' | 'description'] = value;
      seen.add(key);
    } else if (key === 'size') {
      const size = seen.has(key) ? null : parser.parseSize();
      if (size === null) return null;
      program.size = size;
      seen.add(key);
    } else if (!parser.skipValue()) {
      return null;
    }
  } while (parser.consume(','));
  return parser.consume('}') && seen.has('filename') && seen.has('size') && validFilename(program.filename) ? program : null;
}

function parsePrograms(parser: Parser, programs: CatalogProgram[]): boolean {
  if (!parser.consume('[')) return false;
  if (parser.consume(']')) return true;
  do {
    if (programs.length >= CATALOG_MAX_PROGRAMS) return false;
    const program = parseProgram(parser);
    if (!program) return false;
    if (programs.some((entry) => entry.filename.toLowerCase() === program.filename.toLowerCase())) return false;
    programs.push(program);
  } while (parser.consume(','));
  return parser.consume(']');
}

/** Parses catalog JSON (as a byte string); returns null when invalid. */
export function parseCatalog(json: string): CatalogProgram[] | null {
  const parser = new Parser(json);
  const programs: CatalogProgram[] = [];
  let hasPrograms = false;
  if (json.startsWith('\xEF\xBB\xBF') || json.startsWith('\uFEFF')) parser.cursor = json.startsWith('\uFEFF') ? 1 : 3;
  if (!parser.consume('{') || parser.peek('}')) return null;
  do {
    const key = parser.parseString(KEY_CAPACITY);
    if (key === null || !parser.consume(':')) return null;
    if (key === 'programs') {
      if (hasPrograms || !parsePrograms(parser, programs)) return null;
      hasPrograms = true;
    } else if (!parser.skipValue()) {
      return null;
    }
  } while (parser.consume(','));
  if (!parser.consume('}') || !hasPrograms) return null;
  parser.skipWhitespace();
  return parser.cursor === json.length ? programs : null;
}

export function findProgram(programs: CatalogProgram[], name: string): CatalogProgram | undefined {
  const lower = name.toLowerCase();
  return programs.find((program) => {
    const filename = program.filename.toLowerCase();
    return filename === lower || filename === `${lower}.bas`;
  });
}
