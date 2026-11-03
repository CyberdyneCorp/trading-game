// Soundtrack view-model (spec terminal-ui "Music and sound effects").
//
// One music track plays at a time, from the playlist of the current mood (tracks alternate when
// one ends); changing mood crossfades. Stings are short one-shots on top. Browsers block audio
// until the first user gesture, so a blocked start is retried on unlock(). Volumes and mute are
// per-viewer conveniences kept in localStorage.

import type { Mood, Sting } from './audioRules.ts';

export interface Player {
	volume: number;
	loop: boolean;
	play(): Promise<void>;
	pause(): void;
	onended: (() => void) | null;
}
export type PlayerFactory = (url: string) => Player;
export type Ticker = (ms: number, fn: () => void) => () => void;

export const PLAYLISTS: Record<Mood, string[]> = {
	menu: ['menu-1.mp3', 'menu-2.mp3'],
	game: ['game-1.mp3', 'game-2.mp3'],
	tense: ['tense-1.mp3', 'tense-2.mp3']
};
export const STINGS: Record<Sting, string> = { win: 'trade-win.mp3', loss: 'trade-loss.mp3' };
export const FADE_MS = 1200;
const FADE_STEP_MS = 50;
const KEY = 'tradelab.audio.v1';

interface Playing {
	mood: Mood;
	player: Player;
	stopFade: () => void;
}

const defaultTicker: Ticker = (ms, fn) => {
	const id = setInterval(fn, ms);
	return () => clearInterval(id);
};

export class AudioDirector {
	musicVolume = $state(0.35);
	sfxVolume = $state(0.7);
	muted = $state(false);

	mood: Mood | null = null;
	private current: Playing | null = null;
	private next: Record<Mood, number> = { menu: 0, game: 0, tense: 0 };
	private blocked = false;

	constructor(
		private create: PlayerFactory,
		private url: (file: string) => string,
		private ticker: Ticker = defaultTicker
	) {}

	setMood(mood: Mood): void {
		if (mood === this.mood) return;
		this.mood = mood;
		if (this.muted) return;
		this.fadeOut(this.current);
		this.current = this.start(mood);
	}

	sting(s: Sting): void {
		if (this.muted || this.sfxVolume <= 0) return;
		const p = this.create(this.url(STINGS[s]));
		p.volume = this.sfxVolume;
		p.play().catch(() => undefined);
	}

	/** Call on the first user gesture: retries a start the browser blocked. */
	unlock(): void {
		if (!this.blocked || !this.current) return;
		this.blocked = false;
		this.current.player.play().catch(() => (this.blocked = true));
	}

	toggleMute(): void {
		this.setMuted(!this.muted);
	}

	setMuted(muted: boolean): void {
		this.muted = muted;
		this.save();
		if (muted) {
			this.fadeOut(this.current);
			this.current = null;
		} else if (this.mood) {
			this.current = this.start(this.mood);
		}
	}

	setMusicVolume(v: number): void {
		this.musicVolume = v;
		if (this.current) this.current.player.volume = this.target();
		this.save();
	}

	setSfxVolume(v: number): void {
		this.sfxVolume = v;
		this.save();
	}

	load(): void {
		try {
			const s = JSON.parse(localStorage.getItem(KEY) ?? 'null') as Partial<AudioDirector> | null;
			if (!s) return;
			if (typeof s.musicVolume === 'number') this.musicVolume = s.musicVolume;
			if (typeof s.sfxVolume === 'number') this.sfxVolume = s.sfxVolume;
			if (typeof s.muted === 'boolean') this.muted = s.muted;
		} catch {
			// unavailable storage: keep defaults
		}
	}

	private save(): void {
		try {
			localStorage.setItem(KEY, JSON.stringify({ musicVolume: this.musicVolume, sfxVolume: this.sfxVolume, muted: this.muted }));
		} catch {
			// unavailable storage
		}
	}

	private target(): number {
		return this.muted ? 0 : this.musicVolume;
	}

	private start(mood: Mood): Playing {
		const list = PLAYLISTS[mood];
		const file = list[this.next[mood] % list.length];
		this.next[mood] += 1;
		const player = this.create(this.url(file));
		player.volume = 0;
		player.onended = () => {
			if (this.current?.player !== player || !this.mood) return;
			this.current = this.start(this.mood);
		};
		player.play().catch(() => (this.blocked = true));
		const playing: Playing = { mood, player, stopFade: () => undefined };
		playing.stopFade = this.ramp(player, 0, this.target(), () => undefined);
		return playing;
	}

	private fadeOut(p: Playing | null): void {
		if (!p) return;
		p.stopFade();
		this.ramp(p.player, p.player.volume, 0, () => p.player.pause());
	}

	private ramp(player: Player, from: number, to: number, done: () => void): () => void {
		const steps = Math.max(1, Math.round(FADE_MS / FADE_STEP_MS));
		let i = 0;
		const stop = this.ticker(FADE_STEP_MS, () => {
			i += 1;
			player.volume = Math.min(1, Math.max(0, from + ((to - from) * i) / steps));
			if (i >= steps) {
				stop();
				done();
			}
		});
		return stop;
	}
}
