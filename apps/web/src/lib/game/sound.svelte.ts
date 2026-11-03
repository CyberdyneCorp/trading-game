// The app's soundtrack instance (browser only): HTMLAudioElement players streaming /audio/*.mp3.

import { asset } from '$app/paths';
import { AudioDirector, type Player } from './audio.svelte.ts';

export const sound = new AudioDirector(
	(url): Player => {
		const a = new Audio(url);
		a.preload = 'auto';
		const p: Player = {
			get volume() {
				return a.volume;
			},
			set volume(v: number) {
				a.volume = v;
			},
			loop: false,
			play: () => a.play(),
			pause: () => a.pause(),
			onended: null
		};
		a.addEventListener('ended', () => p.onended?.());
		return p;
	},
	(file) => asset(`audio/${file}` as Parameters<typeof asset>[0])
);
sound.load(); // before the first mood is set, so a muted player never hears a note
