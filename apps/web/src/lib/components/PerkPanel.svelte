<script lang="ts">
	// Intel perks on the briefing (spec progression-rewards "Intel perks unlocked by completed
	// missions", spec mission-tutorial "Tutorials introduce the advisor and perks"): a card for each
	// newly unlocked perk, then a toggle per unlocked perk and the requirement of each locked one.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { PERKS, type PerkId } from '#lib/game/perks.ts';
	import { t, tCount } from '#lib/game/i18n.svelte.ts';
	import TutorialArt from './TutorialArt.svelte';

	let { session }: { session: GameSession } = $props();
	const unlocked = $derived(session.perksUnlocked);
	const intro = $derived(session.perksToIntroduce);
	const art = (id: PerkId) => (id === 'calendar' ? 'calendar' : 'wire');
</script>

{#if session.perksAllowed}
	{#each intro as id (id)}
		<section class="intro" data-testid="perk-intro-{id}">
			<TutorialArt art={art(id)} />
			<div>
				<p class="tag info">{t('perk.new')}</p>
				<h3>{t(`perk.intro.${id}.title`)}</h3>
				<p>{t(`perk.intro.${id}.body`)}</p>
			</div>
		</section>
	{/each}
	<section class="perks" data-testid="perks">
		<h3>{t('perk.title')}</h3>
		<ul>
			{#each PERKS as p (p.id)}
				<li>
					{#if unlocked.includes(p.id)}
						<label>
							<input
								type="checkbox"
								data-testid="perk-{p.id}"
								checked={session.perkChoice.includes(p.id)}
								disabled={session.busy}
								onchange={(e) => session.setPerk(p.id, e.currentTarget.checked)}
							/>
							<strong>{t(`perk.${p.id}`)}</strong> — {t(`perk.${p.id}.desc`)}
						</label>
					{:else}
						<span class="muted" data-testid="perk-locked-{p.id}">
							🔒 <strong>{t(`perk.${p.id}`)}</strong> — {tCount('perk.locked', p.missions, { have: session.finishedCount })}
						</span>
					{/if}
				</li>
			{/each}
		</ul>
		<p class="muted small">{t('perk.note')}</p>
	</section>
{:else if session.runCampaign && unlocked.length}
	<p class="muted small" data-testid="perks-campaign-off">{t('perk.campaignOff')}</p>
{/if}

<style>
	.intro {
		display: flex;
		gap: 1rem;
		align-items: center;
		border: 1px solid var(--info);
		padding: 0.6rem;
		margin: 0.8rem 0;
	}
	.intro h3 {
		margin: 0.3rem 0;
	}
	.intro p {
		margin: 0.2rem 0;
	}
	.perks ul {
		list-style: none;
		padding: 0;
		margin: 0.3rem 0;
	}
	.perks li {
		padding: 0.15rem 0;
	}
	.small {
		font-size: 0.8rem;
	}
	@media (max-width: 600px) {
		.intro {
			flex-direction: column;
		}
	}
</style>
