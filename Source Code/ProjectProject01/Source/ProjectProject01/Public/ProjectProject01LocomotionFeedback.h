#pragma once

#include "CoreMinimal.h"

/** One contact-driven vertical dip and small downward nod, not a free-running oscillator. */
struct FProjectProject01LocomotionFeedback
{
	static constexpr float RightFootDepthScale = 1.10f;
	static constexpr float RightFootPitchScale = 0.90f;
	static constexpr float WalkDepth = 1.3f;
	static constexpr float RunDepth = 1.6f;
	static constexpr float WalkPitchDegrees = 0.13f;
	static constexpr float RunPitchDegrees = 0.20f;
	static float FootPitchScale(bool bRightFoot) { return bRightFoot ? RightFootPitchScale : 1.0f; }

	void Contact(bool bRunning, bool bRightFoot)
	{
		// If contacts overlap, continue from the current height instead of snapping to zero.
		StartOffset = Offset;
		StartPitch = Pitch;
		Depth = (bRunning ? RunDepth : WalkDepth) * (bRightFoot ? RightFootDepthScale : 1.0f);
		PitchDepth = (bRunning ? RunPitchDegrees : WalkPitchDegrees) * (bRightFoot ? RightFootDepthScale : 1.0f);
		Duration = bRunning ? 0.26f : 0.32f;
		Elapsed = 0.0f;
		bActive = true;
	}

	void Advance(float DeltaSeconds)
	{
		if (!bActive) return;
		Elapsed += FMath::Max(0.0f, DeltaSeconds);
		if (Elapsed >= Duration) { Reset(); return; }
		const float DipSeconds = Duration * 0.28f;
		if (Elapsed < DipSeconds)
		{
			const float Alpha = FMath::SmoothStep(0.0f, DipSeconds, Elapsed);
			Offset = FMath::Lerp(StartOffset, -Depth, Alpha);
			Pitch = FMath::Lerp(StartPitch, -PitchDepth, Alpha);
		}
		else
		{
			const float Alpha = FMath::SmoothStep(DipSeconds, Duration, Elapsed);
			Offset = FMath::Lerp(-Depth, 0.0f, Alpha);
			Pitch = FMath::Lerp(-PitchDepth, 0.0f, Alpha);
		}
	}

	float GetOffset(float ShakeScale = 1.0f) const { return Offset * FMath::Clamp(ShakeScale, 0.0f, 1.0f); }
	float GetPitchOffset(float ShakeScale = 1.0f) const { return Pitch * FMath::Clamp(ShakeScale, 0.0f, 1.0f); }
	void Reset() { bActive = false; Offset = StartOffset = Pitch = StartPitch = Elapsed = 0.0f; }

private:
	bool bActive = false;
	float Offset = 0.0f, StartOffset = 0.0f, Elapsed = 0.0f, Depth = 0.0f, Duration = 0.32f;
	float Pitch = 0.0f, StartPitch = 0.0f, PitchDepth = 0.0f;
};
