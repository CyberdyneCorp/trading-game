/// <reference types="vitest/config" />
import adapter from '@sveltejs/adapter-static';
import { sveltekit } from '@sveltejs/kit/vite';
import { defineConfig } from 'vite';

export default defineConfig({
	worker: { format: 'es' },
	test: { include: ['src/**/*.test.ts'], environment: 'node' },
	plugins: [
		sveltekit({
			compilerOptions: {
				// Force runes mode for the project, except for libraries. Can be removed in svelte 6.
				runes: ({ filename }) =>
					filename.split(/[/\\]/).includes('node_modules') ? undefined : true
			},

			// Static build for the Phase 1 slice (no server needed). The BFF arrives with
			// add-cyberdyne-identity-and-backend and will switch this to adapter-node.
			adapter: adapter({ fallback: '200.html' })
		})
	]
});
