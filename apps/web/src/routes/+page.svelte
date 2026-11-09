<script lang="ts">
	import { onMount, untrack } from 'svelte';
	import { GameSession } from '#lib/game/session.svelte.ts';
	import { settings } from '#lib/game/settings.svelte.ts';
	import { keymap } from '#lib/game/keymap.svelte.ts';
	import { pwa } from '#lib/game/pwa.svelte.ts';
	import { t } from '#lib/game/i18n.svelte.ts';
	import { sound } from '#lib/game/sound.svelte.ts';
	import { moodFor, StingTracker } from '#lib/game/audioRules.ts';
	import Briefing from '#lib/components/Briefing.svelte';
	import CsvImport from '#lib/components/CsvImport.svelte';
	import Catalog from '#lib/components/Catalog.svelte';
	import CurriculumMap from '#lib/components/CurriculumMap.svelte';
	import LessonRunner from '#lib/components/LessonRunner.svelte';
	import Academy from '#lib/components/Academy.svelte';
	import Glossary from '#lib/components/Glossary.svelte';
	import GlossaryDialog from '#lib/components/GlossaryDialog.svelte';
	import { glossaryUi } from '#lib/game/glossaryUi.svelte.ts';
	import LabScreen from '#lib/components/LabScreen.svelte';
	import Journal from '#lib/components/Journal.svelte';
	import JournalStats from '#lib/components/JournalStats.svelte';
	import ReplayScreen from '#lib/components/ReplayScreen.svelte';
	import Tutorial from '#lib/components/Tutorial.svelte';
	import CrtOverlay from '#lib/components/CrtOverlay.svelte';
	import Debrief from '#lib/components/Debrief.svelte';
	import MissionMenu from '#lib/components/MissionMenu.svelte';
	import Terminal from '#lib/components/Terminal.svelte';
	import DrillScreen from '#lib/components/DrillScreen.svelte';
	import DailyScreen from '#lib/components/DailyScreen.svelte';
	import GuessScreen from '#lib/components/GuessScreen.svelte';

	const session = new GameSession();
	const stings = new StingTracker();

	// Music follows the screen and the account's danger; stings follow trades that close.
	$effect(() => sound.setMood(moodFor(session.screen, session.state, untrack(() => sound.mood))));
	$effect(() => {
		const g = session.state;
		if (!g) return;
		const sting = stings.observe(session.currentRunId, g.events);
		if (sting) sound.sting(sting);
	});

	onMount(() => {
		settings.load();
		keymap.load();
		pwa.init();
		// Browsers only allow audio after a user gesture.
		const unlock = () => sound.unlock();
		window.addEventListener('pointerdown', unlock);
		window.addEventListener('keydown', unlock);
		const seed = Number(new URLSearchParams(location.search).get('seed'));
		if (Number.isInteger(seed) && seed > 0) session.fixedSeed = seed;
		glossaryUi.onOpen = (id) => session.markGlossarySeen(id);
		void session.init();
		return () => {
			window.removeEventListener('pointerdown', unlock);
			window.removeEventListener('keydown', unlock);
			session.dispose();
		};
	});
</script>

{#if session.unsupported.length}
	<main class="panel unsupported" role="alert" data-testid="unsupported">
		<h1>{t('unsupported.title')}</h1>
		<p>{t('unsupported.intro')}</p>
		<ul>
			{#each session.unsupported as cap (cap)}<li>{t(`unsupported.${cap}`)}</li>{/each}
		</ul>
		<p>{t('unsupported.advice')}</p>
	</main>
{:else if session.error}
	<p class="panel down" role="alert">{t('state.error', { msg: session.error })}</p>
{:else if session.screen === 'loading'}
	<p class="loading">{t('state.loading')}</p>
{:else if session.screen === 'menu'}
	<MissionMenu {session} />
{:else if session.screen === 'import'}
	<CsvImport {session} />
{:else if session.screen === 'curriculum'}
	<CurriculumMap {session} />
{:else if session.screen === 'lesson'}
	<LessonRunner {session} />
{:else if session.screen === 'academy'}
	<Academy {session} />
{:else if session.screen === 'glossary'}
	<Glossary {session} />
{:else if session.screen === 'lab'}
	<LabScreen {session} />
{:else if session.screen === 'catalog'}
	<Catalog {session} />
{:else if session.screen === 'journal'}
	<Journal {session} />
{:else if session.screen === 'stats'}
	<JournalStats {session} />
{:else if session.screen === 'drills'}
	<DrillScreen {session} />
{:else if session.screen === 'daily'}
	<DailyScreen {session} />
{:else if session.screen === 'guess'}
	<GuessScreen {session} />
{:else if session.screen === 'replay' && session.viewing && session.state}
	<ReplayScreen {session} />
{:else if session.screen === 'tutorial'}
	<Tutorial {session} />
{:else if session.screen === 'briefing'}
	<Briefing {session} />
{:else if session.screen === 'terminal'}
	<Terminal {session} />
{:else if session.screen === 'debrief' && session.debrief}
	<Debrief {session} />
{/if}

{#if glossaryUi.open}
	<GlossaryDialog seen={session.progress.glossarySeen} />
{/if}

{#if settings.crt}
	<CrtOverlay />
{/if}

<style>
	.unsupported {
		max-width: 40rem;
		margin: 20vh auto 0;
	}
	.loading {
		font-family: var(--font-pixel);
		font-size: 2rem;
		color: var(--accent);
		text-align: center;
		margin-top: 30vh;
	}
</style>
