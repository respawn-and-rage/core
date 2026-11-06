#include "ERCharacter.h"
#include "ERGrenadeExplosion.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundWave.h"

namespace
{
    constexpr float RecoverySeconds = 1.2f;
    constexpr float RingGain = .25f;
}

void AERCharacter::ClientBlastRinging_Implementation(float HoldSeconds, FVector_NetQuantize Origin,
    TSubclassOf<AERGrenadeExplosion> Effect)
{
    if (!IsLocallyControlled() || Health->bDead || GetNetMode() == NM_DedicatedServer) return;

    const auto* AudioSettings = GetDefault<UAudioSettings>();
    auto* MasterMix = Cast<USoundSubmix>(AudioSettings->MasterSubmix.ResolveObject());
    auto* WorldMix = Cast<USoundSubmix>(AudioSettings->BaseDefaultSubmix.ResolveObject());
    auto* Ring = LoadObject<USoundWave>(nullptr,
        TEXT("/Game/EclipseRecoil/Audio/BlastRinging/S_ER_BlastRinging.S_ER_BlastRinging"),
        nullptr, LOAD_NoWarn);
    if (!MasterMix || !WorldMix || !Ring) return;

    if (BlastRinging) BlastRinging->Stop();
    BlastRinging = UGameplayStatics::SpawnSound2D(this, Ring, RingGain);
    if (!BlastRinging)
    {
        StopBlastRinging();
        return;
    }
    BlastWorldMix = WorldMix;
    BlastWorldMix->SetSubmixOutputVolume(this, 0.f);
    BlastWorldGain = 0.f;

    float BlastSeconds = 0.f;
    if (Effect)
    {
        const auto* Defaults = Effect->GetDefaultObject<AERGrenadeExplosion>();
        UNiagaraSystem* WaterSystem = nullptr;
        FVector WaterSurface = FVector::ZeroVector;
        auto* Blast = AERGrenadeExplosion::SelectSound(GetWorld(), Origin,
            Defaults->ExplosionSound, WaterSystem, WaterSurface);
        if (Blast)
        {
            // The multicast still plays the normal spatial blast on the world mix.
            // Only this injured listener gets a copy routed around the local mute.
            FAudioDevice::FCreateComponentParams Params(GetWorld());
            Params.SetLocation(Origin);
            Params.AttenuationSettings = Defaults->ExplosionAttenuation;
            if (auto* BlastVoice = FAudioDevice::CreateComponent(Blast, Params))
            {
                BlastVoice->SetWorldLocation(Origin);
                BlastVoice->SetVolumeMultiplier(.65f);
                BlastVoice->bAllowSpatialization = Params.ShouldUseAttenuation();
                BlastVoice->bAutoDestroy = true;
                BlastVoice->SetSubmixSend(MasterMix, 1.f);
                BlastVoice->Play();
                BlastVoices.RemoveAll([](const auto& Voice) { return !Voice.IsValid() || !Voice->IsPlaying(); });
                BlastVoices.Add(BlastVoice);
            }
            BlastSeconds = Blast->GetDuration();
        }
    }

    const double Now = GetWorld()->GetTimeSeconds();
    BlastRecoverAt = FMath::Max(BlastRecoverAt, Now + FMath::Max(BlastSeconds, HoldSeconds));
}

void AERCharacter::UpdateBlastRinging()
{
    const double Now = GetWorld()->GetTimeSeconds();
    if (!IsLocallyControlled() || Now >= BlastRecoverAt + RecoverySeconds)
    {
        StopBlastRinging();
        return;
    }
    if (Now < BlastRecoverAt) return;

    const float Recovery = FMath::Clamp(float((Now - BlastRecoverAt) / RecoverySeconds), 0.f, 1.f);
    const float WorldGain = FMath::SmoothStep(0.f, 1.f, Recovery);
    if (WorldGain != BlastWorldGain)
    {
        BlastWorldGain = WorldGain;
        BlastWorldMix->SetSubmixOutputVolume(this, WorldGain);
    }
    BlastRinging->SetVolumeMultiplier(RingGain * (1.f - WorldGain));
}

void AERCharacter::StopBlastRinging()
{
    if (BlastRinging) BlastRinging->Stop();
    for (const auto& Voice : BlastVoices) if (Voice.IsValid()) Voice->Stop();
    BlastVoices.Reset();
    if (BlastWorldMix) BlastWorldMix->SetSubmixOutputVolume(this, 1.f);
    BlastRinging = nullptr;
    BlastWorldMix = nullptr;
    BlastRecoverAt = 0;
    BlastWorldGain = 1.f;
}
