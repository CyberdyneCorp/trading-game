import { describe, expect, it } from 'vitest';
import { abiError, abiMismatch, ENGINE_ABI, moduleAbi } from './abi';

describe('engine ABI (tl_abi_version)', () => {
	it('reads the ABI a build exports, and 0 for a build older than tl_abi_version', () => {
		expect(moduleAbi({ _tl_abi_version: () => ENGINE_ABI })).toBe(ENGINE_ABI);
		expect(moduleAbi({ _tl_abi_version: () => 7 })).toBe(7);
		expect(moduleAbi({})).toBe(0);
	});

	it('round-trips the worker refusal through the error message', () => {
		expect(abiMismatch(abiError(0))).toBe(0);
		expect(abiMismatch(new Error(abiError(3).message))).toBe(3);
		expect(abiMismatch(new Error('engine build not set'))).toBeNull();
		expect(abiMismatch('boom')).toBeNull();
	});
});
