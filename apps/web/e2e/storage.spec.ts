import { expect, test } from '@playwright/test';
import {
	answerIfAsked,
	dumpDb,
	lastState,
	marketOrder,
	patchRow,
	playToEnd,
	recordWorkerMessages,
	requestKinds,
	startFirstMission,
	step,
	turn,
	workerLog
} from './helpers.ts';

// Engine isolation and local persistence (spec web-client "Engine in a worker", "Local persistence";
// tasks 2.1 and 2.2).

test.beforeEach(async ({ page }) => {
	await recordWorkerMessages(page);
});

test('no bar after the current step ever reaches the main thread or the stored log', async ({ page }) => {
	await page.goto('/?seed=4242');
	await startFirstMission(page);
	await marketOrder(page);
	for (let i = 0; i < 6; i++) await step(page);

	// Everything the main thread received and stored up to now, mid-run.
	const mid = await workerLog(page);
	const midState = lastState(mid)!;
	const stored = JSON.stringify(await dumpDb(page));
	expect(requestKinds(mid)).not.toContain('reveal'); // the seeded reveal is asked for only after the end

	await playToEnd(page);
	const end = lastState(await workerLog(page))!;
	expect(end.bars.length).toBeGreaterThan(midState.bars.length + 10);
	const future = end.bars.slice(midState.bars.length);
	// Same history: the visible bars were final when shown.
	expect(end.bars.slice(0, midState.bars.length)).toEqual(midState.bars);

	// A future bar is identified by its open/high/low/close; none appears in a message or in storage.
	const ohlc = (b: string[]) => JSON.stringify(b.slice(0, 4)).slice(0, -1);
	expect(mid.fromWorker.some((m) => m.includes(ohlc(midState.bars.at(-1)!)))).toBe(true); // the probe finds a shown bar
	const leaks = future.filter((b) => mid.fromWorker.some((m) => m.includes(ohlc(b))) || stored.includes(ohlc(b)));
	expect(leaks).toEqual([]);
	// No reply before then carried more bars than the step it was at.
	const longest = Math.max(
		...mid.fromWorker.map((m) => {
			const r = JSON.parse(m) as { res?: { bars?: unknown[]; state?: { bars?: unknown[] } } };
			return (r.res?.state?.bars ?? r.res?.bars ?? []).length;
		})
	);
	expect(longest).toBe(midState.bars.length);
});

test('resume restores the latest checkpoint and replays only the actions after it', async ({ page }) => {
	await page.goto('/?seed=4242');
	await startFirstMission(page);
	await marketOrder(page);
	// More than 20 logged actions (each bar advance and answer is one): a checkpoint is written at 20.
	for (let i = 0; i < 24; i++) await step(page);
	await expect.poll(async () => ((await dumpDb(page)).checkpoints ?? []).length).toBe(1);
	const db = await dumpDb(page);
	const cp = db.checkpoints[0] as { actions: number; digest: string };
	const run = db.runs[0] as { actions: number; digest: string };
	expect(cp.actions).toBe(20);
	const before = await turn(page);

	await page.reload();
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(page.locator('.turn')).toBeVisible();
	expect(await turn(page)).toBe(before);
	const log = await workerLog(page);
	const kinds = requestKinds(log);
	expect(kinds).toContain('restore');
	expect(kinds).not.toContain('start'); // no replay from the first bar
	const tail = kinds.slice(kinds.indexOf('restore') + 1).filter((k) => k !== 'checkpoint');
	expect(tail.length).toBe(run.actions - cp.actions);
	expect(lastState(log)!.digest).toBe(run.digest); // same step, same digest as before the reload
	await expect(page.getByText(/digest mismatch/)).toHaveCount(0);

	// The run goes on from there.
	await step(page);
	expect(await turn(page)).toBe(before + 1);
});

test('regression: a damaged checkpoint falls back to replaying the whole log', async ({ page }) => {
	await page.goto('/?seed=4242');
	await startFirstMission(page);
	for (let i = 0; i < 21; i++) await step(page);
	await expect.poll(async () => ((await dumpDb(page)).checkpoints ?? []).length).toBe(1);
	const db = await dumpDb(page);
	const runId = (db.runs[0] as { runId: string }).runId;
	await patchRow(page, 'checkpoints', runId, { hex: 'deadbeef' });
	const before = await turn(page);

	await page.reload();
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(page.locator('.turn')).toBeVisible();
	expect(await turn(page)).toBe(before);
	const log = await workerLog(page);
	expect(requestKinds(log)).toEqual(expect.arrayContaining(['restore', 'start']));
	expect(lastState(log)!.digest).toBe((db.runs[0] as { digest: string }).digest);
	await answerIfAsked(page);
});
