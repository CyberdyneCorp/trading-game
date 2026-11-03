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
	<title>TradeLab</title>
	<meta name="description" content="Educational derivatives trading game — virtual money only" />
</svelte:head>

{@render children()}
