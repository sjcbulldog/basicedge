/** Runtime value model mirroring MY-BASIC's int_t (int32), real_t (float) and strings. */

/** A MY-BASIC real (single precision float) that must keep its real type even when integral. */
export class Real {
  readonly v: number;
  constructor(v: number) {
    this.v = Math.fround(v);
  }
}

/** int (JS number holding an int32), Real, string, or nil (null). */
export type Value = number | Real | string | null;

export const INT_MIN = -2147483648;
export const INT_MAX = 2147483647;

export function isReal(value: Value): value is Real {
  return value instanceof Real;
}

/** Numeric content of a value (nil counts as 0). */
export function num(value: Value): number {
  if (typeof value === 'number') return value;
  if (value instanceof Real) return value.v;
  return 0;
}

/** Mirrors _convert_to_int_if_posible: integral reals in int range become ints. */
export function realOrInt(x: number): Value {
  const f = Math.fround(x);
  if (Number.isInteger(f) && f >= INT_MIN && f <= INT_MAX) return f | 0;
  return new Real(f);
}

/** C float-to-int conversion (truncation, saturating like the Cortex-M VCVT instruction). */
export function toInt(x: number): number {
  if (Number.isNaN(x)) return 0;
  if (x >= INT_MAX) return INT_MAX;
  if (x <= INT_MIN) return INT_MIN;
  return Math.trunc(x) | 0;
}

/** printf("%g") for a float promoted to double. */
export function formatG(x: number, precision = 6): string {
  if (Number.isNaN(x)) return x < 0 ? '-nan' : 'nan';
  if (x === Infinity) return 'inf';
  if (x === -Infinity) return '-inf';
  if (x === 0) return Object.is(x, -0) ? '-0' : '0';
  const p = precision === 0 ? 1 : precision;
  const exponential = x.toExponential(p - 1);
  const exponent = parseInt(exponential.slice(exponential.indexOf('e') + 1), 10);
  if (exponent < -4 || exponent >= p) {
    let mantissa = exponential.slice(0, exponential.indexOf('e'));
    if (mantissa.includes('.')) mantissa = mantissa.replace(/0+$/, '').replace(/\.$/, '');
    const sign = exponent < 0 ? '-' : '+';
    const digits = String(Math.abs(exponent)).padStart(2, '0');
    return `${mantissa}e${sign}${digits}`;
  }
  let fixed = x.toFixed(Math.max(0, p - 1 - exponent));
  if (fixed.includes('.')) fixed = fixed.replace(/0+$/, '').replace(/\.$/, '');
  return fixed;
}

/** Text produced by PRINT / STR for a value. */
export function formatValue(value: Value): string {
  if (value === null) return 'NIL';
  if (typeof value === 'number') return String(value);
  if (value instanceof Real) return formatG(value.v);
  return value;
}

/** C strtol(text, &end, 0) semantics: returns the value and the index where parsing stopped. */
export function strtol(text: string, start = 0): { value: number; end: number } {
  let index = start;
  while (index < text.length && /[ \t\n\v\f\r]/.test(text[index])) index++;
  let negative = false;
  if (text[index] === '+' || text[index] === '-') {
    negative = text[index] === '-';
    index++;
  }
  let base = 10;
  if (text[index] === '0' && (text[index + 1] === 'x' || text[index + 1] === 'X') && /[0-9a-fA-F]/.test(text[index + 2] ?? '')) {
    base = 16;
    index += 2;
  } else if (text[index] === '0') {
    base = 8;
  }
  const digits = base === 16 ? /[0-9a-fA-F]/ : base === 8 ? /[0-7]/ : /[0-9]/;
  const first = index;
  let value = 0;
  while (index < text.length && digits.test(text[index])) {
    value = value * base + parseInt(text[index], 16);
    index++;
  }
  if (index === first) return { value: 0, end: start };
  value = negative ? -value : value;
  // strtol saturates at LONG range (32-bit long on the target); the caller casts to int_t.
  if (value > INT_MAX) value = INT_MAX;
  if (value < INT_MIN) value = INT_MIN;
  return { value, end: index };
}

/** C strtod semantics (decimal, inf and nan forms; hex floats are not needed by BASIC sources). */
export function strtod(text: string, start = 0): { value: number; end: number } {
  let index = start;
  while (index < text.length && /[ \t\n\v\f\r]/.test(text[index])) index++;
  const rest = text.slice(index);
  const special = /^[+-]?(inf(inity)?|nan)/i.exec(rest);
  if (special) {
    const negative = special[0].startsWith('-');
    const isNan = /nan/i.test(special[0]);
    const value = isNan ? NaN : negative ? -Infinity : Infinity;
    return { value, end: index + special[0].length };
  }
  const match = /^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?/.exec(rest);
  if (!match) return { value: 0, end: start };
  return { value: parseFloat(match[0]), end: index + match[0].length };
}
