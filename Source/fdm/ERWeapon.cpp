#include "ERWeapon.h"
#include "ERGrenadeBallistics.h"
#include "Sound/SoundBase.h"
#include "ERClaymore.h"
#include "ERShotTrace.h"
#include "ERPostureMesh.h"
#include "Engine/DamageEvents.h"
#include "ERShotEffects.h"
#include "ERMovement.h"
#include "ERWeaponBallistics.h"
#include "ERCharacter.h"
#include "ERMatch.h"
#include "ERPlayerController.h"
#include "ERGrenadeExplosion.h"
#include "ERSmokeCloud.h"
#include "ERBlockRoom.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "ERProceduralMap.h"
#include "Camera/CameraComponent.h"
#include "Components/DecalComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ERPerformanceSettings.h"

namespace
{
void SpawnBulletHole(UWorld* World, UMaterialInterface* Material, const FVector& Position, const FVector& Normal, float Roll)
{
    if (!World || World->GetNetMode() == NM_DedicatedServer || !Material || Normal.IsNearlyZero()) return;
    FRotator Rotation = Normal.Rotation();
    Rotation.Roll = Roll;
    if (auto* Decal = UGameplayStatics::SpawnDecalAtLocation(World, Material, FVector(1.5f, 5.f, 5.f),
        Position + Normal * .15f, Rotation, 20.f))
    {
        // Keep sustained automatic fire bounded on each rendering peer.
        static TArray<TWeakObjectPtr<UDecalComponent>> Holes;
        Holes.RemoveAll([](const auto& Hole) { return !Hole.IsValid(); });
        while (Holes.Num() >= FMath::Max(1, ERPerformance::Get().BulletHoleLimit()))
        {
            if (Holes[0].IsValid()) Holes[0]->DestroyComponent();
            Holes.RemoveAt(0);
        }
        Holes.Add(Decal);
        Decal->SetFadeScreenSize(.0005f);
        Decal->SetFadeOut(15.f, 5.f, false);
    }
}
}

AERWeapon::AERWeapon()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=.05f;
    bReplicates = true; bNetUseOwnerRelevancy = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    ProjectileClass = AERProjectile::StaticClass();
}
const UERWeaponData* AERWeapon::Tuned() const
{
    if(!Data)return nullptr;
    if(TuningSource==Data && TunedData)return TunedData;
    const auto& Rules=ERServer::Rules(GetWorld());
    auto* Self=const_cast<AERWeapon*>(this);
    Self->TuningSource=Data;
    Self->TunedData=DuplicateObject<UERWeaponData>(Data,Self);
    ERServer::TuneWeapon(Self->TunedData,Rules,Data->GetFName());
    return Self->TunedData;
}
void AERWeapon::RefreshTuning() { TunedData=nullptr;TuningSource=nullptr; }
void AERWeapon::OnRep_Data() { RefreshTuning(); }
void AERWeapon::BeginPlay()
{
    Super::BeginPlay();
    if (!Data) Data = NewObject<UERWeaponData>(this);
    if (const auto* Character = Cast<AERCharacter>(GetOwner()))
        UERShotEffects::Prewarm(GetWorld(), ImpactSystem, Character->Camera->GetComponentLocation());
    if (ProjectileClass)
    {
        AERGrenadeExplosion::PrecacheEffect(ProjectileClass->GetDefaultObject<AERProjectile>()->GrenadeExplosionClass);
        if(auto* Character=Cast<AERCharacter>(GetOwner()))
            AERSmokeCloud::Prewarm(GetWorld(),ProjectileClass->GetDefaultObject<AERProjectile>()->SmokeCloudClass,Character->Camera->GetComponentLocation());
    }
    if (HasAuthority())
    {
        MagazineAmmo = Tuned()->MagazineSize; ReserveAmmo = Tuned()->InitialReserve;
        ShotRandom.Initialize(FMath::Rand());
        RecoilRandom.Initialize(FMath::Rand());
        // Initial map spawns can precede world BeginPlay. Apply supplies here,
        // after weapon defaults, so both initial travel and respawns agree.
        const auto* Character = Cast<AERCharacter>(GetOwner());
        const auto* PS = Character ? Character->GetPlayerState<AERPlayerState>() : nullptr;
        if(InitialLoaded>=0)
        {
            // Grenades/mines are carried stacks, not firearm magazines.
            const bool Utility=IsHandGrenade(Tuned()->Payload) || Tuned()->Payload==EERPayload::Claymore;
            MagazineAmmo=Utility?FMath::Max(0,InitialLoaded):FMath::Clamp(InitialLoaded,0,Tuned()->MagazineSize);
            ReserveAmmo=FMath::Max(0,InitialReserve);
        }
        else if (PS)
        {
            const int32 Count = Tuned()->Payload == EERPayload::HE ? PS->HECount : Tuned()->Payload == EERPayload::Smoke ? PS->SmokeCount : Tuned()->Payload == EERPayload::Stun ? PS->StunCount : -1;
            if (Count >= 0) { MagazineAmmo = FMath::Min(Count, Tuned()->MagazineSize); ReserveAmmo = FMath::Max(0, Count - MagazineAmmo); }
            else if (PS->SpareMagazines >= 0 && Tuned()->Payload != EERPayload::Melee)
                ReserveAmmo = Tuned()->MagazineSize * PS->SpareMagazines;
        }
    }
}
float AERWeapon::GetBloomDegrees() const
{
    if (!Data || !GetWorld()) return 0;
    if (Tuned()->bUseWorksheetAccuracy) return ERWeaponAccuracy::Degrees(GetFiringInaccuracy());
    const auto* State = GetWorld()->GetGameState<AERGameState>();
    const double Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    const bool Low=HasOpticalScope() && ScopeLevel && IsPrecisionCrouched();
    const float Delay=Low?FMath::Min(.025f,Tuned()->BloomRecoveryDelay):Tuned()->BloomRecoveryDelay;
    const float RecoveringTime = FMath::Max(0.f, float(Now - LastSpreadShotServerTime) - Delay);
    return FMath::Clamp(BloomAtLastShot - RecoveringTime * Tuned()->BloomRecoveryDegreesPerSecond*(Low?4.f:1.f), 0.f, Tuned()->MaxBloomDegrees);
}
const FERWeaponAccuracyMode& AERWeapon::GetAccuracyMode() const
{ return HasOpticalScope() && ScopeLevel ? Tuned()->ScopedAccuracy : Tuned()->HipAccuracy; }
const FERWeaponRecoilMode& AERWeapon::GetRecoilMode() const
{ return HasOpticalScope() && ScopeLevel ? Tuned()->ScopedRecoil : Tuned()->HipRecoil; }
float AERWeapon::GetAccuracyLowBlend() const
{
    const auto* C=Cast<AERCharacter>(GetOwner());
    const auto* M=C?Cast<UERMovement>(C->GetCharacterMovement()):nullptr;
    return M ? FMath::Clamp((92.5f-M->Locomotion.HalfHeight)/30.f,0.f,1.f) : 0.f;
}
float AERWeapon::GetFiringInaccuracy() const
{
    if(!Data || !Tuned()->bUseWorksheetAccuracy || !GetWorld())return 0;
    const auto* GS=GetWorld()->GetGameState<AERGameState>();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    return ERWeaponAccuracy::Recover(FiringInaccuracyAtLastShot,Now-AccuracyRecoveryServerTime,AccuracyRecoveryTime);
}
float AERWeapon::GetWorksheetInaccuracy() const
{
    const auto* C=Cast<AERCharacter>(GetOwner());
    if(!Data || !C)return 0;
    const auto* M=CastChecked<UERMovement>(C->GetCharacterMovement());
    const auto& P=GetAccuracyMode();
    float Amount=ERWeaponAccuracy::Base(P,GetAccuracyLowBlend(),C->GetVelocity().Size2D(),M->IsOnLadder(),
        M->IsFalling(),C->GetVelocity().Z,M->JumpZVelocity)+GetFiringInaccuracy();
    if(!M->IsFalling() && AccuracyLandingSpeed>0)
    {
        const auto* GS=GetWorld()->GetGameState<AERGameState>();
        const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
        Amount+=ERWeaponAccuracy::Recover(AccuracyLandingImpulse,Now-AccuracyLandingServerTime,AccuracyLandingRecoveryTime);
    }
    return Amount;
}
void AERWeapon::NotifyAccuracyLanding(float DownSpeedCm)
{
    if(!HasAuthority() || !Data || !Tuned()->bUseWorksheetAccuracy)return;
    AccuracyLandingSpeed=FMath::Max(0.f,DownSpeedCm)/2.54f;
    AccuracyLandingImpulse=GetAccuracyMode().Jump+GetAccuracyMode().Land*AccuracyLandingSpeed;
    AccuracyLandingRecoveryTime=ERWeaponAccuracy::Recovery(GetAccuracyMode(),GetAccuracyLowBlend(),0);
    AccuracyLandingServerTime=GetWorld()->GetTimeSeconds();ForceNetUpdate();
}
void AERWeapon::UpdateAccuracyRecovery()
{
    if(!HasAuthority() || !Data || !Tuned()->bUseWorksheetAccuracy)return;
    const double Now=GetWorld()->GetTimeSeconds();
    const float Time=ERWeaponAccuracy::Recovery(GetAccuracyMode(),GetAccuracyLowBlend(),AccuracyBurstShots);
    const float LandingTime=ERWeaponAccuracy::Recovery(GetAccuracyMode(),GetAccuracyLowBlend(),0);
    if(!FMath::IsNearlyEqual(Time,AccuracyRecoveryTime,.00001f))
    {
        // Change slope at the current value; never re-evaluate an old interval
        // using a new stance's coefficient (which can resurrect recovered error).
        FiringInaccuracyAtLastShot=GetFiringInaccuracy();
        AccuracyRecoveryServerTime=Now;AccuracyRecoveryTime=Time;
    }
    if(!FMath::IsNearlyEqual(LandingTime,AccuracyLandingRecoveryTime,.00001f))
    {
        AccuracyLandingImpulse=ERWeaponAccuracy::Recover(AccuracyLandingImpulse,Now-AccuracyLandingServerTime,AccuracyLandingRecoveryTime);
        AccuracyLandingServerTime=Now;AccuracyLandingRecoveryTime=LandingTime;
    }
}
void AERWeapon::ClearScope()
{
    if (!HasAuthority()) return;
    ScopeLevel = 0; ForceNetUpdate();
}
void AERWeapon::ServerClearScope_Implementation() { ClearScope(); }
void AERWeapon::ServerCycleScope_Implementation()
{
    const auto* C = Cast<AERCharacter>(GetOwner());
    if (!CanFight() || !C || C->Weapon != this || bReloading || !Tuned()->ScopeMode ||
        Cast<UERMovement>(C->GetCharacterMovement())->bHighClimb) return;
    ScopeLevel = (ScopeLevel + 1) % (HasOpticalScope() ? 3 : 2);
    ScopeChangedServerTime = GetWorld()->GetTimeSeconds();
    ForceNetUpdate();
}
bool AERWeapon::IsPrecisionCrouched() const
{
    const auto* C=Cast<AERCharacter>(GetOwner());
    const auto* M=C?Cast<UERMovement>(C->GetCharacterMovement()):nullptr;
    return M && !M->IsFalling() && (M->bWantsToCrouch || M->Locomotion.HalfHeight<=68.f || M->IsProne());
}
float AERWeapon::GetScopeRecoilMultiplier() const
{
    return HasOpticalScope() && ScopeLevel && IsPrecisionCrouched() ? .2f : 1.f;
}
void AERWeapon::UpdateScopeMotion()
{
    const auto* C=Cast<AERCharacter>(GetOwner());
    if(!HasAuthority() || !C || !ScopeLevel || !HasOpticalScope() || IsPrecisionCrouched())return;
    if(C->GetVelocity().Size2D()>5.f || C->GetCharacterMovement()->IsFalling())
    {
        const double Now=GetWorld()->GetTimeSeconds();
        if(Now-LastScopeMotionServerTime>=.025)LastScopeMotionServerTime=Now;
    }
}
float AERWeapon::GetScopeBlur() const
{
    if(!ScopeLevel)return 0;
    if(HasOpticalScope() && Tuned()->bUnscopeAfterShot && IsPrecisionCrouched())return GetScopeSettle();
    return FMath::Max(GetScopeSettle(),FMath::Clamp(GetBloomDegrees()/1.5f,0.f,1.f));
}
float AERWeapon::GetScopeSettle() const
{
    if (!Data || !ScopeLevel) return 0;
    const auto* GS = GetWorld()->GetGameState<AERGameState>();
    const double Now = GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    const bool Low=HasOpticalScope() && IsPrecisionCrouched();
    const auto* C=Cast<AERCharacter>(GetOwner());
    const float Speed=C?C->GetVelocity().Size2D():0.f;
    const bool Moving=C && (Speed>5.f || C->GetCharacterMovement()->IsFalling());
    if(HasOpticalScope() && !Low && Moving)
        return Tuned()->bUnscopeAfterShot?1.f:FMath::Clamp(.35f+Speed/277.78f,.35f,1.f);
    const double Start=Low?ScopeChangedServerTime:FMath::Max(ScopeChangedServerTime,LastScopeMotionServerTime);
    const float Duration=Low?.025f:Tuned()->ScopeSettleDuration;
    return FMath::Clamp(1.f - float(Now-Start)/FMath::Max(.01f,Duration),0.f,1.f);
}
float AERWeapon::GetScopeFOV() const
{
    return ScopeLevel && Data ? (HasOpticalScope() ? (ScopeLevel == 1 ? 40.f : 10.f) : Tuned()->ZoomFOV) : 90.f;
}
FVector AERWeapon::GetLaunchVelocity(const FVector& Direction) const
{
    if (!Data) return FVector::ZeroVector;
    // Launcher sights indicate the target; the barrel gives the grenade a small lob.
    FRotator LaunchAim=Direction.Rotation();
    if(Tuned()->Payload==EERPayload::LaunchedHE)
        LaunchAim.Pitch=FMath::Clamp(LaunchAim.Pitch+LauncherLobDegrees,-89.f,89.f);
    FVector Velocity=LaunchAim.Vector()*Tuned()->ProjectileSpeed;
    const float Range=FMath::Clamp(Tuned()->LaunchRangeMultiplier,1.f,2.f) *
        (ScopeLevel ? FMath::Clamp(Tuned()->ScopedLaunchRangeMultiplier,1.f,2.f) : 1.f);
    // Horizontal range bonus preserves the gravity arc's vertical clock and original fuse.
    Velocity.X*=Range;Velocity.Y*=Range;
    return Velocity;
}
float AERWeapon::GetSpreadDegrees() const
{
    const auto* Character = Cast<AERCharacter>(GetOwner());
    if (!Data || !Character) return 0;
    if(Tuned()->bUseWorksheetAccuracy)
        return ERWeaponAccuracy::Degrees(GetAccuracyMode().Spread+GetWorksheetInaccuracy());
    const float Speed = Character->GetVelocity().Size2D();
    const float Run=ERServer::Get(GetWorld(),"Move.Run")/.036f,SprintSpeed=ERServer::Get(GetWorld(),"Move.Sprint")/.036f;
    const float SpeedFraction = FMath::Clamp(Speed / Run, 0.f, 1.f);
    // Extra physical speed beyond the normal run widens every weapon's cone.
    // Apply after the ordinary cap so sprint also penalizes already-inaccurate rifles.
    const float Sprint = ERServer::Get(GetWorld(),"Weapons.SprintSpread") * FMath::GetMappedRangeValueClamped(FVector2D(Run,FMath::Max(Run+1.f,SprintSpeed)),FVector2D(0,1),Speed);
    const auto* Movement = Cast<UERMovement>(Character->GetCharacterMovement());
    const float Ladder = Movement->IsOnLadder() && Tuned()->bOneHanded && Tuned()->Payload==EERPayload::Bullet ?
        Tuned()->LadderMovingSpreadDegrees * FMath::Clamp((FMath::Abs(Character->GetVelocity().Z)-5.f)/(216.f-5.f),0.f,1.f) : 0.f;
    const float LowPosture = FMath::GetMappedRangeValueClamped(FVector2D(62.5f,92.5f), FVector2D(Tuned()->CrouchSpreadMultiplier,1.f), Movement->Locomotion.HalfHeight);
    const float Posture = FMath::Lerp(LowPosture, Tuned()->CrouchSpreadMultiplier * .65f, Movement->Locomotion.ProneBlend);
    const float Air = Character->GetCharacterMovement()->IsFalling() ? Tuned()->AirborneSpreadDegrees : 0.f;
    if (HasOpticalScope())
    {
        const float Base = ScopeLevel ? Tuned()->NoScopeSpreadDegrees * GetScopeSettle() : Tuned()->NoScopeSpreadDegrees;
        const bool Low=ScopeLevel && IsPrecisionCrouched();
        const float LowMotion=Tuned()->ScopedLowMovingSpreadDegrees*SpeedFraction*FMath::Lerp(1.f,.65f,Movement->Locomotion.ProneBlend);
        const float Motion=Low?LowMotion:Tuned()->ScopedRunningSpreadDegrees*SpeedFraction*Posture;
        // Keep the bolt rifle's recovered low-posture burst behavior, with real motion spread.
        const float Bloom=Low && Tuned()->bUnscopeAfterShot?0.f:GetBloomDegrees();
        return FMath::Clamp(Base + Motion + Air + Ladder + Bloom,0.f,10.f) + Sprint;
    }
    return FMath::Clamp((Tuned()->StandingSpreadDegrees + Tuned()->RunningSpreadDegrees * SpeedFraction) * Posture + Air + Ladder + GetBloomDegrees(), 0.f, 10.f) + Sprint;
}
bool AERWeapon::CanFight() const
{
    const auto* Character = Cast<AERCharacter>(GetOwner());
    const auto* Mode = GetWorld()->GetAuthGameMode<AERGameMode>();
    return HasAuthority() && Character && Character->bNetworkReady && !Character->Health->bDead && Character->Weapon == this && !Character->IsWeaponStowed() && Mode && Mode->IsMatchInProgress();
}
bool AERWeapon::IsHandGrenade(EERPayload Kind)
{ return Kind==EERPayload::HE || Kind==EERPayload::Smoke || Kind==EERPayload::Stun; }
bool AERWeapon::IsGrenadeHandHidden() const
{
    if(Data && Tuned()->Payload==EERPayload::Claymore)return MagazineAmmo<=0;
    if(!Data || !IsHandGrenade(Tuned()->Payload)) return false;
    const auto* State=GetWorld()->GetGameState<AERGameState>();
    const double Now=State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    return bCookingGrenade || MagazineAmmo<=0 || Now<GrenadeRecoveryEndServerTime;
}
void AERWeapon::StartFire()
{
    if (!CanFight() || bReloading || bPlacingClaymore || bWantsFire) return;
    if (MagazineAmmo <= 0)
    {
        const double Now=GetWorld()->GetTimeSeconds();
        const bool Firearm=Tuned()->Payload==EERPayload::Bullet || Tuned()->Payload==EERPayload::LaunchedHE || Tuned()->Payload==EERPayload::Rocket;
        if (Firearm && Data->EmptyTriggerSound && Now>=NextEmptyTriggerTime)
        {
            NextEmptyTriggerTime=Now+.4;
            ++AuthorizedEmptyTriggerCount;
            ClientEmptyTrigger(Data->EmptyTriggerSound);
        }
        return;
    }
    if(Tuned()->Payload==EERPayload::Claymore)
    {
        auto* C=CastChecked<AERCharacter>(GetOwner());
        auto* Movement=CastChecked<UERMovement>(C->GetCharacterMovement());
        FHitResult Hit;FRotator Rotation;FString Failure;
        if(!Movement->IsMovingOnGround() || Movement->IsTraversing() || !AERClaymore::Placement(C,Hit,Rotation,&Failure))
        {
            if(auto* PC=Cast<AERPlayerController>(C->GetController()))PC->ClientGameplayMessage(TEXT("claymore_placement"),TEXT("Cannot place Claymore"),Failure.IsEmpty()?TEXT("Stand on a clear floor before placing a Claymore."):Failure,1,3,false);
            return;
        }
        const auto* GS=GetWorld()->GetGameState<AERGameState>();
        ClaymorePlacementStarted=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
        bPlacingClaymore=true;bClaymoreDeposited=false;
        Movement->StopMovementImmediately();Movement->bProneWanted=false;C->Crouch();
        ClearScope();ForceNetUpdate();
        return;
    }
    if(IsHandGrenade(Tuned()->Payload))
    {
        if(GetWorld()->GetTimeSeconds()<NextAllowedShotTime) return;
        bWantsFire=true;ArmGrenade();return;
    }
    bNewAccuracyBurst=true;
    bWantsFire = true; TryFire();
}
double AERWeapon::ClaymorePlacementPhase() const
{ return ClaymorePlacementElapsed()*2.2/ERServer::Get(GetWorld(),"Claymore.Placement"); }
double AERWeapon::ClaymorePlacementElapsed() const
{
    const auto* GS=GetWorld()->GetGameState<AERGameState>();
    return (GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds())-ClaymorePlacementStarted;
}
void AERWeapon::OnRep_PlacingClaymore()
{
    if(!bPlacingClaymore)if(auto* C=Cast<AERCharacter>(GetOwner()))C->UnCrouch();
}
void AERWeapon::EndClaymorePlacement()
{
    if(!bPlacingClaymore)return;
    bPlacingClaymore=false;
    if(auto* C=Cast<AERCharacter>(GetOwner()))C->UnCrouch();
    ForceNetUpdate();
}
void AERWeapon::UpdateClaymorePlacement()
{
    if(!HasAuthority() || !bPlacingClaymore)return;
    auto* C=Cast<AERCharacter>(GetOwner());
    if(!CanFight() || !C || !C->GetCharacterMovement()->IsMovingOnGround()) { EndClaymorePlacement();return; }
    const double Elapsed=ClaymorePlacementPhase();
    if(!bClaymoreDeposited && Elapsed>=1.3)
    {
        bClaymoreDeposited=true;
        if(MagazineAmmo>0 && AERClaymore::Place(C)) { --MagazineAmmo;ForceNetUpdate(); }
    }
    if(Elapsed>=1.6)C->UnCrouch();
    if(Elapsed>=2.2)EndClaymorePlacement();
}
void AERWeapon::ClientEmptyTrigger_Implementation(USoundBase* Sound)
{
    const auto* Character=Cast<AERCharacter>(GetOwner());
    if (!Sound || !Character || !Character->IsPlayerControlled() || !Character->IsLocallyControlled() || Character->Weapon!=this || Character->Health->bDead || Character->IsWeaponStowed()) return;
    UGameplayStatics::PlaySound2D(this,Sound);
    if (FParse::Param(FCommandLine::Get(),TEXT("ERAudioAudit")))
        UE_LOG(LogTemp,Display,TEXT("ER_EMPTY_TRIGGER_AUDIO weapon=%s"),*GetNameSafe(Data));
}
void AERWeapon::StopFire()
{
    bWantsFire=false;GetWorldTimerManager().ClearTimer(FireTimer);
    if(bCookingGrenade) ReleaseGrenade(CanFight());
}
void AERWeapon::ArmGrenade()
{
    auto* Character=Cast<AERCharacter>(GetOwner());
    if(!Character || !ProjectileClass) { bWantsFire=false;return; }
    const FTransform Transform(FRotator::ZeroRotator,AERProjectile::SafeHandPosition(Character,true));
    auto* P=GetWorld()->SpawnActorDeferred<AERProjectile>(ProjectileClass,Transform,this,Character,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!P) { bWantsFire=false;return; }
    P->Payload=Tuned()->Payload;P->ThrownMesh=Tuned()->ProjectileMesh?Tuned()->ProjectileMesh:Tuned()->Mesh;
    P->Damage=Tuned()->Damage;P->ExplosionRadius=Tuned()->ExplosionRadius;P->ExplosionInnerRadius=Tuned()->ExplosionInnerRadius;P->BlockExplosion=Tuned()->BlockExplosion;
    P->BlastArmorRatio=ERWeaponBallistics::GrenadeArmorRatio(Tuned()->ArmorRatio);
    P->FuseDuration=Tuned()->FuseDuration;P->bHeld=true;
    P->Movement->InitialSpeed=0;P->Movement->MaxSpeed=10000.f;P->Movement->Velocity=FVector::ZeroVector;
    CookedGrenade=P;bCookingGrenade=true;--MagazineAmmo;++AuthorizedShotCount;
    const double Now=GetWorld()->GetTimeSeconds();
    NextAllowedShotTime=Now+Tuned()->ShotInterval;Character->Health->ProtectionEndServerTime=0;
    UGameplayStatics::FinishSpawningActor(P,Transform);ForceNetUpdate();Character->ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("ER_GRENADE_ARM kind=%d fuse=%.2f ammo=%d"),int32(P->Payload),P->FuseDuration,MagazineAmmo);
}
void AERWeapon::ReleaseGrenade(bool bThrow)
{
    auto* P=CookedGrenade.Get();
    CookedGrenade=nullptr;bCookingGrenade=false;
    auto* Character=Cast<AERCharacter>(GetOwner());
    if(IsValid(P) && P->bHeld)
    {
        FVector Velocity=FVector::ZeroVector;
        FVector Position=P->GetActorLocation();
        if(Character)
        {
            Position=AERProjectile::SafeHandPosition(Character,bThrow,P);
            if(bThrow)
            {
                // Aim pitch adjusts throw effort; inherit running and jumping momentum afterwards.
                Velocity=ERGrenadeBallistics::Throw(Character->GetEffectiveAimRotation(),
                    Tuned()->ProjectileSpeed,Tuned()->LaunchRangeMultiplier,Character->GetVelocity());
            }
            else Velocity=Character->GetVelocity()*.5f;
        }
        P->ReleaseHeld(Position,Velocity);
        if(bThrow && Character) Character->MulticastShot(Position,Position+Velocity.GetSafeNormal()*250,false,Data,{});
        UE_LOG(LogTemp,Display,TEXT("ER_GRENADE_RELEASE thrown=%d remaining=%.3f position=%s velocity=%s"),bThrow,
            P->FuseEndServerTime-GetWorld()->GetTimeSeconds(),*Position.ToString(),*Velocity.ToString());
    }
    const double Now=GetWorld()->GetTimeSeconds();
    NextAllowedShotTime=FMath::Max(NextAllowedShotTime,Now+.35);GrenadeRecoveryEndServerTime=NextAllowedShotTime;
    ForceNetUpdate();
}
void AERWeapon::OnGrenadeDetonated(AERProjectile* Projectile)
{
    if(CookedGrenade!=Projectile) return;
    CookedGrenade=nullptr;bCookingGrenade=false;
    GrenadeRecoveryEndServerTime=GetWorld()->GetTimeSeconds()+.35;
    NextAllowedShotTime=FMath::Max(NextAllowedShotTime,GrenadeRecoveryEndServerTime);
    ForceNetUpdate();
}
void AERWeapon::TryFire()
{
    if (!bWantsFire || !CanFight() || bReloading || MagazineAmmo <= 0) { StopFire(); return; }
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now < NextAllowedShotTime)
    {
        // Repeating timers can arrive just before the float world-clock deadline.
        // Retry the remaining fraction instead of losing an entire firing interval.
        GetWorldTimerManager().SetTimer(FireTimer, this, &AERWeapon::TryFire, FMath::Max(.001f, float(NextAllowedShotTime-Now)), false);
        return;
    }
    auto* Character = CastChecked<AERCharacter>(GetOwner());
    FVector Eye; FRotator Aim;
    Character->GetActorEyesViewPoint(Eye, Aim);
    Aim=Character->GetEffectiveAimRotation();
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("ERAimPunchAudit")))
    {
        const auto P=Character->GetAimPunchRotation();
        UE_LOG(LogTemp,Display,TEXT("ER_AIMPUNCH_SHOT pawn=%s seq=%u time=%.6f real=%.6f punch_pitch=%.6f punch_yaw=%.6f aim_pitch=%.6f aim_yaw=%.6f"),
            *Character->GetName(),Character->GetAimPunchState().Sequence,Now,FPlatformTime::Seconds(),P.Pitch,P.Yaw,Aim.Pitch,Aim.Yaw);
    }
#endif
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("ERHitLagProbe")))
        for(TActorIterator<AERCharacter> It(GetWorld());It;++It)if(*It!=Character)
        {
            const double QueryTime=ERShotTrace::BulletTime(Character);
            const uint32 AuditShot=Character->FireAcknowledgement.Sequence>0 ? uint32(Character->FireAcknowledgement.Sequence-1):AuthorizedShotCount;
            FVector Historical=It->GetMesh()->GetBoneLocation(TEXT("spine_03"));TArray<FTransform> Pose;
            if(const auto* Posture=Cast<UERPostureMesh>(It->GetMesh()))Posture->ShotHistory.Sample(QueryTime,Pose,&Historical);
            UE_LOG(LogTemp,Display,TEXT("ER_HIT_LAG_SERVER shot=%u target=%s yaw=%.3f"),AuditShot,*It->GetMesh()->GetBoneLocation(TEXT("spine_03")).ToCompactString(),Aim.Yaw);
            UE_LOG(LogTemp,Display,TEXT("ER_HIT_LAG_REWOUND shot=%u target=%s yaw=%.3f rewind_ms=%.1f"),AuditShot,*Historical.ToCompactString(),Aim.Yaw,(Now-QueryTime)*1000);
        }
#endif
    // Camera height is explicit: no animated socket is required for authoritative collision.
    Eye = Character->Camera->GetComponentLocation();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ERRifle), false, Character); Params.AddIgnoredActor(this);
    const double ShotTime=Tuned()->Payload==EERPayload::Bullet ? ERShotTrace::BulletTime(Character):-1;
    TOptional<ERShotTrace::FContext> ShotContext;
    if(Tuned()->Payload==EERPayload::Bullet && Tuned()->bUseBodyHitZones)ShotContext.Emplace(GetWorld(),Params,ShotTime);
    auto* Context=ShotContext.IsSet() ? &ShotContext.GetValue():nullptr;
    FHitResult ViewHit;
    const FVector Far = Eye + Aim.Vector() * FMath::Max(1.f, Tuned()->HitscanRange);
    const bool bViewHit = Tuned()->Payload == EERPayload::Bullet && Tuned()->bUseBodyHitZones ?
        ERShotTrace::Pellet(GetWorld(),ViewHit,Eye,Far,Params,ShotTime,Context) :
        GetWorld()->LineTraceSingleByChannel(ViewHit, Eye, Far, ECC_GameTraceChannel1, Params);
    const FVector Target = bViewHit ? ViewHit.ImpactPoint : Far;
    FVector IntendedMuzzle = Eye + Aim.RotateVector(FVector(45, 12, -12));
    if(Tuned()->Payload==EERPayload::LaunchedHE && Character->GetIronAimBlend()>0)
    {
        // Use the raised rig in the authoritative aim frame, never a client camera
        // cache (remote pawns have no local camera manager view on the server).
        const FVector LocalMuzzle=Character->Camera->GetComponentTransform().InverseTransformPosition(Character->GetVisualMuzzle(true));
        FMinimalViewInfo View;
        View.Location=Eye;View.Rotation=Aim;
        View.FOV=FMath::Lerp(90.f,Tuned()->ZoomFOV,Character->GetIronAimBlend());
        View.FirstPersonFOV=90.f;View.FirstPersonScale=.2f;
        // Keep full projectile depth while matching the viewmodel's screen position.
        const FVector RaisedMuzzle=View.TransformWorldToFirstPerson(Eye+Aim.RotateVector(LocalMuzzle),true);
        IntendedMuzzle=FMath::Lerp(IntendedMuzzle,RaisedMuzzle,Character->GetIronAimBlend());
    }
    LastShotAimDirection = (Target - IntendedMuzzle).GetSafeNormal();
    LastShotSpreadDegrees = GetSpreadDegrees();
    const float ShotInaccuracy=Tuned()->bUseWorksheetAccuracy ? GetWorksheetInaccuracy() : 0.f;
    LastShotDirection = Tuned()->bUseWorksheetAccuracy ?
        ERWeaponAccuracy::Sample(LastShotAimDirection,GetAccuracyMode().Spread,ShotInaccuracy,ShotRandom) :
        ShotRandom.VRandCone(LastShotAimDirection, FMath::DegreesToRadians(LastShotSpreadDegrees));
    FHitResult MuzzleHit;
    const bool bBlocked = GetWorld()->SweepSingleByChannel(MuzzleHit, Eye, IntendedMuzzle, FQuat::Identity, ECC_GameTraceChannel1, FCollisionShape::MakeSphere(3), Params);
    if (Tuned()->Payload != EERPayload::Melee) --MagazineAmmo;
    NextAllowedShotTime = Now + Tuned()->ShotInterval; Character->Health->ProtectionEndServerTime = 0;
    // Only an authorized shot changes burst buildup. Owners recover it against the server clock.
    const float MovingBurst=HasOpticalScope() && ScopeLevel && !IsPrecisionCrouched() ?
        1.f+FMath::Clamp(Character->GetVelocity().Size2D()/277.78f,0.f,1.f)*2.f : 1.f;
    if(Tuned()->bUseWorksheetAccuracy)
    {
        const float Remaining=GetFiringInaccuracy();
        AccuracyBurstShots=bNewAccuracyBurst || Remaining<.0001f ? 1 : FMath::Min(AccuracyBurstShots+1,1000);
        bNewAccuracyBurst=false;
        FiringInaccuracyAtLastShot=Remaining+GetAccuracyMode().Fire;
        AccuracyRecoveryTime=ERWeaponAccuracy::Recovery(GetAccuracyMode(),GetAccuracyLowBlend(),AccuracyBurstShots);
        AccuracyRecoveryServerTime=Now;
    }
    else BloomAtLastShot = FMath::Min(GetBloomDegrees() + Tuned()->BloomPerShotDegrees*GetScopeRecoilMultiplier()*MovingBurst, Tuned()->MaxBloomDegrees);
    LastSpreadShotServerTime = Now;
    if (Tuned()->bUseWorksheetRecoil && Tuned()->Payload==EERPayload::Bullet)
    {
        if (Now-LastRecoilShotTime>ERWeaponRecoil::PatternResetSeconds) RecoilShotIndex=0;
        LastRecoilImpulse=ERWeaponRecoil::Sample(GetRecoilMode(),Tuned()->bRandomRecoilPattern,
            Tuned()->RecoilPatternSeed,RecoilShotIndex++,RecoilRandom);
        LastRecoilShotTime=Now;
        // After sampling this bullet, before clearing an AWP's optical mode.
        // One impulse per shell, not per pellet or cosmetic multicast.
        Character->AddWeaponRecoil(LastRecoilImpulse);
    }
    ForceNetUpdate();
    if (ScopeLevel && Tuned()->bUnscopeAfterShot) ClearScope();
    ++AuthorizedShotCount;
    LastShotTraceCount = 0;
    LastShotPelletHits.Reset();
    if (Tuned()->Payload == EERPayload::Melee)
    {
        // Short, server-selected first-hit melee query; walls still block it.
        FHitResult Hit;
        LastShotTraceStart = Eye;
        LastShotTraceEnd = Eye + Aim.Vector() * FMath::Max(1.f,Tuned()->HitscanRange);
        LastShotTraceCount = 1;
        if (ERShotTrace::Pellet(GetWorld(), Hit, Eye, LastShotTraceEnd, Params))
        {
            LastShotTraceEnd = Hit.ImpactPoint;
            UGameplayStatics::ApplyPointDamage(Hit.GetActor(), Tuned()->Damage, Aim.Vector(), Hit,
                Character->Controller, this, UDamageType::StaticClass());
        }
        Character->MulticastShot(Eye, LastShotTraceEnd, false, Data, {});
    }
    else if (Tuned()->Payload == EERPayload::Bullet)
    {
        const int32 PelletCount = FMath::Clamp(Tuned()->Pellets, 1, 16);
        FVector FeedbackEnd = IntendedMuzzle;
        TArray<FVector_NetQuantize> PelletEnds;
        PelletEnds.Reserve(PelletCount);
        // At contact distance the locomotion capsule may overlap the intended muzzle.
        // Start at the eye and still resolve anatomy instead of assigning a fake chest hit.
        const bool ContactPawn=Tuned()->bUseBodyHitZones && bBlocked && Cast<AERCharacter>(MuzzleHit.GetActor());
        const FVector PelletStart=ContactPawn ? Eye : IntendedMuzzle;
        const bool PelletBlocked=bBlocked && !ContactPawn;
        for (int32 Pellet = 0; Pellet < PelletCount; ++Pellet)
        {
            const FVector Direction = Tuned()->bUseWorksheetAccuracy ? (Pellet==0 ? LastShotDirection :
                ERWeaponAccuracy::Sample(LastShotAimDirection,GetAccuracyMode().Spread,ShotInaccuracy,ShotRandom)) : Tuned()->bUseBodyHitZones && PelletCount > 1 ?
                ERWeaponBallistics::PelletDirection(LastShotDirection,Pellet,PelletCount,Tuned()->PelletSpreadDegrees,ShotRandom) :
                (Pellet == 0 ? LastShotDirection : ShotRandom.VRandCone(LastShotAimDirection, FMath::DegreesToRadians(LastShotSpreadDegrees)));
            FHitResult Hit = MuzzleHit;
            const FVector TraceEnd = PelletStart + Direction * FMath::Max(1.f, Tuned()->HitscanRange);
            const bool bHit = PelletBlocked || (Tuned()->bUseBodyHitZones ? ERShotTrace::Pellet(GetWorld(),Hit,PelletStart,TraceEnd,Params,ShotTime,Context) :
                GetWorld()->LineTraceSingleByChannel(Hit, IntendedMuzzle, TraceEnd, ECC_GameTraceChannel1, Params));
            ++LastShotTraceCount;
            FVector PelletEnd=bHit ? Hit.ImpactPoint : TraceEnd;
            if (Pellet == 0)
            {
                LastShotTraceStart = PelletBlocked ? Eye : PelletStart;
                LastShotTraceEnd = FeedbackEnd = bHit ? Hit.ImpactPoint : TraceEnd;
            }
            LastShotPelletHits.Add(bHit ? Hit : FHitResult());
            if (bHit)
            {
                auto* Room = Cast<AERBlockRoom>(Hit.GetActor());
                if (!bBlocked && Room && Room->CanMutateBlocks())
                {
                    const auto Shot = ERBlockCombat::ContinueShot(GetWorld(), Hit, IntendedMuzzle, Direction, Tuned()->HitscanRange,
                        Tuned()->Damage,Tuned()->BlockShot,Params,Character->Controller,this,Context);
                    LastShotTraceCount += Shot.ExtraTraces;
                    PelletEnd=Shot.End;
                    if (Pellet == 0) LastShotTraceEnd = FeedbackEnd = Shot.End;
                    const auto& Terminal = Shot.TerminalHit;
                    if (Terminal.bBlockingHit && Terminal.GetActor() && !Terminal.GetActor()->IsA<APawn>() && !Terminal.GetActor()->IsA<AERBlockRoom>())
                        MulticastImpact(Terminal.ImpactPoint, Terminal.ImpactNormal, FMath::FRandRange(0.f, 360.f), uint8(FMath::RandRange(0, 3)));
                }
                else
                {
                    // Ordinary maps and blocked muzzles retain their first-hit behaviour.
                    UGameplayStatics::ApplyPointDamage(Hit.GetActor(), Tuned()->Damage, Direction, Hit,
                        Character->Controller, this, UDamageType::StaticClass());
                    if (Hit.GetActor() && !Hit.GetActor()->IsA<APawn>())
                        MulticastImpact(Hit.ImpactPoint, Hit.ImpactNormal, FMath::FRandRange(0.f, 360.f), uint8(FMath::RandRange(0, 3)));
                }
            }
            PelletEnds.Add(FVector_NetQuantize(PelletEnd));
        }
        const bool bTracer = Tuned()->TracerEveryNShots > 0 && (AuthorizedShotCount - 1) % uint32(Tuned()->TracerEveryNShots) == 0;
        Character->MulticastShot(LastShotTraceStart, FeedbackEnd, bTracer, Data, PelletEnds);
    }
    else
    {
        // Rockets and thrown utility retain their physical movement and fuse rules.
        const FVector LaunchPosition = bBlocked ? Eye + Aim.Vector() * 12.f : IntendedMuzzle;
        auto* Projectile = GetWorld()->SpawnActorDeferred<AERProjectile>(ProjectileClass, FTransform(LastShotDirection.Rotation(), LaunchPosition), this, Character, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (Projectile)
        {
            Projectile->Damage = Tuned()->Damage;
            Projectile->Payload = Tuned()->Payload;
            Projectile->ThrownMesh = Tuned()->ProjectileMesh ? Tuned()->ProjectileMesh : (Tuned()->bOneHanded ? Tuned()->Mesh : nullptr);
            Projectile->ExplosionRadius = Tuned()->ExplosionRadius;
            Projectile->ExplosionInnerRadius = Tuned()->ExplosionInnerRadius;
            Projectile->BlockExplosion = Tuned()->BlockExplosion;
            Projectile->BlastArmorRatio = ERWeaponBallistics::GrenadeArmorRatio(Tuned()->ArmorRatio);
            Projectile->FuseDuration = Tuned()->FuseDuration;
            const FVector LaunchVelocity=GetLaunchVelocity(LastShotDirection);
            Projectile->ReleasedFrom=LaunchPosition;
            Projectile->ReleasedVelocity=LaunchVelocity;
            Projectile->Movement->InitialSpeed = LaunchVelocity.Size();
            // Fuse projectiles must not hit different falling-speed caps when zoom changes range.
            Projectile->Movement->MaxSpeed = AERProjectile::UsesFuse(Tuned()->Payload) ? 10000.f : Tuned()->ProjectileSpeed;
            Projectile->Movement->Velocity = LaunchVelocity;
            Projectile->SetLifeSpan(Tuned()->ProjectileLife);
            UGameplayStatics::FinishSpawningActor(Projectile, Projectile->GetActorTransform());
        }
        Character->MulticastShot(IntendedMuzzle, IntendedMuzzle + LastShotDirection * 250, false, Data, {});
    }
    if (MagazineAmmo <= 0) StopFire();
    else if (bWantsFire && Tuned()->bAutomatic) GetWorldTimerManager().SetTimer(FireTimer, this, &AERWeapon::TryFire, Tuned()->ShotInterval, false);
}
void AERWeapon::MulticastImpact_Implementation(FVector_NetQuantize Position, FVector_NetQuantizeNormal Normal, float Roll, uint8 Variant)
{
    for(TActorIterator<AERProceduralMap> It(GetWorld());It;++It)
        if(It->PlayWaterImpactAt(Position))return;
    const AERProjectile* Defaults = ProjectileClass ? ProjectileClass->GetDefaultObject<AERProjectile>() : nullptr;
    UMaterialInterface* Material = ImpactDecalVariants.IsValidIndex(Variant) ? ImpactDecalVariants[Variant].Get() : (Defaults ? Defaults->ImpactDecalMaterial.Get() : nullptr);
    SpawnBulletHole(GetWorld(), Material, Position, Normal, Roll);
    UERShotEffects::SpawnImpact(GetWorld(), ImpactSystem, Position, Normal);
}
void AERWeapon::Reload()
{
    if (!CanFight() || bPlacingClaymore || bReloading || bCookingGrenade || MagazineAmmo >= Tuned()->MagazineSize || Tuned()->Payload == EERPayload::Melee) return;
    // Only authority enters CanFight; labs replenish reserve, retaining reload timing.
    if (!CastChecked<AERCharacter>(GetOwner())->bPresetInventory && GetWorld()->GetMapName().Contains(TEXT("Lab"))) ReserveAmmo = FMath::Max(ReserveAmmo, Tuned()->MagazineSize);
    if (ReserveAmmo <= 0) return;
    StopFire(); ClearScope(); bReloading = true;
    GetWorldTimerManager().SetTimer(ReloadTimer, this, &AERWeapon::FinishReload, Tuned()->ReloadDuration, false);
}
void AERWeapon::FinishReload()
{
    if (CanFight() && bReloading)
    {
        const int32 Transfer = FMath::Min(Tuned()->MagazineSize - MagazineAmmo, ReserveAmmo);
        MagazineAmmo += Transfer; ReserveAmmo -= Transfer;
    }
    bReloading = false;
}
void AERWeapon::Cancel()
{
    EndClaymorePlacement();
    ClearScope();
    bWantsFire=false;GetWorldTimerManager().ClearTimer(FireTimer);
    if(bCookingGrenade) ReleaseGrenade(false);
    GetWorldTimerManager().ClearTimer(ReloadTimer);bReloading=false;
}
void AERWeapon::EndPlay(const EEndPlayReason::Type Reason) { Cancel(); Super::EndPlay(Reason); }
void AERWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AERWeapon,bPlacingClaymore);DOREPLIFETIME(AERWeapon,ClaymorePlacementStarted);
    DOREPLIFETIME_CONDITION(AERWeapon, MagazineAmmo, COND_OwnerOnly); DOREPLIFETIME_CONDITION(AERWeapon, ReserveAmmo, COND_OwnerOnly); DOREPLIFETIME(AERWeapon, bReloading);
    DOREPLIFETIME_CONDITION(AERWeapon, BloomAtLastShot, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AERWeapon, LastSpreadShotServerTime, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AERWeapon, FiringInaccuracyAtLastShot, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AERWeapon, AccuracyBurstShots, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AERWeapon, AccuracyRecoveryServerTime, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AERWeapon, AccuracyRecoveryTime, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AERWeapon, AccuracyLandingServerTime, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AERWeapon, AccuracyLandingSpeed, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AERWeapon, AccuracyLandingImpulse, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AERWeapon, AccuracyLandingRecoveryTime, COND_OwnerOnly);
    DOREPLIFETIME(AERWeapon, Data);
    DOREPLIFETIME(AERWeapon, ScopeLevel);
    DOREPLIFETIME_CONDITION(AERWeapon, ScopeChangedServerTime, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AERWeapon, LastScopeMotionServerTime, COND_OwnerOnly);
    DOREPLIFETIME(AERWeapon,bCookingGrenade);DOREPLIFETIME(AERWeapon,GrenadeRecoveryEndServerTime);
}
AERProjectile::AERProjectile()
{
    bReplicates = true; SetReplicateMovement(true);
    PrimaryActorTick.bCanEverTick = true;
    SetNetUpdateFrequency(30);
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
    SetRootComponent(Collision); Collision->InitSphereRadius(3);
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Collision->SetCollisionObjectType(ECC_GameTraceChannel1);
    Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    Collision->OnComponentHit.AddDynamic(this, &AERProjectile::Hit);
    ShotTarget=CreateDefaultSubobject<USphereComponent>(TEXT("ShotTarget"));
    ShotTarget->SetupAttachment(Collision);ShotTarget->InitSphereRadius(6);
    // Query-only on the shot channel; movement roots ignore this object channel.
    ShotTarget->SetCollisionObjectType(ECC_GameTraceChannel1);
    ShotTarget->SetCollisionResponseToAllChannels(ECR_Ignore);
    ShotTarget->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Block);
    ShotTarget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ShotTarget->SetGenerateOverlapEvents(false);ShotTarget->SetCanEverAffectNavigation(false);
    Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
    Movement->SetUpdatedComponent(Collision); Movement->InitialSpeed = Movement->MaxSpeed = 30000;
    Movement->bInitialVelocityInLocalSpace = false;
    Movement->ProjectileGravityScale = 0; Movement->bShouldBounce = false;
    Movement->bForceSubStepping = true;
    Movement->OnProjectileBounce.AddDynamic(this, &AERProjectile::Bounce);
    Visual = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Visual"));
    Visual->SetupAttachment(Collision); Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Effect = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Effect"));
    Effect->SetupAttachment(Collision); Effect->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Effect->SetCastShadow(false); Effect->SetVisibility(false);
    InitialLifeSpan = 2;
}
bool AERProjectile::UsesFuse(EERPayload Kind)
{
    return Kind == EERPayload::HE || Kind == EERPayload::Smoke || Kind == EERPayload::Stun || Kind == EERPayload::LaunchedHE;
}
void AERProjectile::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    // A delayed projectile outlives the pawn that fired it. Keep the controller
    // so kills still credit and notify both players after that pawn respawns.
    if (HasAuthority()) DamageController = GetInstigatorController();
}
void AERProjectile::BeginPlay()
{
    Super::BeginPlay();
    Visual->SetReceivesDecals(false);Effect->SetReceivesDecals(false);
    // Capture allegiance when armed; death, respawn or team changes cannot recolor it.
    if(HasAuthority() && Payload==EERPayload::Smoke)
        if(const auto* Pawn=GetInstigator())
            if(const auto* State=Pawn->GetPlayerState<AERPlayerState>()) SmokeTeamColor=AERSmokeCloud::ColorForTeam(State->Team);
    PreviousSmokePosition=GetActorLocation();
    Visual->SetSkeletalMeshAsset(ThrownMesh);
    Effect->SetStaticMesh(EffectMesh);
    if (Payload == EERPayload::Rocket)
    {
        Effect->SetMaterial(0, ExplosionMaterial);
        Effect->SetRelativeScale3D(FVector(.08f)); Effect->SetVisibility(true);
    }
    if (UsesFuse(Payload))
    {
        Movement->ProjectileGravityScale = 1;
        Movement->bShouldBounce = true;
        Movement->bRotationFollowsVelocity = false;
        Movement->Friction = .4f;
        Movement->bBounceAngleAffectsFriction = true;
        Movement->BounceVelocityStopSimulatingThreshold = 30;
        if (HasAuthority())
        {
            BounceRandom.Initialize(FMath::Rand());
            Movement->Bounciness = BounceRandom.FRandRange(.18f,.25f);
            Spin = FVector(BounceRandom.FRandRange(-7.f,7.f),BounceRandom.FRandRange(10.f,17.f),BounceRandom.FRandRange(-5.f,5.f));
            FuseDuration = FMath::Max(.05f,FuseDuration);
            FuseEndServerTime=GetWorld()->GetTimeSeconds()+FuseDuration;
            // A weapon's projectile lifetime must never cut its fuse short.
            SetLifeSpan(FMath::Max(GetLifeSpan(),FuseDuration+.5f));
            GetWorldTimerManager().SetTimer(FuseTimer, this, &AERProjectile::Detonate, FuseDuration, false);
        }
    }
    SetActorTickEnabled(HasAuthority() && UsesFuse(Payload));
    Collision->SetCollisionResponseToChannel(ECC_GameTraceChannel1,Payload==EERPayload::Bullet?ECR_Block:ECR_Ignore);
    if (HasAuthority()) { Collision->IgnoreActorWhenMoving(GetInstigator(), true); Collision->IgnoreActorWhenMoving(GetOwner(), true); }
    else { Movement->Deactivate(); Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
    OnRep_Held();
}
FVector AERProjectile::SafeHandPosition(AERCharacter* Character,bool bOverhead,const AActor* Ignore)
{
    const FVector Eye=Character->Camera->GetComponentLocation();
    const FRotator Yaw(0,Character->GetBaseAimRotation().Yaw,0);
    const FVector Wanted=Eye+(bOverhead?Yaw.RotateVector(FVector(-14,24,-4)):FVector(0,0,-35));
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERGrenadeHand),false,Character);
    if(Ignore) Query.AddIgnoredActor(Ignore);
    for(const auto& Item:Character->Inventory) if(Item) Query.AddIgnoredActor(Item);
    FHitResult Hit;
    if(Character->GetWorld()->SweepSingleByChannel(Hit,Eye,Wanted,FQuat::Identity,ECC_GameTraceChannel1,FCollisionShape::MakeSphere(3.f),Query))
        return Hit.bStartPenetrating?Eye:Hit.Location+Hit.ImpactNormal*.5f;
    return Wanted;
}
void AERProjectile::OnRep_Held()
{
    ShotTarget->SetSphereRadius(bHeld?12.f:6.f);
    ShotTarget->SetCollisionEnabled(UsesFuse(Payload) && !bImpacted ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    Visual->SetVisibility(!bHeld && !bImpacted);
    if(bHeld) { Movement->Deactivate();Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
}
void AERProjectile::ReleaseHeld(const FVector& Position,const FVector& Velocity)
{
    if(!HasAuthority() || !bHeld || bImpacted) return;
    bHeld=false;ReleasedFrom=Position;ReleasedVelocity=Velocity;
    ShotTarget->SetSphereRadius(6.f);
    SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Movement->SetUpdatedComponent(Collision);Movement->InitialSpeed=0;
    // A dropped grenade starts at zero speed but must still accelerate under
    // gravity; do not cap its subsequent motion to the release speed.
    Movement->MaxSpeed=FMath::Max(Movement->MaxSpeed,Velocity.Size());Movement->Velocity=Velocity;
    Movement->Activate(true);Movement->UpdateComponentVelocity();
    Visual->SetVisibility(true);ForceNetUpdate();
}
void AERProjectile::Bounce(const FHitResult& Result, const FVector& IncomingVelocity)
{
    if (!HasAuthority() || bImpacted || !UsesFuse(Payload)) return;
    ++BounceCount;
    RestNormal = Result.ImpactNormal.GetSafeNormal();
    if (Result.GetActor() && Result.GetActor()->IsA<APawn>())
    {
        // Soft body contact: retain a little sliding motion and only 5% rebound.
        const float IntoSurface=FMath::Max(0.f,-FVector::DotProduct(IncomingVelocity,RestNormal));
        Movement->Velocity=FVector::VectorPlaneProject(IncomingVelocity,RestNormal)*.25f+RestNormal*IntoSurface*.05f;
    }
    const float Speed = Movement->Velocity.Size();
    // Small tangent impulses vary the contact without reversing the normal
    // rebound, adding energy, or allowing a client to choose its trajectory.
    const FVector Tangent = FVector::VectorPlaneProject(BounceRandom.VRand(),RestNormal).GetSafeNormal();
    Movement->Velocity = (Movement->Velocity + Tangent*Speed*.07f).GetSafeNormal()*Speed*BounceRandom.FRandRange(.92f,1.f);
    Spin = Spin*.6f + FVector::CrossProduct(RestNormal,IncomingVelocity)*.012f + BounceRandom.VRand()*2.f;
    Spin = Spin.GetClampedToMaxSize(22.f);
    const FQuat Floor = FQuat::FindBetweenNormals(FVector::UpVector,RestNormal);
    RestRotation = Floor * FRotator(BounceRandom.FRandRange(-12.f,12.f),BounceRandom.FRandRange(-180.f,180.f),BounceRandom.FRandRange(78.f,102.f)).Quaternion();
    bResting = RestNormal.Z>.45f && Speed<75.f;
    ForceNetUpdate();
}
void AERProjectile::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bImpacted) return;
    if(bHeld)
    {
        auto* Character=Cast<AERCharacter>(GetInstigator());
        if(!Character || Character->Health->bDead) ReleaseHeld(GetActorLocation(),FVector::ZeroVector);
        else SetActorLocation(SafeHandPosition(Character,true,this),false,nullptr,ETeleportType::TeleportPhysics);
        PreviousSmokePosition=GetActorLocation();
        return;
    }
    // Local cosmetic wake follows replicated ordnance; no extra RPCs or drag.
    if(GetWorld()->GetTimeSeconds()>=NextSmokeInteraction)
    {
        if((GetActorLocation()-PreviousSmokePosition).SizeSquared()>4.f)
            for(TActorIterator<AERSmokeCloud> It(GetWorld());It;++It)
                It->AddShot(PreviousSmokePosition,GetActorLocation(),UsesFuse(Payload)?25.f:12.f,UsesFuse(Payload)?1.5f:2.f);
        PreviousSmokePosition=GetActorLocation();NextSmokeInteraction=GetWorld()->GetTimeSeconds()+.1;
    }
    if (bResting || (!Movement->IsActive() && BounceCount>0))
        SetActorRotation(FQuat::Slerp(GetActorQuat(),RestRotation,1.f-FMath::Exp(-DeltaSeconds*9.f)));
    else if (!Spin.IsNearlyZero())
        SetActorRotation((FQuat(Spin.GetSafeNormal(),Spin.Size()*DeltaSeconds)*GetActorQuat()).GetNormalized());
}
void AERProjectile::Hit(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent, FVector Impulse, const FHitResult& Result)
{
    if (!HasAuthority() || bImpacted || Other == GetInstigator() || Other == GetOwner()) return;
    if (UsesFuse(Payload))
    {
        if(Payload==EERPayload::LaunchedHE && Cast<AERCharacter>(Other) && !ContactDamagedPlayers.Contains(Other))
        {
            ContactDamagedPlayers.Add(Other);
            UGameplayStatics::ApplyPointDamage(Other,Damage*.25f,Movement->Velocity.GetSafeNormal(),Result,
                GetDamageController(),this,UDamageType::StaticClass());
        }
        return; // Contact never consumes or restarts the fuse.
    }
    if (Payload == EERPayload::Rocket) { Detonate(); return; }
    const FVector ShotDirection = Movement->Velocity.GetSafeNormal();
    bImpacted = true; Movement->StopMovementImmediately(); Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    UGameplayStatics::ApplyPointDamage(Other, Damage, ShotDirection, Result, GetDamageController(), this, UDamageType::StaticClass());
    if (Other && !Other->IsA<APawn>())
        MulticastImpact(Result.ImpactPoint, Result.ImpactNormal, FMath::FRandRange(0.f, 360.f));
    SetLifeSpan(0.1f);
}
void AERProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AERProjectile, Payload); DOREPLIFETIME(AERProjectile, ThrownMesh);
    DOREPLIFETIME(AERProjectile,bHeld);DOREPLIFETIME(AERProjectile,FuseEndServerTime);
}
float AERProjectile::TakeDamage(float Amount,const FDamageEvent& Event,AController* DamageInstigator,AActor* Causer)
{
    const auto* Gun=Cast<AERWeapon>(Causer);const auto* Round=Cast<AERProjectile>(Causer);
    const bool Bullet=(Gun && Gun->Data && Gun->Data->Payload==EERPayload::Bullet) || (Round && Round->Payload==EERPayload::Bullet);
    if(!HasAuthority() || bImpacted || !UsesFuse(Payload) || Amount<=0 || !Bullet || !Event.IsOfType(FPointDamageEvent::ClassID)) return 0;
    UE_LOG(LogTemp,Display,TEXT("ER_GRENADE_SHOT_DETONATE held=%d kind=%d"),bHeld,int32(Payload));
    Detonate();return Amount;
}
void AERProjectile::Detonate()
{
    if (!HasAuthority() || bImpacted) return;
    if(auto* Gun=Cast<AERWeapon>(GetOwner())) Gun->OnGrenadeDetonated(this);
    bHeld=false;
    bImpacted = true; Movement->StopMovementImmediately(); Movement->Deactivate();
    ShotTarget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    const FVector Position = GetActorLocation();
    // Snapshot exposure before character deaths/drop callbacks or block removal.
    // Queue each live grenade once; chained blasts never recurse on this stack.
    if(Damage>0 && (Payload==EERPayload::HE || Payload==EERPayload::LaunchedHE || Payload==EERPayload::Rocket))
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ERGrenadeChain),false,this);
        for(TActorIterator<AERProjectile> It(GetWorld());It;++It)
        {
            auto* Other=*It;
            if(Other==this || Other->bImpacted || Other->bChainPending || !UsesFuse(Other->Payload)) continue;
            const float Distance=FVector::Dist(Position,Other->GetActorLocation());
            if(Distance>=ExplosionRadius || (Payload!=EERPayload::Rocket &&
                ERWeaponBallistics::GrenadeDamage(Damage,Distance,ExplosionInnerRadius,ExplosionRadius)<=0)) continue;
            FHitResult Cover;
            if(GetWorld()->LineTraceSingleByObjectType(Cover,Position+FVector::UpVector*3.f,Other->GetActorLocation(),
                FCollisionObjectQueryParams(ECC_WorldStatic),Query)) continue;
            Other->bChainPending=true;
            UE_LOG(LogTemp,Display,TEXT("ER_GRENADE_CHAIN_QUEUED held=%d distance=%.1f"),Other->bHeld,Distance);
            const TWeakObjectPtr<AERProjectile> Pending=Other;
            GetWorldTimerManager().SetTimerForNextTick([Pending]()
            {
                if(auto* Grenade=Pending.Get()) { Grenade->bChainPending=false;Grenade->Detonate(); }
            });
        }
    }
    if(Payload==EERPayload::HE || Payload==EERPayload::LaunchedHE || Payload==EERPayload::Rocket)
    for(TActorIterator<AERClaymore> It(GetWorld());It;++It)
    {
        if(FVector::Distance(Position,It->GetActorLocation())>ExplosionRadius)continue;
        FHitResult Cover;FCollisionQueryParams Query(SCENE_QUERY_STAT(ERClaymoreNeutralize),true,this);
        if(!GetWorld()->LineTraceSingleByObjectType(Cover,Position+FVector::UpVector*3,It->GetActorLocation(),FCollisionObjectQueryParams(ECC_WorldStatic),Query))
            It->TakeDamage(Damage,FDamageEvent(),GetDamageController(),this);
    }
    for (TActorIterator<AERCharacter> It(GetWorld()); It; ++It)
    {
        AERCharacter* Target = *It;
        if (Target->Health->bDead) continue;
        const float Distance = FVector::Dist(Position, Target->GetActorLocation());
        if (Distance > ExplosionRadius) continue;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ERExplosion), false, this);
        Query.AddIgnoredActor(Target);
        float Exposure=0;
        const bool Fragmentation=Payload==EERPayload::HE || Payload==EERPayload::LaunchedHE;
        const TArray<FVector> Samples=Fragmentation?TArray<FVector>{Target->Camera->GetComponentLocation(),Target->GetActorLocation(),
            Target->GetActorLocation()-FVector(0,0,Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*.55f)}:
            TArray<FVector>{Target->Camera->GetComponentLocation()};
        for(const FVector& Sample:Samples)
        {
            FHitResult Cover;
            if(!GetWorld()->LineTraceSingleByObjectType(Cover,Position+FVector::UpVector*3.f,Sample,
                FCollisionObjectQueryParams(ECC_WorldStatic),Query)) Exposure+=1.f/Samples.Num();
        }
        if(Exposure<=0) continue;
        if (Payload == EERPayload::HE || Payload == EERPayload::Rocket || Payload == EERPayload::LaunchedHE)
        {
            const FVector Direction = (Target->GetActorLocation()-Position).GetSafeNormal();
            const FHitResult AreaHit(Target,Target->GetCapsuleComponent(),Target->GetActorLocation(),-Direction);
            const float AreaDamage=Fragmentation?ERWeaponBallistics::GrenadeDamage(Damage,Distance,ExplosionInnerRadius,ExplosionRadius):
                Damage*FMath::Clamp(1.f-Distance/ExplosionRadius,.15f,1.f);
            UGameplayStatics::ApplyPointDamage(Target, AreaDamage*Exposure, Direction, AreaHit,
                GetDamageController(), this, UDamageType::StaticClass());
        }
        else if (Payload == EERPayload::Stun)
            if (auto* PC = Cast<AERPlayerController>(Target->Controller))
            {
                const float Facing = FVector::DotProduct(PC->GetControlRotation().Vector(), (Position-Target->Camera->GetComponentLocation()).GetSafeNormal());
                PC->ClientFlash(Facing > .3f ? .9f : .3f);
            }
    }
    // Player cover checks above see the intact walls. Block exposure is also
    // snapshotted before removal, including cover across multiple room actors.
    if (Payload == EERPayload::HE || Payload == EERPayload::Rocket || Payload == EERPayload::LaunchedHE)
    {
        // Fragment injury range is larger than the local structural blast.
        // HE uses its own structural profile; character damage above is
        // unchanged. Rockets keep their previous linear building envelope.
        const FERBlockExplosionProfile Structure=Payload==EERPayload::Rocket ?
            FERBlockExplosionProfile::Linear(ExplosionRadius,Damage) : BlockExplosion;
        AERBlockRoom::ApplyWorldExplosion(GetWorld(), Position + FVector::UpVector * 10, Structure, this);
        UE_LOG(LogTemp,Display,TEXT("ER_GRENADE_STRUCTURE damage=%.1f inner=%.1f radius=%.1f exponent=%.2f"),
            Structure.BaseDamage,Structure.InnerRadius,Structure.OuterRadius,Structure.FalloffExponent);
    }
    MulticastDetonate(Position, Payload);
    if (Payload==EERPayload::Smoke && SmokeCloudClass)
    {
        TArray<AERSmokeCloud*> Clouds;
        for(TActorIterator<AERSmokeCloud> It(GetWorld());It;++It) Clouds.Add(*It);
        Clouds.Sort([](const AERSmokeCloud& A,const AERSmokeCloud& B){return A.StartedAtServerTime<B.StartedAtServerTime;});
        while(Clouds.Num()>=4) { Clouds[0]->Destroy(); Clouds.RemoveAt(0); }
        const FTransform Transform(FRotator::ZeroRotator,Position);
        auto* Cloud=GetWorld()->SpawnActorDeferred<AERSmokeCloud>(SmokeCloudClass,Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(Cloud)
        {
            Cloud->SmokeColor=SmokeTeamColor;
            UGameplayStatics::FinishSpawningActor(Cloud,Transform);
        }
    }
    ForceNetUpdate();
    SetLifeSpan(Payload == EERPayload::Smoke && !SmokeCloudClass ? 12.f : .3f);
    UE_LOG(LogTemp, Display, TEXT("ER_PAYLOAD_DETONATE kind=%d position=%s"), int32(Payload), *Position.ToString());
}
void AERProjectile::MulticastDetonate_Implementation(FVector_NetQuantize Position, EERPayload Kind)
{
    bImpacted=true;bHeld=false;
    ShotTarget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if(Kind==EERPayload::HE || Kind==EERPayload::LaunchedHE || Kind==EERPayload::Rocket)
        for(TActorIterator<AERSmokeCloud> It(GetWorld());It;++It)
            It->AddShot(FVector(Position)-FVector(0,0,60),FVector(Position)+FVector(0,0,60),400.f,12.f);
    Visual->SetVisibility(false);
    if(Kind==EERPayload::Smoke && SmokeCloudClass) { Effect->SetVisibility(false); return; }
    if ((Kind == EERPayload::HE || Kind == EERPayload::LaunchedHE) && GrenadeExplosionClass)
    {
        Effect->SetVisibility(false);
        if (GetNetMode() != NM_DedicatedServer)
        {
            // A separate local actor outlives the projectile's 300ms cleanup.
            // It has no collision, replication or gameplay damage of its own.
            TArray<AERGrenadeExplosion*> Bursts;
            for (TActorIterator<AERGrenadeExplosion> It(GetWorld()); It; ++It) Bursts.Add(*It);
            Bursts.Sort([](const AERGrenadeExplosion& A, const AERGrenadeExplosion& B) { return A.GetGameTimeSinceCreation() > B.GetGameTimeSinceCreation(); });
            while (Bursts.Num() >= 8) { Bursts[0]->Destroy(); Bursts.RemoveAt(0); }
            GetWorld()->SpawnActor<AERGrenadeExplosion>(GrenadeExplosionClass, FVector(Position), FRotator::ZeroRotator);
        }
        return;
    }
    Effect->SetWorldLocation(FVector(Position) + (Kind == EERPayload::Smoke ? FVector(0,0,160) : FVector::ZeroVector));
    Effect->SetMaterial(0, Kind == EERPayload::Smoke ? SmokeMaterial : ExplosionMaterial);
    Effect->SetWorldScale3D(FVector(Kind == EERPayload::Smoke ? 6.f : 1.5f));
    Effect->SetVisibility(true);
}
void AERProjectile::MulticastImpact_Implementation(FVector_NetQuantize Position, FVector_NetQuantizeNormal Normal, float Roll)
{
    for(TActorIterator<AERProceduralMap> It(GetWorld());It;++It)
        if(It->PlayWaterImpactAt(Position))return;
    SpawnBulletHole(GetWorld(), ImpactDecalMaterial, Position, Normal, Roll);
}

void AERWeapon::Tick(float Delta)
{
    Super::Tick(Delta);
    UpdateClaymorePlacement();
    const auto* C=Cast<AERCharacter>(GetOwner());
    const bool Show=C && C->IsLocallyControlled() && C->Weapon==this && !C->Health->bDead && Data && Data->Payload==EERPayload::Claymore && MagazineAmmo>0;
    if(!Show) { if(PlacementGhost)PlacementGhost->SetVisibility(false);return; }
    if(!PlacementGhost)
    {
        PlacementGhost=NewObject<UStaticMeshComponent>(this);AddInstanceComponent(PlacementGhost);
        PlacementGhost->SetReceivesDecals(false);
        PlacementGhost->SetStaticMesh(Data->PlacedMesh?Data->PlacedMesh.Get():GetDefault<AERClaymore>()->Body->GetStaticMesh().Get());
        PlacementGhost->SetCollisionEnabled(ECollisionEnabled::NoCollision);PlacementGhost->SetCastShadow(false);
        PlacementGhost->RegisterComponent();PlacementGhost->SetWorldScale3D(Data->PlacedMesh?FVector(1):FVector(.15f,.30f,.18f));
    }
    FHitResult H;FRotator R;const bool Valid=AERClaymore::Placement(const_cast<AERCharacter*>(C),H,R);
    PlacementGhost->SetVisibility(Valid);
    if(Valid)PlacementGhost->SetWorldLocationAndRotation(H.ImpactPoint,R);
}
