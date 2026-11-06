// The engine C ABI this client speaks (engine.hpp kAbiVersion, tl_abi_version). The worker drives
// only builds with this ABI: a pinned older build whose ABI differs (or that predates
// tl_abi_version, ABI 0) cannot resume its run, and the player is told so instead of the worker
// calling functions the build does not have or with arguments it reads differently.

export const ENGINE_ABI = 1;

/** Prefix of the worker's error for a build with another ABI: `engine_abi:<its abi>`. */
const ABI_ERROR = 'engine_abi:';

/** The ABI a loaded module reports; 0 for builds without tl_abi_version. */
export function moduleAbi(mod: object): number {
	const fn = (mod as { _tl_abi_version?: () => number })._tl_abi_version;
	return typeof fn === 'function' ? fn() >>> 0 : 0;
}

export function abiError(abi: number): Error {
	return new Error(`${ABI_ERROR}${abi}`);
}

/** The other build's ABI when `err` is the worker's ABI refusal, else null. */
export function abiMismatch(err: unknown): number | null {
	const msg = err instanceof Error ? err.message : String(err);
	return msg.startsWith(ABI_ERROR) ? Number(msg.slice(ABI_ERROR.length)) : null;
}
