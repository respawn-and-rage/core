#include "ERSkull.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "ERCharacter.h"
#include "ERRecon.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
    constexpr float SkullLifetime = 63.f;
    constexpr float SkullDiameter = 24.f;
    constexpr float GroundClearance = 10.f;
    constexpr double PickupInterval = .05;

    double SkullYaw(double Age)
    {
        for (double Duration = 1; Duration <= 32; Duration *= 2)
        {
            if (Age < Duration) return 360 * Age / Duration;
            Age -= Duration;
        }
        return 0;
    }
}

AERSkull::AERSkull()
{
    bReplicates = true;
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Coin = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Coin"));
    Coin->SetupAttachment(GetRootComponent());
    Coin->SetMobility(EComponentMobility::Movable);
    Coin->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Coin->SetCanEverAffectNavigation(false);
    Coin->SetCastShadow(false);
}

void AERSkull::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority()) SetLifeSpan(SkullLifetime);
    if (GetNetMode() == NM_DedicatedServer)
        SetActorTickInterval(PickupInterval);
    else if (auto* Surface = Coin->CreateDynamicMaterialInstance(0))
        Surface->SetVectorParameterValue(TEXT("TeamColor"), ERRecon::TeamColor(VictimTeam));
}

void AERSkull::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const auto* State = GetWorld()->GetGameState<AERGameState>();
    if (!State) return;
    const double Age = FMath::Max(0.0, State->GetServerWorldTimeSeconds() - SpawnTime);
    if (Age >= SkullLifetime)
    {
        if (HasAuthority()) Destroy();
        else SetActorHiddenInGame(true);
        return;
    }
    if (GetNetMode() != NM_DedicatedServer) Coin->SetRelativeRotation(FRotator(0, SkullYaw(Age), 0));

    auto* Mode = GetWorld()->GetAuthGameMode<AERGameMode>();
    if (!Mode || !Mode->IsMatchInProgress() || State->GetServerWorldTimeSeconds() >= State->MatchEndServerTime) return;
    // Check the server's pawn capsules directly, including a player already over the drop.
    TArray<FOverlapResult> Nearby;
    const FVector Location = GetActorLocation();
    GetWorld()->OverlapMultiByObjectType(Nearby, Location, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
        FCollisionShape::MakeSphere(SkullDiameter * .5f));
    for (const FOverlapResult& Hit : Nearby)
    {
        const auto* Character = Cast<AERCharacter>(Hit.GetActor());
        if (!Character || Hit.GetComponent() != Character->GetCapsuleComponent() ||
            !Character->bNetworkReady || Character->Health->bDead || !Character->GetController()) continue;
        auto* PS = Character->GetPlayerState<AERPlayerState>();
        if (!PS || PS->Team == EERTeam::None) continue;
        // The collection radius can intersect a capsule on the other side of thin cover.
        if (GetWorld()->LineTraceTestByObjectType(Location, Character->GetActorLocation(), FCollisionObjectQueryParams(ECC_WorldStatic))) continue;
        if (Destroy() && PS->Team != VictimTeam) Mode->CollectSkull(*PS);
        return;
    }
}

void AERSkull::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(AERSkull, VictimTeam, COND_InitialOnly);
    DOREPLIFETIME_CONDITION(AERSkull, SpawnTime, COND_InitialOnly);
}

void AERGameMode::DropSkull(const AERCharacter& Victim, EERTeam Team)
{
    if (!ensureMsgf(SkullClass, TEXT("Skull Hunt requires a SkullClass in the game mode Blueprint."))) return;
    const FVector Center = Victim.GetActorLocation();
    const FVector Feet = Center - FVector(0, 0, Victim.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    FHitResult Ground;
    const bool bGrounded = GetWorld()->LineTraceSingleByObjectType(Ground, Center, Feet - FVector(0, 0, 5000),
        FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(SkullGround), false, &Victim));
    const FVector Location = (bGrounded ? Ground.ImpactPoint : Feet) + FVector(0, 0, GroundClearance + SkullDiameter * .5f);
    const FTransform Transform(FRotator::ZeroRotator, Location);
    if (auto* Skull = GetWorld()->SpawnActorDeferred<AERSkull>(SkullClass, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
    {
        Skull->VictimTeam = Team;
        Skull->SpawnTime = GetGameState<AERGameState>()->GetServerWorldTimeSeconds();
        Skull->FinishSpawning(Transform);
    }
}

void AERGameMode::CollectSkull(AERPlayerState& Collector)
{
    auto* State = GetGameState<AERGameState>();
    const int32 OldAlpha = State->AlphaScore, OldOmega = State->OmegaScore;
    ++Collector.Skulls;
    if (Collector.Team == EERTeam::Alpha) ++State->AlphaScore;
    else ++State->OmegaScore;
    Collector.ForceNetUpdate();
    State->ForceNetUpdate();
    GetWorld()->GetSubsystem<UERGameplayMessages>()->ScoresChanged(OldAlpha, OldOmega);
    CheckMatch();
}
