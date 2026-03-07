// Copyright Arcane Engine. AnimInstance implementation.

#include "ArcaneEntityAnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "GameFramework/Actor.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogArcaneAnim, Log, All);

static const TCHAR* RUN_MONTAGE_FALLBACK_PATHS[] = {
	TEXT("/Game/Characters/Mannequins/Anims/Unarmed/AM_Unarmed_Jog.AM_Unarmed_Jog"),
	TEXT("/Game/Characters/Mannequins/Animations/Unarmed/AM_Unarmed_Jog.AM_Unarmed_Jog"),
	TEXT("/Game/Characters/Mannequins/Animations/AM_Unarmed_Jog.AM_Unarmed_Jog"),
	TEXT("/Game/Characters/Characters/Mannequins/Animations/Unarmed/AM_Unarmed_Jog.AM_Unarmed_Jog"),
	TEXT("/Game/Animation/AM_Unarmed_Jog.AM_Unarmed_Jog"),
	nullptr
};

// Fallbacks for run as AnimSequence (template may ship sequence not montage)
static const TCHAR* RUN_SEQUENCE_FALLBACK_PATHS[] = {
	TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Unarmed_Jog.Unarmed_Jog"),
	TEXT("/Game/Characters/Mannequins/Animations/Unarmed/Unarmed_Jog.Unarmed_Jog"),
	TEXT("/Game/Characters/Mannequins/Animations/Unarmed_Jog.Unarmed_Jog"),
	TEXT("/Game/Characters/Mannequins/Animations/Jog_Fwd_Jog_Fwd.Jog_Fwd_Jog_Fwd"),
	TEXT("/Game/Characters/Mannequins/Animations/A_Quinn_Jog.A_Quinn_Jog"),
	nullptr
};

void UArcaneEntityAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	// Drive locomotion from owner (entity character overrides GetVelocity() to return replicated velocity).
	AActor* Owner = GetOwningActor();
	const FVector Vel = Owner ? Owner->GetVelocity() : FVector::ZeroVector;
	Speed = Vel.Size();
	Velocity = Vel;
	bIsFalling = Vel.Z < -1.f;

	// Try to load run montage or sequence (once per instance for fallbacks; retry RunMontagePath when Display sets it).
	auto TryMontage = [this](const FString& Path) -> bool
	{
		UAnimMontage* M = LoadObject<UAnimMontage>(nullptr, *Path);
		if (M) { CachedRunMontage = M; return true; }
		return false;
	};
	auto TrySequenceAsMontage = [this](const FString& Path) -> bool
	{
		UAnimSequenceBase* Seq = LoadObject<UAnimSequenceBase>(nullptr, *Path);
		if (Seq)
		{
			UAnimMontage* DynamicMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Seq, FName(TEXT("DefaultSlot")), 0.25f, 0.25f, 1.f, 0 /* loop */);
			if (DynamicMontage) { CachedRunMontage = DynamicMontage; return true; }
		}
		return false;
	};
	if (!CachedRunMontage && RunMontagePath.IsValid())
	{
		FString Path = RunMontagePath.ToString();
		if (!TryMontage(Path)) { TrySequenceAsMontage(Path); }
		if (CachedRunMontage)
		{
			static bool bLoggedLoad = false;
			if (!bLoggedLoad) { bLoggedLoad = true; UE_LOG(LogArcaneAnim, Log, TEXT("ArcaneEntityAnimInstance: Loaded run animation from Entity Run Animation Path: %s"), *Path); }
		}
	}
	if (!CachedRunMontage && !bRunMontageLoadAttempted)
	{
		bRunMontageLoadAttempted = true;
		for (int i = 0; RUN_MONTAGE_FALLBACK_PATHS[i]; ++i)
		{
			if (TryMontage(RUN_MONTAGE_FALLBACK_PATHS[i])) break;
		}
		for (int i = 0; !CachedRunMontage && RUN_SEQUENCE_FALLBACK_PATHS[i]; ++i)
		{
			TrySequenceAsMontage(RUN_SEQUENCE_FALLBACK_PATHS[i]);
		}
		static bool bLoggedOnce = false;
		if (!CachedRunMontage && !bLoggedOnce)
		{
			bLoggedOnce = true;
			UE_LOG(LogArcaneAnim, Warning, TEXT("ArcaneEntityAnimInstance: Run animation not found. Set 'Entity Run Animation Path' on Arcane Entity Display to a jog/run montage or sequence (Content Browser → Right-click asset → Copy Reference)."));
		}
	}

	if (CachedRunMontage)
	{
		// Play the single run animation when moving (Speed > 10), stop when idle. We don't blend walk/jog/run or direction (forward vs forward-left/right) here.
		const float RunThreshold = 10.f;
		if (Speed > RunThreshold && !Montage_IsPlaying(CachedRunMontage))
		{
			Montage_Play(CachedRunMontage, 1.f, EMontagePlayReturnType::MontageLength, 0.f, true);
			static bool bLoggedPlay = false;
			if (!bLoggedPlay) { bLoggedPlay = true; UE_LOG(LogArcaneAnim, Log, TEXT("ArcaneEntityAnimInstance: Playing run montage (Speed=%.0f). To see it: use an ABP that parents from ArcaneEntityAnimInstance and has a Slot node named 'DefaultSlot' in the AnimGraph connected to Output Pose."), Speed); }
		}
		else if (Speed <= RunThreshold && Montage_IsPlaying(CachedRunMontage))
		{
			Montage_Stop(0.2f, CachedRunMontage);
		}
	}
}
