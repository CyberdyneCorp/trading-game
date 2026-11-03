<script lang="ts">
	import '@fontsource/vt323';
	import '@fontsource/ibm-plex-mono/400.css';
	import '@fontsource/ibm-plex-mono/600.css';
	import '../app.css';
	import favicon from '#lib/assets/favicon.svg';

	let { children } = $props();
</script>

<svelte:head>
	<link rel="icon" href={favicon} />
	<link rel="manifest" href="manifest.webmanifest" />
	<link rel="apple-touch-icon" href="icons/icon-192.png" />
	<meta name="theme-color" content="#0B1010" />
	<title>TradeLab</title>
	<meta name="description" content="Educational derivatives trading game — virtual money only" />
</svelte:head>

{@render children()}
