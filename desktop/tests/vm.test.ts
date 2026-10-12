import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';
import { setTrace, translate } from '../src/core/translator';
import { Real, Value } from '../src/core/values';
import { MB_FUNC_END, MB_FUNC_OK, Vm } from '../src/core/vm';

const root = join(__dirname, '..');
const asset = (name: string): string => readFileSync(join(root, 'assets', name), 'latin1');

interface Run {
  vm: Vm;
  status: number;
  output: string;
  errors: string[];
  answersUsed: number;
}

function makeVm(answers: string[] = [], sink: { output: string; errors: string[]; used: number; map: number[] }): Vm {
  return new Vm({
    write: (text) => (sink.output += text),
    readLine: async () => {
      if (sink.used >= answers.length) throw new Error('ran out of answers');
      return answers[sink.used++];
    },
    sleep: async () => true,
    takeKey: () => ' ',
    interrupted: () => false,
    reportError: (code, description, row) => sink.errors.push(`${code} line ${sink.map[row - 1] ?? row}: ${description}`),
    yieldNow: () => new Promise((resolve) => setImmediate(resolve)),
    now: () => 42000,
  });
}

async function run(source: string, answers: string[] = []): Promise<Run> {
  const translation = translate(source, true);
  const sink = { output: '', errors: [] as string[], used: 0, map: translation.map };
  const vm = makeVm(answers, sink);
  let status: number;
  try {
    vm.load(translation.text);
    status = await vm.run();
  } catch {
    status = 3;
  }
  if (status === MB_FUNC_END) status = MB_FUNC_OK;
  return { vm, status, output: sink.output, errors: sink.errors, answersUsed: sink.used };
}

async function ok(source: string, answers: string[] = []): Promise<Run> {
  const result = await run(source, answers);
  assert.equal(result.status, MB_FUNC_OK, `status ${result.status}: ${result.errors.join('; ')}\n${result.output}`);
  return result;
}

function number(vm: Vm, name: string): number {
  const value: Value = vm.getVariable(name);
  if (value instanceof Real) return value.v;
  assert.equal(typeof value, 'number');
  return value as number;
}

test('arrays, DEF FN, ON GOTO and INT', async () => {
  const { vm } = await ok(
    '10 DIM A(8,8),B(3)\n20 DEF FNR(R)=INT(RND(R)*7.98+1.01)\n30 A(8,8)=17:X=A(8,8):Y=FNR(1):Z=INT(-1.2)\n' +
      '40 IF 0 THEN X=99:Y=99\n50 ON 2 GOTO 70,80\n60 X=98:GOTO 90\n70 X=97:GOTO 90\n80 X=X+1\n90 END\n',
  );
  assert.equal(number(vm, 'X'), 18);
  assert.equal(number(vm, 'Z'), -2);
});

test('FOR STEP -1, GOSUB, nested IF, ON GOSUB', async () => {
  const { vm } = await ok(
    '10 X=0:FOR I=3 TO 1 STEP -1\n20 GOSUB 100:NEXT I\n30 IF 1 THEN IF 0 THEN X=99 ELSE X=X+1\n' +
      '40 ON 0 GOTO 90,100\n50 ON 2 GOSUB 90,100\n60 END\n90 X=99:RETURN\n100 X=X+I:RETURN\n',
  );
  assert.equal(number(vm, 'X'), 7);
});

test('DEF FN reads globals and arrays', async () => {
  const { vm } = await ok('10 DIM K(3,3)\n20 DEF FND(D)=K(I,1)+S1\n30 I=2:S1=5:K(2,1)=4:X=FND(0)\n40 END\n');
  assert.equal(number(vm, 'X'), 9);
});

test('runtime DIM bounds and array/scalar name sharing', async () => {
  const { vm } = await ok('10 N=3:M=2:V=7\n20 X=1:DIM W(N,M),V(INT(N/2)+1,M):Y=2\n30 W(N,M)=5:V(2,M)=V:X=W(N,M)+V(2,2)+X+Y\n40 END\n');
  assert.equal(number(vm, 'X'), 15);
  assert.equal(number(vm, 'V'), 7);
});

test('invalid DIM is rejected', async () => {
  assert.throws(() => translate('10 DIM A()\n', true));
  assert.throws(() => translate('10 DIM A(N\n', true));
  assert.notEqual((await run('10 N=-1\n20 DIM A(N)\n30 END\n')).status, MB_FUNC_OK);
  assert.notEqual((await run('10 N=100000\n20 DIM A(N,N)\n30 END\n')).status, MB_FUNC_OK);
});

test('TRACE output matches the firmware', async () => {
  setTrace(true);
  let result: Run;
  try {
    result = await ok(
      '10 REM HELLO: WORLD\n20 X=1:Y=2\n30 IF X=1 THEN 50\n40 PRINT "NO"\n50 FOR I=1 TO 2:NEXT I\n' +
        '60 DEF FNA(Q)=Q+1\n70 PRINT "A";:Z=FNA(1)\n80 IF X=2 THEN Y=3:Y=4\n90 IF X=1 THEN Y=5:GOTO 110\n100 Y=6\n110 END\n',
    );
  } finally {
    setTrace(false);
  }
  assert.equal(number(result.vm, 'Y'), 5);
  assert.equal(number(result.vm, 'Z'), 2);
  const lines = result.output.split('\r\n').filter((line) => line !== '');
  assert.deepEqual(lines, [
    'TRACE 10: REM HELLO: WORLD', 'TRACE 20: X=1', 'TRACE 20: Y=2', 'TRACE 30: IF X=1 THEN 50',
    'TRACE 50: FOR I=1 TO 2', 'TRACE 50: NEXT I', 'TRACE 50: NEXT I', 'TRACE 60: DEF FNA(Q)=Q+1',
    'TRACE 70: PRINT "A";', 'A', 'TRACE 70: Z=FNA(1)', 'TRACE 80: IF X=2 THEN', 'TRACE 90: IF X=1 THEN',
    'TRACE 90: Y=5', 'TRACE 90: GOTO 110', 'TRACE 110: END',
  ]);
});

test('translator rejects tiny buffers and IF without THEN', () => {
  assert.throws(() => translate('10 PRINT "hello"\n', true, 8));
  assert.throws(() => translate('10 IF 1 PRINT 2\n', true));
});

test('GOSUB with MID$ comparison', async () => {
  const { vm } = await ok('10 GOSUB 100:X=Z:END\n100 A$="   ":B$="   ":Z=0\n110 IF MID$(A$,1,3)<>B$ THEN RETURN\n120 Z=1:RETURN\n');
  assert.equal(number(vm, 'X'), 1);
});

test('INPUT parses fields', async () => {
  const { vm, output } = await ok('10 INPUT "COORD";A,B\n20 INPUT C$\n30 X=A+B:S$=MID$(C$,2,3)\n40 END\n', ['1,2', 'hello']);
  assert.equal(number(vm, 'X'), 3);
  assert.equal(vm.getVariable('S$'), 'ell');
  assert.ok(output.startsWith('COORD? '));
});

test('LEFT$/RIGHT$/MID$ clamp', async () => {
  const { vm } = await ok('10 A$=LEFT$("",3):B$=RIGHT$("ABC",20)\n20 C$=LEFT$("ABC",0):D$=MID$("ABC",9,2)\n30 END\n');
  assert.equal(vm.getVariable('B$'), 'ABC');
  assert.equal(vm.getVariable('A$'), '');
  assert.equal(vm.getVariable('C$'), '');
  assert.equal(vm.getVariable('D$'), '');
});

for (const field of ['7,invalid', '7,2,3', '7,1e100']) {
  test(`invalid INPUT "${field}" assigns nothing`, async () => {
    const { vm } = await ok('10 A=1:B=2:END\n');
    const sink = { output: '', errors: [] as string[], used: 0, map: [] };
    const immediate = makeVm([field], sink);
    (immediate as unknown as { globals: Map<string, Value> }).globals = (vm as unknown as { globals: Map<string, Value> }).globals;
    immediate.load(translate('INPUT A,B', false).text);
    assert.notEqual(await immediate.run(), MB_FUNC_OK);
    assert.equal(number(vm, 'A'), 1);
    assert.equal(number(vm, 'B'), 2);
  });
}

test('PRINT formatting and numeric semantics', async () => {
  const { output } = await ok('10 PRINT 1/3;7/2;2^10;-5 MOD 3;10/5\n20 PRINT "A","B";\n30 PRINT 1E10;0.1+0.2;SQR(2)\n40 PRINT STR$(2.5);LEN("ABC");VAL("12");VAL("1.5")\n');
  assert.equal(output, '0.3333333.51024-22\nAB1e+100.31.41421\n2.53121.5\n');
});

test('divide by zero is a warning', async () => {
  const result = await ok('10 X=1/0\n20 PRINT X\n');
  assert.equal(result.output, 'inf\n');
  assert.deepEqual(result.errors, ['49 line 10: Divide by zero']);
});

test('runtime errors report source line numbers', async () => {
  const result = await run('10 X=1\n20 GOSUB 100\n30 RETURN\n100 RETURN\n');
  assert.equal(result.status, 3);
  assert.deepEqual(result.errors, ['29 line 30: No return point']);
});

test('TAB, RND and TIMER', async () => {
  const { output, vm } = await ok('10 PRINT "AB";TAB(5);"C"\n20 R=RND(1):S=RND(-3):T=RND(1)\n30 U=TIMER\n');
  assert.equal(output, 'AB  C\n');
  const r = number(vm, 'R');
  assert.ok(r >= 0 && r < 1);
  assert.ok(Math.abs(number(vm, 'U')) < 1);
});

test('Super Star Trek plays a short game', async () => {
  const source = asset('trek.bas');
  const game = ['N', 'SRS', 'LRS', 'DAM', 'COM', '2', 'COM', '4', '1,1', '2,2', 'SHE', '500', 'NAV', '1', '0.1', 'XXX', 'NO'];
  const result = await ok(source, game);
  assert.equal(result.answersUsed, 17);
  assert.match(result.output, /DISTANCE =1\.41421/);
  assert.match(result.output, /WARP ENGINES/);
  assert.match(result.output, /COMMAND\?/);
  const instructions = await ok(source, ['Y', 'XXX', 'NO']);
  assert.equal(instructions.answersUsed, 3);
  setTrace(true);
  try {
    translate(source, true);
  } finally {
    setTrace(false);
  }
});

test('Amazing draws a 12x9 maze', async () => {
  const result = await ok(asset('amazing.bas'), ['1,1', '12,9']);
  assert.equal(result.answersUsed, 2);
  assert.equal(number(result.vm, 'C'), 12 * 9 + 1);
  const rows = result.output.split('\n');
  assert.equal(rows.filter((row) => /^I[ I]{36}$/.test(row)).length, 9);
  assert.equal(rows.filter((row) => /^(:--|:  ){12}\.$/.test(row)).length, 9);
});
