#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"

class FMetaHumanTattooSession;

/**
 * Turns clicks on the MetaHuman Character editor viewport into tattoo placement while a Tattoos panel has
 * Place on Skin active:
 *   left click or drag on the skin   place / slide the selected tattoo (clicks that miss the character still orbit)
 *   Ctrl + mouse wheel               scale the selected tattoo
 *   Shift + mouse wheel              turn the selected tattoo in 5 degree steps
 *   Esc                              leave Place on Skin
 * Alt + left mouse and the other buttons are left to the viewport's camera controls.
 */
class FMetaHumanTattooInputProcessor : public IInputProcessor
{
public:
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& WheelEvent, const FPointerEvent* GestureEvent) override;
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& KeyEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("MetaHumanTattooPlacement"); }

private:
	TWeakPtr<FMetaHumanTattooSession> DragSession;
};
