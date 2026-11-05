// The engine build this app version ships (spec web-client "Safe web updates", design D4a).
// Vite emits the Emscripten module as a content-hashed asset, so its path is its build id: a new
// engine gets a new path, and an unfinished run keeps the path it started with.

import engineAsset from './wasm/tradelab.mjs?url';

/** Origin-relative path of this version's engine module (e.g. /_app/immutable/assets/tradelab.H4sh.mjs). */
export function currentEnginePath(base: string = globalThis.location?.href ?? 'http://localhost/'): string {
	return new URL(engineAsset, base).pathname;
}

/** Short label of a build for messages: the content hash in the file name, or the file name. */
export function engineBuildLabel(path: string): string {
	const file = path.split('/').pop() ?? path;
	const m = /^tradelab[.-]([\w-]+)\.mjs$/.exec(file);
	return m ? m[1] : file;
}
