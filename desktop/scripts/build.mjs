// Bundles the Electron main/preload/renderer entry points (and optionally the tests) with esbuild.
import { build } from 'esbuild';
import { cpSync, mkdirSync, readdirSync, rmSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const tests = process.argv.includes('--tests');
const common = { bundle: true, sourcemap: true, logLevel: 'warning', loader: { '.bas': 'text' } };

if (tests) {
  rmSync(join(root, 'dist-test'), { recursive: true, force: true });
  const entries = readdirSync(join(root, 'tests')).filter((name) => name.endsWith('.test.ts'));
  await build({
    ...common,
    entryPoints: entries.map((name) => join(root, 'tests', name)),
    outdir: join(root, 'dist-test'),
    platform: 'node',
    format: 'cjs',
    target: 'node20',
  });
} else {
  rmSync(join(root, 'dist'), { recursive: true, force: true });
  mkdirSync(join(root, 'dist'), { recursive: true });
  await build({
    ...common,
    entryPoints: [join(root, 'src/main/main.ts'), join(root, 'src/main/preload.ts')],
    outdir: join(root, 'dist'),
    platform: 'node',
    format: 'cjs',
    target: 'node20',
    external: ['electron'],
  });
  await build({
    ...common,
    entryPoints: {
      console: join(root, 'src/renderer/console.ts'),
      graphics: join(root, 'src/renderer/graphics-window.ts'),
    },
    outdir: join(root, 'dist'),
    platform: 'browser',
    format: 'iife',
    target: 'chrome120',
    loader: { ...common.loader, '.woff2': 'file', '.woff': 'file' },
  });
  cpSync(join(root, 'static'), join(root, 'dist'), { recursive: true });
  cpSync(join(root, 'node_modules/@xterm/xterm/css/xterm.css'), join(root, 'dist/xterm.css'));
}
