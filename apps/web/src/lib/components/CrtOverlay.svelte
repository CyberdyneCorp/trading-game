<!-- Static scanlines + vignette (no flicker). Toggleable; off under reduced motion by default. -->
<div class="crt" aria-hidden="true"></div>

<style>
	.crt {
		pointer-events: none;
		position: fixed;
		inset: 0;
		z-index: 100;
		background:
			repeating-linear-gradient(to bottom, rgba(0, 0, 0, 0) 0, rgba(0, 0, 0, 0) 2px, rgba(0, 0, 0, 0.18) 3px),
			radial-gradient(ellipse at center, rgba(0, 0, 0, 0) 60%, rgba(0, 0, 0, 0.45) 100%);
		mix-blend-mode: multiply;
	}
</style>
