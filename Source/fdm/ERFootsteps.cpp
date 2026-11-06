#include "ERFootsteps.h"
#include "ERCharacter.h"
#include "ERMovement.h"
#include "ERPlayerController.h"
#include "ERProceduralMap.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"
#include "UObject/ConstructorHelpers.h"

float FERFootstepCadence::Stride(float Speed, bool Low, bool Prone)
{
    if (Prone) return 65.f;
    if (Low) return 80.f;
    if (Speed<=138.8889f) return FMath::Lerp(70.f,95.f,FMath::Clamp(Speed/138.8889f,0.f,1.f));
    if (Speed<=277.7778f) return FMath::Lerp(95.f,130.f,(Speed-138.8889f)/138.8889f);
    return FMath::Lerp(130.f,150.f,FMath::Clamp((Speed-277.7778f)/222.2222f,0.f,1.f));
}
float FERFootstepCadence::Gain(float Speed, bool Low, bool Prone)
{
    if (Prone) return .3f;
    if (Low) return .4f;
    if (Speed<=138.8889f) return .7f;
    if (Speed<=277.7778f) return FMath::Lerp(.7f,1.f,(Speed-138.8889f)/138.8889f);
    return FMath::Lerp(1.f,1.15f,FMath::Clamp((Speed-277.7778f)/222.2222f,0.f,1.f));
}
bool FERFootstepCadence::Advance(const FVector& Position, bool Active, float Speed, bool Low, bool Prone, double Now)
{
    const float Travel=FVector::Dist2D(Position,Previous); Previous=Position;
    if (!Active || Speed<5.f || Travel>200.f)
    { Distance=0; Grounded=false; return false; }
    if (!Grounded) { Grounded=true; Distance=0; return false; }
    // At most one event per update, discard backlog after hitches/teleports.
    Distance+=Travel;
    const float Length=Stride(Speed,Low,Prone);
    if (Distance<Length || Now-LastStep<.22) return false;
    Distance=FMath::Min(Distance-Length,Length*.25f); LastStep=Now; return true;
}
UERFootsteps::UERFootsteps()
{
    static ConstructorHelpers::FObjectFinder<USoundWave> SplashA(TEXT("/Game/EclipseRecoil/Audio/Water/S_ER_Wade_A"));
    static ConstructorHelpers::FObjectFinder<USoundWave> SplashB(TEXT("/Game/EclipseRecoil/Audio/Water/S_ER_Wade_B"));
    if(SplashA.Succeeded())WaterSteps.Add(SplashA.Object);
    if(SplashB.Succeeded())WaterSteps.Add(SplashB.Object);
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostPhysics;
    SetIsReplicatedByDefault(true);
}
void UERFootsteps::BeginPlay()
{
    Super::BeginPlay();
    if (auto* Character=Cast<AERCharacter>(GetOwner()))
        AddTickPrerequisiteComponent(Character->GetCharacterMovement());
    // Simulated and autonomous clients only play authoritative transient events.
    SetComponentTickEnabled(GetOwner()->HasAuthority());
}
void UERFootsteps::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    auto* Character=Cast<AERCharacter>(GetOwner());
    if (!Character || !Character->HasAuthority() || !Audio) return;
    const auto* Movement=Cast<UERMovement>(Character->GetCharacterMovement());
    const auto* PC=Cast<AERPlayerController>(Character->GetController());
    const bool Active=Character->GetController() && !Character->Health->bDead && Movement->IsMovingOnGround() &&
        !Movement->IsTraversing() && !(PC && (PC->bMenuOpen || PC->IsMoveInputIgnored()));
    const float Speed=Movement->Velocity.Size2D();
    const bool Low=Movement->Locomotion.HalfHeight<80.f, Prone=Movement->IsProne();
    const double Now=GetWorld()->GetTimeSeconds();
    const bool RegularStep=Cadence.Advance(Character->GetActorLocation(),Active,Speed,Low,Prone,Now);
    const FVector Feet=Character->GetActorLocation()-FVector(0,0,Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    bool InWater=false;
    for(TActorIterator<AERProceduralMap> It(GetWorld());It;++It)
        if(It->WaterDepthAt(Feet)>5.f)
        {InWater=true;break;}
    const bool CanHearEntry=Character->GetController() && !Character->Health->bDead &&
        !Movement->IsTraversing() && !(PC && (PC->bMenuOpen || PC->IsMoveInputIgnored()));
    const bool WaterEntry=InWater && !bWasInWater && CanHearEntry && Movement->Velocity.Size()>5.f;
    bWasInWater=InWater;
    if(WaterEntry)
    {
        Cadence.Distance=0;
        Cadence.LastStep=Now;
    }
    if(!WaterEntry && !RegularStep)return;
    if(InWater)
    {
        ++EmittedCount;
        MulticastStep(Feet,EERFootstepSurface::Water,FERFootstepCadence::Gain(Speed,Low,Prone));
        return;
    }
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERFootstepFloor),false,Character); Query.bReturnPhysicalMaterial=true;
    if (!GetWorld()->LineTraceSingleByObjectType(Hit,Feet+FVector(0,0,15),Feet-FVector(0,0,30),
        FCollisionObjectQueryParams(ECC_WorldStatic),Query) || Hit.ImpactNormal.Z<.5f) return;
    LastSurface=Audio->Resolve(Hit); ++EmittedCount;
    MulticastStep(Hit.ImpactPoint,LastSurface,FERFootstepCadence::Gain(Speed,Low,Prone));
}
void UERFootsteps::MulticastStep_Implementation(FVector_NetQuantize Position, EERFootstepSurface Surface, float Gain)
{
    auto* Character=Cast<AERCharacter>(GetOwner());
    if (!Character || Character->GetNetMode()==NM_DedicatedServer || Character->Health->bDead || !Audio) return;
    const bool Local=Character->IsLocallyControlled() && Character->Controller && Character->Controller->IsPlayerController();
    if (Local) if (const auto* PC=Cast<AERPlayerController>(Character->Controller); PC && PC->bMenuOpen) return;
    if(Surface==EERFootstepSurface::Water)
    {
        if(WaterSteps.Num()>0)
        {
            const int32 Variant=int32(PlaybackCount++%uint32(WaterSteps.Num()));
            UGameplayStatics::PlaySoundAtLocation(this,WaterSteps[Variant],Position,
                Audio->BaseVolume*Gain*(Local?Audio->OwnerVolume:1.f),1.f,0.f,Audio->Attenuation);
        }
        return;
    }
    auto* Palette=Audio->Find(Surface);
    const int32 Variant=ERAudio::Play(Character,Palette,Shuffles.FindOrAdd(Palette),Position,Local,
        Audio->BaseVolume*FMath::Clamp(Gain,0.f,1.2f)*(Local?Audio->OwnerVolume:1.f),Audio->Attenuation);
    if (Variant!=INDEX_NONE) ++PlaybackCount;
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(),TEXT("ERAudioAudit")))
        UE_LOG(LogTemp,Display,TEXT("ER_STEP_AUDIO pawn=%s local=%d surface=%d variant=%d count=%u"),
            *Character->GetName(),Local,uint8(Surface),Variant,PlaybackCount);
#endif
}
