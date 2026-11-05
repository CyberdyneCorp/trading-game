// The engine's ObjectiveCode names (engine/src/c_api.cpp objective_name), read as text.
import ENGINE from '../../../../../engine/src/c_api.cpp?raw';
import { describe, expect, it } from 'vitest';
import { bpPct, OBJECTIVE_CODES, objectiveText, objectiveTexts } from './objectives.ts';
import type { MissionRulesView } from '#lib/engine/types.ts';


describe('mission objectives (engine MissionRules)', () => {
	it('has PT-BR and English text for every engine objective code', () => {
		const block = ENGINE.slice(ENGINE.indexOf('const char* objective_name'), ENGINE.indexOf('const auto i', ENGINE.indexOf('const char* objective_name')));
		const codes = [...block.matchAll(/"([a-z_]+)"/g)].map((m) => m[1]);
		expect(codes.length).toBeGreaterThan(30);
		expect([...OBJECTIVE_CODES].sort()).toEqual([...codes].sort());
		for (const code of codes) {
			for (const locale of ['pt-BR', 'en'] as const) {
				const text = objectiveText({ code: code as (typeof OBJECTIVE_CODES)[number], valueBp: 100 }, locale);
				expect(text).not.toBe(code);
				expect(text).not.toContain('{');
			}
		}
	});

	it('renders the mission number in the locale (decimal comma in PT-BR)', () => {
		expect(objectiveText({ code: 'drawdown_budget', valueBp: 1000 }, 'pt-BR')).toBe('Termine sem drawdown acima de 10%.');
		expect(objectiveText({ code: 'risk_per_idea', valueBp: 150 }, 'pt-BR')).toContain('1,5%');
		expect(objectiveText({ code: 'risk_per_idea', valueBp: 150 }, 'en')).toContain('1.5%');
		expect(bpPct(300, 'pt-BR')).toBe('3%');
		expect(bpPct(1025, 'pt-BR')).toBe('10,25%');
	});

	it('lists the objectives in the engine order', () => {
		const rules = {
			objectives: [
				{ code: 'plan_every_entry', valueBp: 0 },
				{ code: 'liq_distance', valueBp: 1000 }
			]
		} as MissionRulesView;
		expect(objectiveTexts(rules, 'en')).toEqual([
			'Record a hypothesis and an invalidation level before every entry.',
			'Keep the distance to liquidation above 10%.'
		]);
	});
});
