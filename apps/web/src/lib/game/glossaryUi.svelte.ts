// The in-place glossary dialog (glossary spec "In-place definitions": "more" opens the glossary at
// that term). One dialog for the whole page, so a term in the terminal, a lesson or a tutorial card
// opens it without leaving the screen; the menu and the Academy open the glossary screen instead.

class GlossaryUi {
	/** The term the dialog shows; null = closed. */
	focus = $state<string | null>(null);
	/** Called when a term is opened (the session records it as seen). */
	onOpen: ((id: string) => void) | null = null;

	get open(): boolean {
		return this.focus !== null;
	}

	openAt(id: string): void {
		this.focus = id;
		this.onOpen?.(id);
	}

	close(): void {
		this.focus = null;
	}
}

export const glossaryUi = new GlossaryUi();

let uid = 0;
/** A page-unique id for a term's description element. */
export const termUid = (): string => `term-${++uid}`;
