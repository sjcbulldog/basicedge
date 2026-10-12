/**
 * Model of the BASIC-to-LVGL graphics bridge (fw/proj_cm55/source/basic_graphics.c).
 * Objects are laid out the way the LVGL v9 light theme does on the 800x480 display and
 * exported as a flat scene that the graphics window paints on a canvas.
 */
import { BasicError, MB_FUNC_ERR, Vm, arity, popInt, popString, SE_CLOSE_BRACKET_EXPECTED, SE_SYNTAX } from './vm';
import { Value } from './values';

export const GFX_WIDTH = 800;
export const GFX_HEIGHT = 480;
export const GFX_MAX_OBJECTS = 64;
const TEXT_LIMIT = 127;

export const SCREEN_BG_COLOR = 0xadd8e6;
export const PRIMARY = 0x2196f3;
export const TEXT_COLOR = 0x212121;
const BORDER_COLOR = 0xe0e0e0;
const GREY = 0xe0e0e0;
/* LV_DPX(n) for a 130 DPI panel. */
const PAD_DEF = 20;
const PAD_SMALL = 11;
const PAD_TINY = 7;
const BORDER = 2;
const RADIUS = 10;
const RADIUS_CIRCLE = 0x7fff;

export type GfxType =
  | 'SCREEN' | 'OBJ' | 'LABEL' | 'BUTTON' | 'BAR' | 'SLIDER' | 'CHECKBOX' | 'SWITCH'
  | 'DROPDOWN' | 'TEXTAREA' | 'KEYBOARD' | 'IMAGE' | 'ARC' | 'LED';

const CREATABLE: GfxType[] = ['OBJ', 'LABEL', 'BUTTON', 'BAR', 'SLIDER', 'CHECKBOX', 'SWITCH', 'DROPDOWN', 'TEXTAREA', 'KEYBOARD', 'IMAGE', 'ARC', 'LED'];
const ALIGNMENTS = ['TOP_LEFT', 'TOP_MID', 'TOP_RIGHT', 'LEFT_MID', 'CENTER', 'RIGHT_MID', 'BOTTOM_LEFT', 'BOTTOM_MID', 'BOTTOM_RIGHT'] as const;
type Alignment = (typeof ALIGNMENTS)[number];
const FONTS: Record<string, number> = { MONTSERRAT_12: 12, MONTSERRAT_14: 14, MONTSERRAT_16: 16 };
export const LINE_HEIGHT: Record<number, number> = { 12: 15, 14: 16, 16: 18 };

type Size = number | { pct: number } | 'content';

interface GfxObject {
  type: GfxType;
  parent: GfxObject | null;
  children: GfxObject[];
  x: number;
  y: number;
  align: Alignment;
  w: Size;
  h: Size;
  hidden: boolean;
  disabled: boolean;
  checked: boolean;
  pressed: boolean;
  text: string;
  value: number;
  min: number;
  max: number;
  options: string[];
  selected: number;
  bgColor: number | null;
  opacity: number;
  radius: number | null;
  font: number | null;
}

/** One painted object; coordinates are absolute display pixels. */
export interface SceneNode {
  type: GfxType;
  x: number;
  y: number;
  w: number;
  h: number;
  clip: [number, number, number, number];
  opacity: number;
  radius: number;
  bg: number;
  bgOpa: number;
  border: number;
  borderColor: number;
  pad: number;
  textColor: number;
  font: number;
  lines: string[];
  value: number;
  min: number;
  max: number;
  checked: boolean;
  disabled: boolean;
  pressed: boolean;
  selectedText: string;
}

export interface Scene {
  width: number;
  height: number;
  version: number;
  nodes: SceneNode[];
}

export type Measure = (text: string, fontSize: number) => number;

/** Rough Montserrat advance width; used when no canvas is available (tests). */
export const approximateMeasure: Measure = (text, size) => Math.round(text.length * size * 0.6);

interface Rect {
  x: number;
  y: number;
  w: number;
  h: number;
}

export class Graphics {
  private readonly handles: (GfxObject | null)[] = new Array(GFX_MAX_OBJECTS).fill(null);
  private readonly screen: GfxObject;
  private readonly startTime: number;
  private version = 0;
  /** True once any GFX function has run (the desktop opens the window lazily). */
  used = false;
  onChange: (() => void) | null = null;

  constructor(private readonly measure: Measure = approximateMeasure, private readonly now: () => number = () => Date.now()) {
    this.screen = this.make('SCREEN', null);
    this.screen.w = GFX_WIDTH;
    this.screen.h = GFX_HEIGHT;
    this.startTime = now();
  }

  /* --------------------------------------------------------- handles */

  private make(type: GfxType, parent: GfxObject | null): GfxObject {
    const object: GfxObject = {
      type, parent, children: [], x: 0, y: 0, align: 'TOP_LEFT', w: 'content', h: 'content',
      hidden: false, disabled: false, checked: false, pressed: false, text: '', value: 0, min: 0, max: 100,
      options: [], selected: 0, bgColor: null, opacity: 255, radius: null, font: null,
    };
    switch (type) {
      case 'OBJ': object.w = 130; object.h = 130; break;
      case 'LABEL': object.text = 'Text'; break;
      case 'CHECKBOX': object.text = 'Check box'; break;
      case 'BAR':
      case 'SLIDER': object.w = 260; object.h = 13; break;
      case 'SWITCH': object.w = 52; object.h = 30; break;
      case 'DROPDOWN': object.w = 130; object.options = ['Option 1', 'Option 2', 'Option 3']; break;
      case 'TEXTAREA': object.w = 260; object.h = 130; break;
      case 'KEYBOARD': object.w = { pct: 100 }; object.h = { pct: 50 }; object.align = 'BOTTOM_MID'; break;
      case 'IMAGE': object.w = 0; object.h = 0; break;
      case 'ARC': object.w = 130; object.h = 130; object.value = 0; break;
      case 'LED': object.w = 26; object.h = 26; break;
      default: break;
    }
    if (parent) parent.children.push(object);
    if (type === 'TEXTAREA') {
      // lv_textarea owns a label child (child 0), which GFX_TEXT reaches.
      const label = this.make('LABEL', object);
      label.text = '';
    }
    return object;
  }

  private objectFromHandle(handle: number): GfxObject | null {
    if (handle <= 0 || handle > GFX_MAX_OBJECTS) return null;
    return this.handles[handle - 1];
  }

  private handleFromObject(object: GfxObject | null): number {
    if (!object) return 0;
    const index = this.handles.indexOf(object);
    return index < 0 ? 0 : index + 1;
  }

  private allocate(object: GfxObject): number {
    const index = this.handles.indexOf(null);
    if (index < 0) return 0;
    this.handles[index] = object;
    return index + 1;
  }

  private isDescendant(object: GfxObject, ancestor: GfxObject): boolean {
    for (let parent = object.parent; parent; parent = parent.parent) if (parent === ancestor) return true;
    return false;
  }

  private release(object: GfxObject): void {
    this.handles.forEach((entry, index) => {
      if (entry && (entry === object || this.isDescendant(entry, object))) this.handles[index] = null;
    });
  }

  private remove(object: GfxObject): void {
    const parent = object.parent;
    if (parent) parent.children.splice(parent.children.indexOf(object), 1);
    object.parent = null;
  }

  private changed(): void {
    this.version++;
    this.onChange?.();
  }

  /** GFX CLEAR / RESET and console reinitialisation. */
  clear(): void {
    this.handles.forEach((entry, index) => {
      if (entry && entry !== this.screen) {
        this.remove(entry);
        this.handles[index] = null;
      }
    });
    this.changed();
  }

  /** Rows for GFX LIST. */
  list(): { handle: number; type: string; parent: number }[] {
    const rows: { handle: number; type: string; parent: number }[] = [];
    this.handles.forEach((entry, index) => {
      if (entry) {
        rows.push({ handle: index + 1, type: entry.type === 'SCREEN' ? 'OBJ' : entry.type, parent: this.handleFromObject(entry.parent) });
      }
    });
    return rows;
  }

  /* ---------------------------------------------------------- layout */

  private font(object: GfxObject): number {
    for (let node: GfxObject | null = object; node; node = node.parent) if (node.font !== null) return node.font;
    return 14;
  }

  private padding(object: GfxObject): { hor: number; ver: number } {
    switch (object.type) {
      case 'OBJ': return { hor: PAD_DEF, ver: PAD_DEF };
      case 'BUTTON': return { hor: PAD_DEF, ver: PAD_SMALL };
      case 'DROPDOWN': return { hor: PAD_SMALL, ver: PAD_SMALL };
      case 'TEXTAREA': return { hor: PAD_SMALL, ver: PAD_SMALL };
      case 'KEYBOARD': return { hor: 4, ver: 4 };
      default: return { hor: 0, ver: 0 };
    }
  }

  private border(object: GfxObject): number {
    return object.type === 'OBJ' || object.type === 'DROPDOWN' || object.type === 'TEXTAREA' ? BORDER : 0;
  }

  /** Wraps text the way LV_LABEL_LONG_WRAP does for a fixed width (or splits on newlines only). */
  private lines(text: string, font: number, width: number | null): string[] {
    const result: string[] = [];
    for (const paragraph of text.split('\n')) {
      if (width === null || this.measure(paragraph, font) <= width) {
        result.push(paragraph);
        continue;
      }
      let line = '';
      for (const word of paragraph.split(/(?<= )/)) {
        if (line && this.measure(line + word, font) > width) {
          result.push(line.trimEnd());
          line = '';
        }
        line += word;
      }
      result.push(line.trimEnd());
    }
    return result;
  }

  private contentSize(object: GfxObject, width: number | null): { w: number; h: number } {
    const font = this.font(object);
    const lineHeight = LINE_HEIGHT[font];
    switch (object.type) {
      case 'LABEL': {
        const lines = this.lines(object.text, font, width);
        return { w: Math.max(0, ...lines.map((line) => this.measure(line, font))), h: lines.length * lineHeight };
      }
      case 'CHECKBOX':
        return { w: lineHeight + PAD_TINY + this.measure(object.text, font), h: lineHeight };
      case 'DROPDOWN':
        return { w: 130, h: lineHeight + 2 * (PAD_SMALL + BORDER) };
      default: {
        const pad = this.padding(object);
        const border = this.border(object);
        let w = 0;
        let h = 0;
        for (const child of object.children) {
          if (child.hidden) continue;
          const size = this.size(child, { x: 0, y: 0, w: 0, h: 0 });
          const anchored = child.align === 'TOP_LEFT';
          w = Math.max(w, (anchored ? child.x : Math.abs(child.x)) + size.w);
          h = Math.max(h, (anchored ? child.y : Math.abs(child.y)) + size.h);
        }
        return { w: w + 2 * (pad.hor + border), h: h + 2 * (pad.ver + border) };
      }
    }
  }

  private size(object: GfxObject, content: Rect): { w: number; h: number } {
    const resolve = (size: Size, total: number): number | null =>
      size === 'content' ? null : typeof size === 'number' ? size : Math.trunc((total * size.pct) / 100);
    const fixedW = resolve(object.w, content.w);
    const fixedH = resolve(object.h, content.h);
    if (fixedW !== null && fixedH !== null) return { w: fixedW, h: fixedH };
    const measured = this.contentSize(object, fixedW);
    return { w: fixedW ?? measured.w, h: fixedH ?? measured.h };
  }

  /** Position relative to the parent's content area (what lv_obj_get_x/y report). */
  private childRect(object: GfxObject, content: Rect): Rect {
    const { w, h } = this.size(object, content);
    let x = object.x;
    let y = object.y;
    const mode = object.align;
    if (mode.endsWith('_MID') && mode.startsWith('TOP')) x += Math.trunc(content.w / 2) - Math.trunc(w / 2);
    if (mode.endsWith('_MID') && mode.startsWith('BOTTOM')) x += Math.trunc(content.w / 2) - Math.trunc(w / 2);
    if (mode === 'CENTER') {
      x += Math.trunc(content.w / 2) - Math.trunc(w / 2);
      y += Math.trunc(content.h / 2) - Math.trunc(h / 2);
    }
    if (mode.endsWith('RIGHT') || mode === 'RIGHT_MID') x += content.w - w;
    if (mode === 'LEFT_MID' || mode === 'RIGHT_MID') y += Math.trunc(content.h / 2) - Math.trunc(h / 2);
    if (mode.startsWith('BOTTOM')) y += content.h - h;
    return { x, y, w, h };
  }

  private contentRect(object: GfxObject, rect: Rect): Rect {
    const pad = this.padding(object);
    const border = this.border(object);
    return {
      x: rect.x + pad.hor + border,
      y: rect.y + pad.ver + border,
      w: Math.max(0, rect.w - 2 * (pad.hor + border)),
      h: Math.max(0, rect.h - 2 * (pad.ver + border)),
    };
  }

  private absoluteRect(object: GfxObject): Rect {
    if (!object.parent) return { x: 0, y: 0, w: GFX_WIDTH, h: GFX_HEIGHT };
    const parent = this.absoluteRect(object.parent);
    const content = this.contentRect(object.parent, parent);
    const rect = this.childRect(object, content);
    return { x: content.x + rect.x, y: content.y + rect.y, w: rect.w, h: rect.h };
  }

  private relativeRect(object: GfxObject): Rect {
    if (!object.parent) return { x: 0, y: 0, w: GFX_WIDTH, h: GFX_HEIGHT };
    const parent = this.absoluteRect(object.parent);
    return this.childRect(object, this.contentRect(object.parent, parent));
  }

  /* ----------------------------------------------------------- scene */

  scene(): Scene {
    const nodes: SceneNode[] = [];
    const visit = (object: GfxObject, rect: Rect, clip: [number, number, number, number], opacity: number): void => {
      const node = this.node(object, rect, clip, opacity);
      nodes.push(node);
      const content = this.contentRect(object, rect);
      const childClip: [number, number, number, number] = [
        Math.max(clip[0], rect.x), Math.max(clip[1], rect.y),
        Math.min(clip[2], rect.x + rect.w), Math.min(clip[3], rect.y + rect.h),
      ];
      if (object.type === 'TEXTAREA' && object.children[0]?.type === 'LABEL') {
        node.lines = this.lines(object.children[0].text, node.font, content.w);
      }
      for (const child of object.children) {
        if (child.hidden || (object.type === 'TEXTAREA' && child === object.children[0])) continue;
        const local = this.childRect(child, content);
        visit(child, { x: content.x + local.x, y: content.y + local.y, w: local.w, h: local.h }, childClip, node.opacity);
      }
    };
    visit(this.screen, { x: 0, y: 0, w: GFX_WIDTH, h: GFX_HEIGHT }, [0, 0, GFX_WIDTH, GFX_HEIGHT], 255);
    return { width: GFX_WIDTH, height: GFX_HEIGHT, version: this.version, nodes };
  }

  private node(object: GfxObject, rect: Rect, clip: [number, number, number, number], parentOpacity: number): SceneNode {
    const font = this.font(object);
    const textColor = this.inheritedTextColor(object);
    let bg = 0xffffff;
    let bgOpa = 255;
    let border = this.border(object);
    let radius = 0;
    switch (object.type) {
      case 'SCREEN': bg = SCREEN_BG_COLOR; break;
      case 'OBJ': case 'DROPDOWN': case 'TEXTAREA': radius = RADIUS; break;
      case 'LABEL': case 'IMAGE': case 'CHECKBOX': bgOpa = 0; break;
      case 'BUTTON': bg = PRIMARY; radius = RADIUS; break;
      // LVGL's default theme draws bar/slider tracks as the main colour at LV_OPA_20.
      case 'BAR': case 'SLIDER': bg = PRIMARY; bgOpa = 51; radius = RADIUS_CIRCLE; break;
      case 'SWITCH': bg = GREY; radius = RADIUS_CIRCLE; break;
      case 'KEYBOARD': bg = 0xf5f5f5; break;
      case 'ARC': bgOpa = 0; break;
      case 'LED': bg = PRIMARY; radius = RADIUS_CIRCLE; break;
    }
    if (object.bgColor !== null) bg = object.bgColor;
    if (object.radius !== null) radius = object.radius;
    if (object.type === 'LABEL') border = 0;
    const pad = this.padding(object);
    const lines = object.type === 'LABEL'
      ? this.lines(object.text, font, object.w === 'content' ? null : rect.w)
      : object.type === 'CHECKBOX' ? [object.text] : [];
    return {
      type: object.type, x: rect.x, y: rect.y, w: rect.w, h: rect.h, clip,
      opacity: Math.round((parentOpacity * object.opacity) / 255), radius, bg, bgOpa, border,
      borderColor: BORDER_COLOR, pad: pad.hor, textColor, font, lines,
      value: object.value, min: object.min, max: object.max, checked: object.checked,
      disabled: object.disabled, pressed: object.pressed,
      selectedText: object.options[object.selected] ?? '',
    };
  }

  private inheritedTextColor(object: GfxObject): number {
    for (let node: GfxObject | null = object; node; node = node.parent) if (node.type === 'BUTTON') return 0xffffff;
    return TEXT_COLOR;
  }

  /* ----------------------------------------------------- interaction */

  private hit(x: number, y: number): GfxObject | null {
    let found: GfxObject | null = null;
    const visit = (object: GfxObject, rect: Rect): void => {
      if (x < rect.x || y < rect.y || x >= rect.x + rect.w || y >= rect.y + rect.h) return;
      if (object.type !== 'LABEL' || !object.parent || object.parent.type === 'SCREEN' || object.parent.type === 'OBJ') {
        found = object;
      }
      const content = this.contentRect(object, rect);
      for (const child of object.children) {
        if (child.hidden) continue;
        const local = this.childRect(child, content);
        visit(child, { x: content.x + local.x, y: content.y + local.y, w: local.w, h: local.h });
      }
    };
    visit(this.screen, { x: 0, y: 0, w: GFX_WIDTH, h: GFX_HEIGHT });
    return found;
  }

  private active: GfxObject | null = null;

  /** Pointer input from the graphics window, in display coordinates. */
  pointer(kind: 'down' | 'move' | 'up', x: number, y: number): void {
    if (kind === 'down') {
      let target = this.hit(x, y);
      while (target && target.type === 'LABEL' && target.parent?.type === 'BUTTON') target = target.parent;
      if (!target || target.disabled || target.type === 'SCREEN') return;
      this.active = target;
      target.pressed = true;
    }
    const target = this.active;
    if (!target) return;
    const rect = this.absoluteRect(target);
    if ((target.type === 'SLIDER' || target.type === 'ARC') && kind !== 'up') {
      let ratio: number;
      if (target.type === 'SLIDER') {
        ratio = rect.w >= rect.h ? (x - rect.x) / Math.max(1, rect.w) : 1 - (y - rect.y) / Math.max(1, rect.h);
      } else {
        const angle = (Math.atan2(y - (rect.y + rect.h / 2), x - (rect.x + rect.w / 2)) * 180) / Math.PI;
        ratio = (((angle - 135) % 360) + 360) % 360 / 270;
        if (ratio > 1) ratio = ratio > 1.17 ? 0 : 1;
      }
      ratio = Math.min(1, Math.max(0, ratio));
      target.value = Math.round(target.min + ratio * (target.max - target.min));
    }
    if (kind === 'up') {
      target.pressed = false;
      const inside = x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
      if (inside && (target.type === 'SWITCH' || target.type === 'CHECKBOX')) target.checked = !target.checked;
      if (inside && target.type === 'DROPDOWN' && target.options.length) {
        target.selected = (target.selected + 1) % target.options.length;
      }
      this.active = null;
    }
    this.changed();
  }

  /* ------------------------------------------------- BASIC functions */

  /** Registers GFX_* on a VM. Failures are silent MB_FUNC_ERR, exactly like call_graphics. */
  register(vm: Vm): void {
    const failed = (): never => {
      throw new BasicError(null, MB_FUNC_ERR);
    };
    const object = (handle: Value): GfxObject => this.objectFromHandle(popInt(handle)) ?? failed();
    const between = (args: Value[], min: number, max: number): void => {
      if (args.length > max) throw new BasicError(SE_CLOSE_BRACKET_EXPECTED);
      if (args.length < min) throw new BasicError(SE_SYNTAX);
    };
    const define = (name: string, fn: (args: Value[]) => Value | undefined, mutates = true): void => {
      vm.register(name, (args) => {
        this.used = true;
        const result = fn(args);
        if (mutates) this.changed();
        return result;
      });
    };
    const text = (value: Value): string => popString(value).slice(0, TEXT_LIMIT);

    define('GFX_WIDTH', (args) => (arity(args, 0), GFX_WIDTH), false);
    define('GFX_HEIGHT', (args) => (arity(args, 0), GFX_HEIGHT), false);
    define('GFX_TICK', (args) => (arity(args, 0), (this.now() - this.startTime) | 0), false);
    define('GFX_SCREEN', (args) => {
      arity(args, 0);
      return this.handleFromObject(this.screen) || this.allocate(this.screen) || failed();
    });
    define('GFX_CREATE', (args) => {
      between(args, 1, 2);
      const type = popString(args[0]).toUpperCase() as GfxType;
      const parentHandle = args.length > 1 ? popInt(args[1]) : 0;
      const parent = parentHandle === 0 ? this.screen : this.objectFromHandle(parentHandle);
      if (!parent || !CREATABLE.includes(type)) return failed();
      if (!this.handles.includes(null)) return failed();
      return this.allocate(this.make(type, parent));
    });
    define('GFX_DELETE', (args) => {
      const target = object(arity(args, 1)[0]);
      if (target === this.screen) return failed();
      this.release(target);
      this.remove(target);
      return undefined;
    });
    define('GFX_PARENT', (args) => this.handleFromObject(object(arity(args, 1)[0]).parent), false);
    define('GFX_SET_POS', (args) => {
      const [handle, x, y] = arity(args, 3);
      const target = object(handle);
      target.x = popInt(x);
      target.y = popInt(y);
      target.align = 'TOP_LEFT';
      return undefined;
    });
    define('GFX_SET_SIZE', (args) => {
      const [handle, w, h] = arity(args, 3);
      const target = object(handle);
      const width = popInt(w);
      const height = popInt(h);
      if (width < 0 || height < 0) return failed();
      target.w = width;
      target.h = height;
      return undefined;
    });
    define('GFX_X', (args) => this.relativeRect(object(arity(args, 1)[0])).x, false);
    define('GFX_Y', (args) => this.relativeRect(object(arity(args, 1)[0])).y, false);
    define('GFX_WIDTH_OF', (args) => this.relativeRect(object(arity(args, 1)[0])).w, false);
    define('GFX_HEIGHT_OF', (args) => this.relativeRect(object(arity(args, 1)[0])).h, false);
    define('GFX_ALIGN', (args) => {
      between(args, 2, 4);
      const target = object(args[0]);
      const mode = text(args[1]).toUpperCase() as Alignment;
      const dx = args.length > 2 ? popInt(args[2]) : 0;
      const dy = args.length > 3 ? popInt(args[3]) : 0;
      if (!ALIGNMENTS.includes(mode)) return failed();
      target.align = mode;
      target.x = dx;
      target.y = dy;
      return undefined;
    });
    define('GFX_SHOW', (args) => void (object(arity(args, 1)[0]).hidden = false));
    define('GFX_HIDE', (args) => void (object(arity(args, 1)[0]).hidden = true));
    define('GFX_ENABLED', (args) => {
      const [handle, flag] = arity(args, 2);
      const target = object(handle);
      const enabled = popInt(flag);
      if (enabled !== 0 && enabled !== 1) return failed();
      target.disabled = enabled === 0;
      return undefined;
    });
    define('GFX_INVALIDATE', (args) => void object(arity(args, 1)[0]));
    define('GFX_TEXT', (args) => {
      between(args, 1, 2);
      const target = object(args[0]);
      let label: GfxObject | undefined = target.type === 'LABEL' ? target : target.children[0];
      if (args.length === 2) {
        const value = text(args[1]);
        if (target.type === 'CHECKBOX') {
          target.text = value;
          return undefined;
        }
        if (!label && target.type === 'BUTTON') {
          // Desktop extension: give the button a centred label like lv_button examples do.
          label = this.make('LABEL', target);
          label.align = 'CENTER';
        }
        if (!label || label.type !== 'LABEL') return failed();
        label.text = value;
        return undefined;
      }
      if (target.type === 'CHECKBOX') return target.text;
      if (!label || label.type !== 'LABEL') return failed();
      return label.text;
    });
    define('GFX_COLOR', (args) => {
      const [r, g, b] = arity(args, 3).map(popInt);
      if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return failed();
      return (r << 16) | (g << 8) | b;
    }, false);
    define('GFX_RGB', (args) => {
      const value = popInt(arity(args, 1)[0]);
      if (value < 0 || value > 0xffffff) return failed();
      return value;
    }, false);
    const setBg = (args: Value[]): undefined => {
      const [handle, color] = arity(args, 2);
      const target = object(handle);
      target.bgColor = popInt(color) & 0xffffff;
      return undefined;
    };
    define('GFX_COLOR_SET', setBg);
    define('GFX_BG_COLOR', setBg);
    define('GFX_OPACITY', (args) => {
      const [handle, value] = arity(args, 2);
      const target = object(handle);
      const opacity = popInt(value);
      if (opacity < 0 || opacity > 255) return failed();
      target.opacity = opacity;
      return undefined;
    });
    define('GFX_RADIUS', (args) => {
      const [handle, value] = arity(args, 2);
      const target = object(handle);
      const radius = popInt(value);
      if (radius < 0) return failed();
      target.radius = radius;
      return undefined;
    });
    define('GFX_VALUE', (args) => {
      between(args, 1, 2);
      const target = object(args[0]);
      const isValued = target.type === 'SLIDER' || target.type === 'BAR' || target.type === 'ARC';
      if (!isValued && target.type !== 'SWITCH') return failed();
      if (args.length === 1) return target.type === 'SWITCH' ? (target.checked ? 1 : 0) : target.value;
      const value = popInt(args[1]);
      if (target.type === 'SWITCH') target.checked = value !== 0;
      else target.value = Math.min(target.max, Math.max(target.min, value));
      return undefined;
    });
    define('GFX_RANGE', (args) => {
      const [handle, low, high] = arity(args, 3);
      const target = object(handle);
      const min = popInt(low);
      const max = popInt(high);
      if (min > max) return failed();
      target.min = min;
      target.max = max;
      target.value = Math.min(max, Math.max(min, target.value));
      return undefined;
    });
    define('GFX_CHECKED', (args) => {
      between(args, 1, 2);
      const target = object(args[0]);
      if (args.length === 1) return target.checked ? 1 : 0;
      const value = popInt(args[1]);
      if (value !== 0 && value !== 1) return failed();
      target.checked = value === 1;
      return undefined;
    });
    define('GFX_OPTIONS', (args) => {
      const [handle, options] = arity(args, 2);
      const target = object(handle);
      const value = text(options);
      if (target.type !== 'DROPDOWN') return failed();
      target.options = value.split('\n');
      target.selected = 0;
      return undefined;
    });
    define('GFX_SELECTED', (args) => {
      between(args, 1, 2);
      const target = object(args[0]);
      if (target.type !== 'DROPDOWN') return failed();
      if (args.length === 1) return target.selected;
      const selected = popInt(args[1]) >>> 0;
      target.selected = selected >= target.options.length ? Math.max(0, target.options.length - 1) : selected;
      return undefined;
    });
    define('GFX_FONT', (args) => {
      const [handle, name] = arity(args, 2);
      const target = object(handle);
      const size = FONTS[text(name).toUpperCase()];
      if (!size) return failed();
      target.font = size;
      return undefined;
    });
    define('GFX_REFRESH', (args) => (arity(args, 0), undefined));
  }
}
