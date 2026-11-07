<script lang="ts">
	// A marked glossary term (design D7, glossary spec "In-place definitions"). Hover or keyboard focus
	// shows the short definition next to it; the same text is the control's accessible description
	// (aria-describedby on a hidden element, so it never joins a label's name). On touch, the first tap
	// shows the definition and the second opens the glossary; a click or Enter opens it ("more").
	import type { Snippet } from 'svelte';
	import { termById } from '#lib/content/glossary.ts';
	import { glossaryUi, termUid } from '#lib/game/glossaryUi.svelte.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { tipPlacement, type TipPlacement } from '#lib/game/termTip.ts';

	let { id, children }: { id: string; children: Snippet } = $props();

	const term = $derived(termById(id));
	const desc = termUid();
	let hover = $state(false);
	let focused = $state(false);
	let touched = $state(false);
	const open = $derived(hover || focused || touched);
	let button = $state<HTMLButtonElement>();
	// Fixed to the viewport (scrolling panels would clip it) and kept on screen; placed when it opens.
	const place = $derived<TipPlacement | null>(open && button ? tipPlacement(button.getBoundingClientRect(), innerWidth, innerHeight) : null);
	const px = (v: number | undefined) => (v == null ? 'auto' : `${v}px`);

	function onPointerDown(e: PointerEvent) {
		if (e.pointerType !== 'touch' || touched) return;
		touched = true;
		e.preventDefault(); // the first tap only shows the definition
	}

	function more() {
		touched = false;
		glossaryUi.openAt(id);
	}

	function onKeydown(e: KeyboardEvent) {
		if (e.key === 'Escape' && open) {
			e.stopPropagation();
			hover = focused = touched = false;
		}
	}
</script>

{#if term}
	<!-- No whitespace between the tags: the term sits inside a sentence ("o strike;", "(lançamento)"). -->
	<span class="term-wrap" role="presentation" onmouseenter={() => (hover = true)} onmouseleave={() => (hover = false)}
		><button
			bind:this={button}
			type="button"
			class="term"
			aria-describedby={desc}
			data-term={id}
			onfocus={() => (focused = true)}
			onblur={() => ((focused = false), (touched = false))}
			onpointerdown={onPointerDown}
			onclick={more}
			onkeydown={onKeydown}>{@render children()}</button
		><span id={desc} hidden>{term.text[i18n.locale].short}</span
		>{#if place}<span
				class="term-tip"
				aria-hidden="true"
				data-testid="term-tip"
				style:left={px(place.left)}
				style:top={px(place.top)}
				style:bottom={px(place.bottom)}
				style:max-width={px(place.maxWidth)}
				>{term.text[i18n.locale].short}
				<span class="more">{t('glossary.more')}</span></span
			>{/if}</span
	>
{:else}
	{@render children()}
{/if}

<style>
	.term-wrap {
		position: relative;
		display: inline;
	}
	.term {
		all: unset;
		cursor: help;
		border-bottom: 1px dotted currentColor;
	}
	.term:focus-visible {
		outline: 2px solid var(--accent);
		outline-offset: 1px;
	}
	.term-tip {
		position: fixed;
		z-index: 40;
		width: max-content;
		padding: 0.4rem 0.6rem;
		background: var(--panel, var(--bg));
		color: var(--text);
		border: 1px solid var(--accent);
		font-size: 0.8rem;
		font-weight: normal;
		line-height: 1.35;
		text-align: left;
		text-transform: none;
		white-space: normal;
		pointer-events: none;
	}
	.more {
		display: block;
		margin-top: 0.2rem;
		color: var(--muted);
		font-size: 0.75rem;
	}
</style>
