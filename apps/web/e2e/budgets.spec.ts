import { expect, test } from '@playwright/test';
import { answerIfAsked, buildLongPack, startFirstMission } from './helpers.ts';

// Performance budgets (spec web-client "Performance budgets"; task 1.1), measured on fixed data:
// mission ready under 3 s after pack load, p95 interaction latency under 100 ms, and a chart that
// keeps 60 fps (median redraw within a 16.7 ms frame) while zooming over 5,000 bars. The gzipped-size budget is checked
// on the build output by scripts/check-budgets.mjs.

const quantile = (xs: number[], q: number) => [...xs].sort((a, b) => a - b)[Math.ceil(xs.length * q) - 1];
const p95 = (xs: number[]) => quantile(xs, 0.95);

test('mission ready under 3 s and p95 advance latency under 100 ms', async ({ page }) => {
	await page.goto('/?seed=4242');
	await startFirstMission(page); // first start: the tutorial is shown once
	await page.goto('/?seed=4242');
	const t0 = Date.now();
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.locator('canvas').first()).toBeVisible();
	const ready = Date.now() - t0;
	expect(ready).toBeLessThan(3000);
	await answerIfAsked(page);

	// Space → the bar counter shows the next bar, measured in the page (no test-runner round trip).
	const latencies = await page.evaluate(async () => {
		const out: number[] = [];
		const counter = () => document.querySelector('.turn strong')!.textContent;
		const answer = async () => {
			const btn = [...document.querySelectorAll<HTMLButtonElement>('[role=dialog] button')].find((b) => /Responder/.test(b.textContent ?? ''));
			if (!btn) return;
			btn.click();
			while (document.querySelector('[role=dialog]')) await new Promise((r) => setTimeout(r, 10));
		};
		for (let i = 0; i < 30; i++) {
			await answer();
			const before = counter();
			const t = performance.now();
			window.dispatchEvent(new KeyboardEvent('keydown', { key: ' ', code: 'Space', bubbles: true }));
			while (counter() === before) await new Promise((r) => requestAnimationFrame(r));
			out.push(performance.now() - t);
		}
		return out;
	});
	console.log(`mission ready ${ready} ms; advance p95 ${p95(latencies).toFixed(1)} ms`);
	expect(p95(latencies)).toBeLessThan(100);
});

test('5,000-bar chart: ready under 3 s after pack load, zooming at 60 fps', async ({ page }) => {
	await page.goto('/?seed=7');
	await expect(page.getByRole('button', { name: /Iniciar missão/ }).first()).toBeVisible();
	const pack = await buildLongPack(page);
	await page.getByTestId('pack-file').setInputFiles({ name: 'long.tlpack', mimeType: 'application/octet-stream', buffer: pack });
	await expect(page.getByTestId('pack-message')).toContainText('Pacote instalado');

	const t0 = Date.now();
	await page.getByTestId('pack-start').click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.locator('canvas').first()).toBeVisible();
	const ready = Date.now() - t0;
	expect(ready).toBeLessThan(3000);

	// CPU_THROTTLE=3 slows this machine down to check the headroom left for slower CI runners.
	if (process.env.CPU_THROTTLE) {
		const cdp = await page.context().newCDPSession(page);
		await cdp.send('Emulation.setCPUThrottlingRate', { rate: Number(process.env.CPU_THROTTLE) });
	}
	// Zoom out over every bar, then time each redraw (wheel → effect → canvas draw) at full width.
	const times = await page.evaluate(async () => {
		const canvas = document.querySelector('canvas')!;
		const wheel = (deltaY: number) => canvas.dispatchEvent(new WheelEvent('wheel', { deltaY, bubbles: true, cancelable: true }));
		// A macrotask without setTimeout's 4 ms clamp: Svelte's effect (the redraw) has run by then.
		const flushed = () =>
			new Promise<void>((r) => {
				const ch = new MessageChannel();
				ch.port1.onmessage = () => r();
				ch.port2.postMessage(0);
			});
		for (let i = 0; i < 520; i++) wheel(100);
		await flushed();
		const out: number[] = [];
		for (let i = 0; i < 60; i++) {
			const t = performance.now();
			wheel(i % 2 ? 100 : -100);
			await flushed();
			out.push(performance.now() - t);
		}
		return out;
	});
	const median = quantile(times, 0.5);
	console.log(`5,000-bar pack ready ${ready} ms; zoom redraw median ${median.toFixed(1)} ms, p95 ${p95(times).toFixed(1)} ms`);
	expect(median).toBeLessThan(1000 / 60);
});
