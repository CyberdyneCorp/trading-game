// Test aid for `vite preview` only (never part of the built app): simulates a new deploy. After a
// request to `/__e2e-deploy/<id>`, the server answers `/service-worker.js` with the built script plus
// a trailing comment, so browsers checking for an update find a new version; `/__e2e-deploy/off`
// restores the built script. Browsers fetch the worker script without cookies or test headers, so
// the switch is server-wide: the test using it runs alone (Playwright project `deploy`).

import type { Plugin } from 'vite';

interface Req {
	url?: string;
	headers: Record<string, string | string[] | undefined>;
}

interface Res {
	statusCode: number;
	setHeader(name: string, value: string): void;
	end(body: string): void;
}

function send(res: Res, type: string, body: string): void {
	res.statusCode = 200;
	res.setHeader('Content-Type', type);
	res.setHeader('Cache-Control', 'no-store');
	res.end(body);
}

export function previewDeploySimulator(): Plugin {
	let deploy: string | null = null;
	return {
		name: 'tradelab-preview-deploy-simulator',
		configurePreviewServer(server) {
			server.middlewares.use(async (rawReq, rawRes, next) => {
				const req = rawReq as unknown as Req;
				const res = rawRes as unknown as Res;
				const toggle = /^\/__e2e-deploy\/(\w+)$/.exec(req.url ?? '');
				if (toggle) {
					deploy = toggle[1] === 'off' ? null : toggle[1];
					return send(res, 'text/plain', `deploy ${deploy ?? 'off'}`);
				}
				if (!deploy || !req.url?.startsWith('/service-worker.js') || req.headers['x-e2e-built']) return next();
				// The built script, fetched from this same server, plus a comment: a new version.
				const built = await fetch(`http://${req.headers.host}${req.url}`, { headers: { 'x-e2e-built': '1' } }).then((r) => r.text());
				send(res, 'text/javascript', `${built}\n// simulated deploy ${deploy}\n`);
			});
		}
	};
}
