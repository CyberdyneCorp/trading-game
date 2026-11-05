// Supported browsers (spec web-client "Supported browsers"): the game needs WebAssembly (the
// engine) and Ed25519 in WebCrypto (pack signatures, per the spec's capability list). A browser
// missing either gets a clear message instead of a game that fails silently.

export type MissingCapability = 'webassembly' | 'ed25519';

/** The smallest valid WebAssembly module: magic number and version 1. */
const EMPTY_MODULE = new Uint8Array([0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]);

/** RFC 8032 test 1 public key: any well-formed 32-byte key works for the import probe. */
const ED25519_PUBLIC = new Uint8Array([
	0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda,
	0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a
]);

export interface SupportEnv {
	WebAssembly?: { validate?: (bytes: BufferSource) => boolean };
	crypto?: { subtle?: Pick<SubtleCrypto, 'importKey'> };
}

function hasWasm(env: SupportEnv): boolean {
	try {
		return typeof env.WebAssembly?.validate === 'function' && env.WebAssembly.validate(EMPTY_MODULE);
	} catch {
		return false;
	}
}

async function hasEd25519(env: SupportEnv): Promise<boolean> {
	const subtle = env.crypto?.subtle;
	if (!subtle?.importKey) return false;
	try {
		await subtle.importKey('raw', ED25519_PUBLIC, { name: 'Ed25519' }, false, ['verify']);
		return true;
	} catch {
		return false;
	}
}

/** The capabilities this browser lacks (empty = supported). */
export async function missingCapabilities(env: SupportEnv = globalThis as SupportEnv): Promise<MissingCapability[]> {
	const missing: MissingCapability[] = [];
	if (!hasWasm(env)) missing.push('webassembly');
	if (!(await hasEd25519(env))) missing.push('ed25519');
	return missing;
}
