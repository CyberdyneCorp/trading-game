// Where a glossary term's definition box goes (Term.svelte). The box is fixed to the viewport so the
// scrolling panels it sits in (rail, ticket, option chain) cannot clip it, and it is kept on screen:
// shifted left when the term is near the right edge, and above the term when there is no room below.

export interface TipRect {
	left: number;
	top: number;
	bottom: number;
}

export interface TipPlacement {
	left: number;
	/** Distance from the viewport top (box below the term) ... */
	top?: number;
	/** ... or from the viewport bottom (box above the term). */
	bottom?: number;
	maxWidth: number;
}

export const TIP_GAP = 4;
const MARGIN = 8;
/** Room the box needs below the term before it flips above (a four-line definition plus "more"). */
const ROOM_BELOW = 130;

export function tipPlacement(term: TipRect, vw: number, vh: number): TipPlacement {
	const maxWidth = Math.min(320, vw * 0.8);
	const left = Math.max(MARGIN, Math.min(term.left, vw - maxWidth - MARGIN));
	if (term.bottom + TIP_GAP + ROOM_BELOW <= vh) return { left, top: term.bottom + TIP_GAP, maxWidth };
	return { left, bottom: vh - term.top + TIP_GAP, maxWidth };
}
