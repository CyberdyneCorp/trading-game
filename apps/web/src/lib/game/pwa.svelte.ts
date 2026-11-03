// Service-worker lifecycle for the UI: update available, online/offline, iOS install hint.
// Updates are applied only on the player's request from the menu (never mid-mission).

class Pwa {
	updateReady = $state(false);
	online = $state(true);
	iosHint = $state(false);
	private waiting: ServiceWorker | null = null;
	private reloading = false;

	init(): void {
		if (typeof window === 'undefined') return;
		this.online = navigator.onLine;
		addEventListener('online', () => (this.online = true));
		addEventListener('offline', () => (this.online = false));
		const ios = /iPad|iPhone|iPod/.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
		const standalone = matchMedia('(display-mode: standalone)').matches || (navigator as unknown as { standalone?: boolean }).standalone === true;
		this.iosHint = ios && !standalone;
		if (!('serviceWorker' in navigator)) return;
		navigator.serviceWorker.addEventListener('controllerchange', () => {
			if (this.reloading) location.reload();
		});
		void navigator.serviceWorker.getRegistration().then((reg) => reg && this.watch(reg));
	}

	private watch(reg: ServiceWorkerRegistration): void {
		const check = (w: ServiceWorker | null) => {
			if (w && navigator.serviceWorker.controller) {
				this.waiting = w;
				this.updateReady = true;
			}
		};
		check(reg.waiting);
		reg.addEventListener('updatefound', () => {
			const w = reg.installing;
			w?.addEventListener('statechange', () => w.state === 'installed' && check(w));
		});
	}

	apply(): void {
		if (!this.waiting) return;
		this.reloading = true;
		this.waiting.postMessage('SKIP_WAITING');
	}
}

export const pwa = new Pwa();
