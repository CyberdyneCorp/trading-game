import { describe, expect, it } from 'vitest';
import { missingCapabilities, type SupportEnv } from './browserSupport.ts';

const wasm = { validate: () => true };
const subtle = (ok: boolean) => ({
	importKey: async (_f: string, _k: unknown, alg: unknown) => {
		if (!ok || (alg as { name: string }).name !== 'Ed25519') throw new DOMException('Unrecognized name', 'NotSupportedError');
		return {} as CryptoKey;
	}
});

describe('missingCapabilities', () => {
	it('a browser with WebAssembly and Ed25519 is supported', async () => {
		expect(await missingCapabilities({ WebAssembly: wasm, crypto: { subtle: subtle(true) } } as SupportEnv)).toEqual([]);
	});

	it('names WebAssembly when it is missing or cannot validate a module', async () => {
		const crypto = { subtle: subtle(true) } as SupportEnv['crypto'];
		expect(await missingCapabilities({ crypto })).toEqual(['webassembly']);
		const broken = { validate: () => { throw new Error('disabled'); } };
		expect(await missingCapabilities({ WebAssembly: broken, crypto })).toEqual(['webassembly']);
	});

	it('names Ed25519 when WebCrypto refuses the algorithm or is absent', async () => {
		expect(await missingCapabilities({ WebAssembly: wasm, crypto: { subtle: subtle(false) } } as SupportEnv)).toEqual(['ed25519']);
		expect(await missingCapabilities({ WebAssembly: wasm })).toEqual(['ed25519']);
	});

	it('this Node runtime (the test host) has both', async () => {
		expect(await missingCapabilities()).toEqual([]);
	});
});
