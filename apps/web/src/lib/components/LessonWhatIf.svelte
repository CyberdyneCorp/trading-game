<script lang="ts">
	// The what-if lab inside a lesson step (design D2 `whatif`): fixed legs on an illustrative market,
	// valued by the engine (tl_option_whatif); only the step's unlocked sliders move.
	import type { WhatIfWidget } from '#lib/content/academy.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { t } from '#lib/game/i18n.svelte.ts';
	import WhatIfLab from './WhatIfLab.svelte';

	let { session, widget }: { session: GameSession; widget: WhatIfWidget } = $props();
</script>

<figure class="widget" data-testid="lesson-whatif">
	<figcaption>
		<span>{t('academy.market', { market: widget.market, interval: t('academy.whatif.caption') })}</span>
		<span class="tag attention">{t('academy.illustrative')}</span>
	</figcaption>
	<WhatIfLab {session} mode="academy" academyLegs={widget.legs} forward={widget.forward} editable={false} unlocked={widget.unlocked} />
</figure>

<style>
	.widget {
		margin: 0.6rem 0;
		border: 1px solid var(--line);
		padding: 0.5rem;
	}
	figcaption {
		display: flex;
		justify-content: space-between;
		font-size: 0.8rem;
		color: var(--muted);
		margin-bottom: 0.4rem;
	}
</style>
