// Promise-based client for the engine worker. Requests are processed strictly in order.

import type { EngineRequest, EngineResponse, WorkerReply } from './protocol';

export class EngineClient {
	private worker: Worker;
	private nextId = 1;
	private pending = new Map<number, { resolve: (v: unknown) => void; reject: (e: Error) => void }>();

	constructor() {
		this.worker = new Worker(new URL('./engine.worker.ts', import.meta.url), { type: 'module' });
		this.worker.onmessage = (e: MessageEvent<WorkerReply>) => this.onReply(e.data);
	}

	request<R extends EngineRequest>(req: R): Promise<EngineResponse<R['kind']>> {
		const id = this.nextId++;
		return new Promise((resolve, reject) => {
			this.pending.set(id, { resolve: resolve as (v: unknown) => void, reject });
			this.worker.postMessage({ id, req });
		});
	}

	dispose(): void {
		this.worker.terminate();
		for (const p of this.pending.values()) p.reject(new Error('engine disposed'));
		this.pending.clear();
	}

	private onReply(reply: WorkerReply): void {
		const p = this.pending.get(reply.id);
		if (!p) return;
		this.pending.delete(reply.id);
		if (reply.ok) p.resolve(reply.res);
		else p.reject(new Error(reply.error));
	}
}
