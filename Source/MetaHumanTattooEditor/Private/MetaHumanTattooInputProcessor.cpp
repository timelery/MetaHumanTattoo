#include "MetaHumanTattooInputProcessor.h"

#include "MetaHumanTattooSession.h"

#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"

namespace
{
	template <typename FPredicate>
	TSharedPtr<FMetaHumanTattooSession> FindPlacingSession(FPredicate&& Predicate)
	{
		for (const TWeakPtr<FMetaHumanTattooSession>& Weak : FMetaHumanTattooSession::GetAllSessions())
		{
			const TSharedPtr<FMetaHumanTattooSession> Session = Weak.Pin();
			if (Session.IsValid() && Session->IsPlacing() && Predicate(*Session))
			{
				return Session;
			}
		}
		return nullptr;
	}
}

bool FMetaHumanTattooInputProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || MouseEvent.IsAltDown())
	{
		return false;
	}

	const FVector2D Position = MouseEvent.GetScreenSpacePosition();
	const TSharedPtr<FMetaHumanTattooSession> Session = FindPlacingSession([&Position](FMetaHumanTattooSession& Candidate)
	{
		return Candidate.PlaceAtScreenPosition(Position, true);
	});
	if (Session.IsValid())
	{
		DragSession = Session;
		return true;
	}
	return false;
}

bool FMetaHumanTattooInputProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (DragSession.IsValid() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		DragSession.Reset();
		return true;
	}
	return false;
}

bool FMetaHumanTattooInputProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	const TSharedPtr<FMetaHumanTattooSession> Session = DragSession.Pin();
	if (!Session.IsValid())
	{
		return false;
	}
	if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) || !Session->IsPlacing())
	{
		DragSession.Reset();
		return false;
	}

	Session->PlaceAtScreenPosition(MouseEvent.GetScreenSpacePosition(), false);
	return true;
}

bool FMetaHumanTattooInputProcessor::HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& WheelEvent, const FPointerEvent* GestureEvent)
{
	if (GestureEvent || (!WheelEvent.IsControlDown() && !WheelEvent.IsShiftDown()))
	{
		return false;
	}

	const FVector2D Position = WheelEvent.GetScreenSpacePosition();
	const TSharedPtr<FMetaHumanTattooSession> Session = FindPlacingSession([&Position](FMetaHumanTattooSession& Candidate)
	{
		return Candidate.IsOverViewport(Position);
	});
	if (!Session.IsValid())
	{
		return false;
	}

	const float Steps = WheelEvent.GetWheelDelta();
	if (WheelEvent.IsControlDown())
	{
		Session->AdjustSelected(FMath::Pow(1.05f, Steps), 0.0f);
	}
	else
	{
		Session->AdjustSelected(1.0f, Steps * 5.0f);
	}
	return true;
}

bool FMetaHumanTattooInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() != EKeys::Escape)
	{
		return false;
	}

	bool bHandled = false;
	for (const TWeakPtr<FMetaHumanTattooSession>& Weak : FMetaHumanTattooSession::GetAllSessions())
	{
		const TSharedPtr<FMetaHumanTattooSession> Session = Weak.Pin();
		if (Session.IsValid() && Session->IsPlacing())
		{
			Session->SetPlacing(false);
			bHandled = true;
		}
	}
	DragSession.Reset();
	return bHandled;
}
