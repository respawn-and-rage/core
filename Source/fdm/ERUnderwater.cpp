#include "ERCharacter.h"
#include "ERMatch.h"
#include "ERPlayerController.h"
#include "ERProceduralMap.h"
#include "ERServerSettings.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundSubmix.h"
#include "SubmixEffects/AudioMixerSubmixEffectEQ.h"

namespace
{
bool IsSubmerged(UWorld* World, const FVector& ViewLocation)
{
    for (TActorIterator<AERProceduralMap> Map(World); Map; ++Map)
    {
        FVector Surface;
        if (Map->IsMapReady() && Map->WaterSurfaceAt(ViewLocation, Surface)) return true;
    }
    return false;
}
}

void AERCharacter::UpdateDrowning()
{
    const auto* Mode = GetWorld()->GetAuthGameMode<AERGameMode>();
    if (!bNetworkReady || Health->bDead || !Mode || !Mode->IsMatchInProgress() ||
        !IsSubmerged(GetWorld(), GetPawnViewLocation()))
    {
        NextDrowningDamageAt = -1;
        return;
    }

    const double Now = GetWorld()->GetTimeSeconds();
    if (NextDrowningDamageAt < 0) NextDrowningDamageAt = Now + 30;
    if (Now < NextDrowningDamageAt) return;

    // Keep the one-second cadence even if a server frame arrives late.
    const double TicksDue = 1 + FMath::FloorToDouble(Now - NextDrowningDamageAt);
    NextDrowningDamageAt += TicksDue;
    FERMessageDamage Facts;
    Facts.Drowning = true;
    const float Damage = float(TicksDue) * .1f * ERServer::Get(GetWorld(), "Health.Max");
    const float Applied = Health->ReceiveDamage(Damage, nullptr, 2.f, Facts);
    if (Applied > 0 && IsPlayerControlled()) ClientDrowningFeedback(Applied, Health->bDead);
}

void AERCharacter::ClientDrowningFeedback_Implementation(float Amount, bool bLethal)
{
    if (IsLocallyControlled() && Amount > 0)
        AddDamageVisual(Amount, FVector::ZeroVector, bLethal);
}

void AERPlayerController::UpdateCameraManager(float DeltaSeconds)
{
    Super::UpdateCameraManager(DeltaSeconds);
    if (!IsLocalController() || !PlayerCameraManager || !FApp::CanEverRender()) return;

    const bool Submerged = IsSubmerged(GetWorld(), PlayerCameraManager->GetCameraLocation());
    SetUnderwaterAudio(Submerged);
    if (!Submerged) return;

    // Grade the scene from the submerged viewpoint, including rays that never hit the surface.
    static FPostProcessSettings Underwater = []
    {
        FPostProcessSettings Settings;
        Settings.bOverride_ColorSaturation = true;
        Settings.ColorSaturation = FVector4(.72, .82, .9, 1);
        Settings.bOverride_ColorContrast = true;
        Settings.ColorContrast = FVector4(.87, .91, .94, 1);
        Settings.bOverride_ColorGain = true;
        Settings.ColorGain = FVector4(.56, .86, .96, 1);
        Settings.bOverride_ColorOffset = true;
        Settings.ColorOffset = FVector4(-.015, .015, .035, 0);
        return Settings;
    }();
    PlayerCameraManager->AddCachedPPBlend(Underwater, 1.f, VTBlendOrder_Override);
}

void AERPlayerController::SetUnderwaterAudio(bool Submerged)
{
    if (Submerged == (UnderwaterMix != nullptr)) return;
    if (!Submerged)
    {
        UAudioMixerBlueprintLibrary::RemoveSubmixEffect(this, UnderwaterMix, UnderwaterEQ);
        UnderwaterMix->SetSubmixOutputVolume(this, 1.f);
        UnderwaterMix = nullptr;
        return;
    }

    UnderwaterMix = Cast<USoundSubmix>(GetDefault<UAudioSettings>()->MasterSubmix.ResolveObject());
    if (!UnderwaterMix) return;
    if (!UnderwaterEQ)
    {
        UnderwaterEQ = NewObject<USubmixEffectSubmixEQPreset>(this);
        FSubmixEffectSubmixEQSettings Settings;
        for (float Frequency : {1800.f, 4100.f, 8800.f, 16000.f})
        {
            auto& Band = Settings.EQBands.AddDefaulted_GetRef();
            Band.bEnabled = true;
            Band.Frequency = Frequency;
            Band.GainDb = -18.f;
        }
        UnderwaterEQ->SetSettings(Settings);
    }
    UAudioMixerBlueprintLibrary::AddSubmixEffect(this, UnderwaterMix, UnderwaterEQ);
    UnderwaterMix->SetSubmixOutputVolume(this, .55f);
}
