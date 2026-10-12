/** Graphics window: paints scene snapshots of the emulated 800x480 LVGL display at 1x-4x. */
import fontUrl from '@fontsource/montserrat/files/montserrat-latin-500-normal.woff2';
import { LINE_HEIGHT, type Scene, type SceneNode } from '../core/graphics';
import type { BridgeApi } from '../main/preload';

declare global {
  interface Window {
    basicedge: BridgeApi;
  }
}

const PRIMARY = '#2196f3';
const GREY = '#e0e0e0';
const ARC_WIDTH = 10;
const KEY_ROWS = [
  ['1#', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '⌫'],
  ['ABC', 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '↵'],
  ['_', '-', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '.', ',', ':'],
  ['⌨', '◀', ' ', '▶', '✓'],
];

const api = window.basicedge;
const canvas = document.getElementById('display') as HTMLCanvasElement;
const context = canvas.getContext('2d')!;
let scale = 2;
let scene: Scene | null = null;

const css = (color: number): string => `#${color.toString(16).padStart(6, '0')}`;

function shade(color: number, factor: number): string {
  const channel = (shift: number): number => Math.round(((color >> shift) & 0xff) * factor);
  return `rgb(${channel(16)}, ${channel(8)}, ${channel(0)})`;
}

function roundRect(x: number, y: number, w: number, h: number, radius: number): void {
  context.beginPath();
  context.roundRect(x, y, w, h, Math.max(0, Math.min(radius, w / 2, h / 2)));
}

function fillBox(node: SceneNode, color: string = css(node.bg)): void {
  if (node.bgOpa > 0) {
    context.save();
    context.globalAlpha *= node.bgOpa / 255;
    roundRect(node.x, node.y, node.w, node.h, node.radius);
    context.fillStyle = color;
    context.fill();
    context.restore();
  }
  if (node.border > 0) {
    roundRect(node.x + node.border / 2, node.y + node.border / 2, node.w - node.border, node.h - node.border, node.radius);
    context.lineWidth = node.border;
    context.strokeStyle = css(node.borderColor);
    context.stroke();
  }
}

function setFont(node: SceneNode): number {
  context.font = `500 ${node.font}px Montserrat, sans-serif`;
  context.textBaseline = 'middle';
  context.fillStyle = css(node.textColor);
  return LINE_HEIGHT[node.font] ?? node.font + 2;
}

function text(lines: string[], node: SceneNode, x: number, y: number): void {
  const lineHeight = setFont(node);
  lines.forEach((line, index) => context.fillText(line, x, y + index * lineHeight + lineHeight / 2));
}

function knob(cx: number, cy: number, radius: number, color = PRIMARY): void {
  context.beginPath();
  context.arc(cx, cy, radius, 0, Math.PI * 2);
  context.fillStyle = color;
  context.fill();
}

function ratio(node: SceneNode): number {
  return node.max > node.min ? Math.min(1, Math.max(0, (node.value - node.min) / (node.max - node.min))) : 0;
}

function paintNode(node: SceneNode): void {
  switch (node.type) {
    case 'SCREEN':
    case 'OBJ':
      fillBox(node);
      break;
    case 'LABEL':
      fillBox(node);
      text(node.lines, node, node.x, node.y);
      break;
    case 'BUTTON':
      fillBox(node, node.pressed ? shade(node.bg, 0.7) : css(node.bg));
      break;
    case 'BAR':
    case 'SLIDER': {
      fillBox(node);
      const horizontal = node.w >= node.h;
      const r = ratio(node);
      context.save();
      roundRect(node.x, node.y, node.w, node.h, node.radius);
      context.clip();
      context.fillStyle = PRIMARY;
      if (horizontal) context.fillRect(node.x, node.y, node.w * r, node.h);
      else context.fillRect(node.x, node.y + node.h * (1 - r), node.w, node.h * r);
      context.restore();
      if (node.type === 'SLIDER') {
        const radius = Math.min(node.w, node.h) / 2 + 6;
        if (horizontal) knob(node.x + node.w * r, node.y + node.h / 2, radius);
        else knob(node.x + node.w / 2, node.y + node.h * (1 - r), radius);
      }
      break;
    }
    case 'CHECKBOX': {
      const lineHeight = setFont(node);
      const box = lineHeight;
      roundRect(node.x + 1, node.y + 1, box - 2, box - 2, 4);
      context.lineWidth = 2;
      if (node.checked) {
        context.fillStyle = PRIMARY;
        context.fill();
        context.strokeStyle = '#ffffff';
        context.beginPath();
        context.moveTo(node.x + box * 0.25, node.y + box * 0.5);
        context.lineTo(node.x + box * 0.45, node.y + box * 0.7);
        context.lineTo(node.x + box * 0.78, node.y + box * 0.3);
        context.stroke();
      } else {
        context.fillStyle = '#ffffff';
        context.fill();
        context.strokeStyle = node.pressed ? PRIMARY : '#9e9e9e';
        context.stroke();
      }
      text(node.lines, node, node.x + box + 7, node.y);
      break;
    }
    case 'SWITCH': {
      fillBox(node, node.checked ? PRIMARY : css(node.bg));
      const radius = node.h / 2 - 4;
      knob(node.checked ? node.x + node.w - node.h / 2 : node.x + node.h / 2, node.y + node.h / 2, radius, '#ffffff');
      break;
    }
    case 'DROPDOWN': {
      fillBox(node);
      const lineHeight = setFont(node);
      const top = node.y + (node.h - lineHeight) / 2;
      context.save();
      context.beginPath();
      context.rect(node.x, node.y, node.w - node.pad - 14, node.h);
      context.clip();
      text([node.selectedText], node, node.x + node.pad + node.border, top);
      context.restore();
      const cx = node.x + node.w - node.pad - 6;
      const cy = node.y + node.h / 2;
      context.beginPath();
      context.moveTo(cx - 5, cy - 2);
      context.lineTo(cx, cy + 3);
      context.lineTo(cx + 5, cy - 2);
      context.strokeStyle = css(node.textColor);
      context.lineWidth = 2;
      context.stroke();
      break;
    }
    case 'TEXTAREA': {
      fillBox(node);
      const inset = node.pad + node.border;
      text(node.lines, node, node.x + inset, node.y + inset);
      const lineHeight = LINE_HEIGHT[node.font] ?? node.font + 2;
      const last = node.lines.length ? node.lines[node.lines.length - 1] : '';
      const cursorX = node.x + inset + context.measureText(last).width + 1;
      const cursorY = node.y + inset + Math.max(0, node.lines.length - 1) * lineHeight;
      if (Math.floor(performance.now() / 500) % 2 === 0) {
        context.fillStyle = PRIMARY;
        context.fillRect(cursorX, cursorY, 2, lineHeight);
      }
      break;
    }
    case 'KEYBOARD': {
      fillBox(node);
      const pad = 4;
      const gap = 4;
      const rowHeight = (node.h - 2 * pad - gap * (KEY_ROWS.length - 1)) / KEY_ROWS.length;
      setFont(node);
      context.textAlign = 'center';
      KEY_ROWS.forEach((row, rowIndex) => {
        const weights = row.map((key) => (key === ' ' ? 6 : key.length > 1 ? 2 : 1));
        const total = weights.reduce((sum, weight) => sum + weight, 0);
        const unit = (node.w - 2 * pad - gap * (row.length - 1)) / total;
        let x = node.x + pad;
        const y = node.y + pad + rowIndex * (rowHeight + gap);
        row.forEach((key, index) => {
          const width = unit * weights[index];
          roundRect(x, y, width, rowHeight, 6);
          context.fillStyle = key.length > 1 || '⌫↵⌨◀▶✓'.includes(key) ? '#e0e0e0' : '#ffffff';
          context.fill();
          context.fillStyle = css(node.textColor);
          context.fillText(key, x + width / 2, y + rowHeight / 2);
          x += width + gap;
        });
      });
      context.textAlign = 'start';
      break;
    }
    case 'ARC': {
      fillBox(node);
      const cx = node.x + node.w / 2;
      const cy = node.y + node.h / 2;
      const radius = Math.max(1, Math.min(node.w, node.h) / 2 - ARC_WIDTH / 2);
      const start = (135 * Math.PI) / 180;
      const end = start + (270 * Math.PI) / 180;
      context.lineCap = 'round';
      context.lineWidth = ARC_WIDTH;
      context.strokeStyle = GREY;
      context.beginPath();
      context.arc(cx, cy, radius, start, end);
      context.stroke();
      const angle = start + ratio(node) * (end - start);
      if (ratio(node) > 0) {
        context.strokeStyle = PRIMARY;
        context.beginPath();
        context.arc(cx, cy, radius, start, angle);
        context.stroke();
      }
      context.lineCap = 'butt';
      knob(cx + radius * Math.cos(angle), cy + radius * Math.sin(angle), ARC_WIDTH / 2 + 4);
      break;
    }
    case 'LED': {
      // The firmware never changes LED brightness; LVGL's default is fully on.
      context.save();
      context.shadowColor = css(node.bg);
      context.shadowBlur = Math.min(node.w, node.h) / 2;
      fillBox(node);
      context.restore();
      break;
    }
    case 'IMAGE':
      break;
  }
}

function paint(): void {
  const dpr = window.devicePixelRatio || 1;
  const width = (scene?.width ?? 800) * scale;
  const height = (scene?.height ?? 480) * scale;
  canvas.style.width = `${width}px`;
  canvas.style.height = `${height}px`;
  if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) {
    canvas.width = Math.round(width * dpr);
    canvas.height = Math.round(height * dpr);
  }
  context.setTransform(scale * dpr, 0, 0, scale * dpr, 0, 0);
  context.clearRect(0, 0, width, height);
  if (!scene) return;
  for (const node of scene.nodes) {
    const [x0, y0, x1, y1] = node.clip;
    if (x1 <= x0 || y1 <= y0) continue;
    context.save();
    context.beginPath();
    context.rect(x0, y0, x1 - x0, y1 - y0);
    context.clip();
    context.globalAlpha = (node.opacity / 255) * (node.disabled ? 0.5 : 1);
    paintNode(node);
    context.restore();
  }
}

function display(event: PointerEvent): [number, number] {
  return [Math.floor(event.offsetX / scale), Math.floor(event.offsetY / scale)];
}

canvas.addEventListener('pointerdown', (event) => {
  canvas.setPointerCapture(event.pointerId);
  api.sendPointer('down', ...display(event));
});
canvas.addEventListener('pointermove', (event) => {
  if (event.buttons & 1) api.sendPointer('move', ...display(event));
});
canvas.addEventListener('pointerup', (event) => api.sendPointer('up', ...display(event)));
window.addEventListener('keydown', (event) => {
  // Keys typed while the graphics window has focus reach the BASIC console (INKEY$, Ctrl-C).
  if (event.ctrlKey && event.key.toLowerCase() === 'c') api.sendKey('\x03');
  else if (event.key === 'Enter') api.sendKey('\r');
  else if (event.key === 'Backspace') api.sendKey('\x7f');
  else if (event.key === 'Escape') api.sendKey('\x1b');
  else if (event.key.length === 1 && !event.ctrlKey && !event.metaKey) api.sendKey(event.key);
});

api.onScale((value) => {
  scale = value;
  paint();
});
api.onScene((value) => {
  scene = value as Scene;
  paint();
});

const face = new FontFace('Montserrat', `url(${fontUrl})`, { weight: '500' });
face.load().then(
  (loaded) => {
    document.fonts.add(loaded);
    paint();
  },
  () => paint(),
);
// Keeps the text-area cursor blinking.
setInterval(() => {
  if (scene?.nodes.some((node) => node.type === 'TEXTAREA')) paint();
}, 500);
