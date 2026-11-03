import { beforeEach, describe, expect, it } from 'vitest';
import { AudioDirector, FADE_MS, type Player } from './audio.svelte.ts';

class FakePlayer implements Player {
	volume = 1;
	loop = false;
	playing = false;
	onended: (() => void) | null = null;
	constructor(
		public url: string,
		private blocked: () => boolean
	) {}
	play(): Promise<void> {
		if (this.blocked()) return Promise.reject(new Error('NotAllowedError'));
		this.playing = true;
		return Promise.resolve();
	}
	pause(): void {
		this.playing = false;
	}
}

// Node test environment: a minimal in-memory localStorage.
const store = new Map<string, string>();
globalThis.localStorage = {
	getItem: (k: string) => store.get(k) ?? null,
	setItem: (k: string, v: string) => void store.set(k, v),
	removeItem: (k: string) => void store.delete(k),
	clear: () => store.clear(),
	key: () => null,
	length: 0
} as Storage;

let players: FakePlayer[];
let ticks: (() => void)[];
let blocked: boolean;

function director(): AudioDirector {
	return new AudioDirector(
		(url) => {
			const p = new FakePlayer(url, () => blocked);
			players.push(p);
			return p;
		},
		(f) => `/audio/${f}`,
		(_ms, fn) => {
			ticks.push(fn);
			return () => (ticks = ticks.filter((t) => t !== fn));
		}
	);
}

function runFades() {
	for (let i = 0; i < FADE_MS; i++) for (const t of [...ticks]) t();
}

const flush = () => new Promise((r) => setTimeout(r, 0));

beforeEach(() => {
	players = [];
	ticks = [];
	blocked = false;
	localStorage.clear();
});

describe('AudioDirector', () => {
	it('fades a mood track in to the music volume', () => {
		const a = director();
		a.setMood('menu');
		expect(players[0].url).toBe('/audio/menu-1.mp3');
		expect(players[0].volume).toBe(0);
		runFades();
		expect(players[0].volume).toBeCloseTo(a.musicVolume);
		expect(players[0].playing).toBe(true);
	});

	it('crossfades on a mood change and pauses the old track', () => {
		const a = director();
		a.setMood('game');
		runFades();
		a.setMood('tense');
		runFades();
		expect(players.map((p) => p.url)).toEqual(['/audio/game-1.mp3', '/audio/tense-1.mp3']);
		expect(players[0].playing).toBe(false);
		expect(players[1].playing).toBe(true);
	});

	it('does nothing when the mood is unchanged', () => {
		const a = director();
		a.setMood('game');
		a.setMood('game');
		expect(players).toHaveLength(1);
	});

	it('alternates tracks when one ends', () => {
		const a = director();
		a.setMood('game');
		players[0].onended?.();
		expect(players[1].url).toBe('/audio/game-2.mp3');
	});

	it('retries a start the browser blocked once the user interacts', async () => {
		blocked = true;
		const a = director();
		a.setMood('menu');
		await flush();
		expect(players[0].playing).toBe(false);
		blocked = false;
		a.unlock();
		await flush();
		expect(players[0].playing).toBe(true);
	});

	it('mute stops the music and blocks stings; unmute resumes the mood', () => {
		const a = director();
		a.setMood('game');
		runFades();
		a.toggleMute();
		runFades();
		expect(players[0].playing).toBe(false);
		a.sting('win');
		expect(players).toHaveLength(1);
		a.toggleMute();
		expect(players[1].url).toBe('/audio/game-2.mp3');
	});

	it('plays stings at the effects volume', () => {
		const a = director();
		a.setSfxVolume(0.5);
		a.sting('loss');
		expect(players[0].url).toBe('/audio/trade-loss.mp3');
		expect(players[0].volume).toBe(0.5);
	});

	it('persists volumes and mute', () => {
		const a = director();
		a.setMusicVolume(0.2);
		a.setMuted(true);
		const b = director();
		b.load();
		expect(b.musicVolume).toBe(0.2);
		expect(b.muted).toBe(true);
	});
});
