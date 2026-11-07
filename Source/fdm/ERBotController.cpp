#include "ERBotController.h"
#include "ERCharacter.h"
#include "ERMatch.h"
#include "ERWeapon.h"
#include "ERMovement.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"

AERBotPatrolPoint::AERBotPatrolPoint()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    bIsEditorOnlyActor = false; // Route is needed by the server during play.
}
AERBotController::AERBotController()
{
    bWantsPlayerState = true;
    bSetControlRotationFromPawnOrientation = false;
    PrimaryActorTick.bCanEverTick = true;
    // CharacterMovement consumes input every frame; steering must feed every
    // frame too. Only the perception scan is throttled separately.
    PrimaryActorTick.TickInterval = 0;
}
void AERBotController::ResetLife()
{
    bAwaitingRespawn = false; RespawnAllowedAt = 0;
    Target.Reset(); NextPerceptionAt = NextBurstAt = BurstEndsAt = 0;
    NextProgressCheckAt = GetWorld()->GetTimeSeconds() + 2;
    MotionEpoch=GetWorld()->GetTimeSeconds();
    const float Stagger=MovementPhaseOffset;
    MotionEpoch-=Stagger;NextDemoJump=GetWorld()->GetTimeSeconds()+3+Stagger;
    JumpReleaseAt=ObstacleUntil=MotionPauseUntil=0;NextObstacleSearch=GetWorld()->GetTimeSeconds()+2;
    ObstacleSearch=MotionPosture=0;PosturePauseUntil=0;SawMotionTraversal=false;
    if(auto* C=Cast<AERCharacter>(GetPawn()))
    {
        auto* M=Cast<UERMovement>(C->GetCharacterMovement());
        M->bClimbHeld=M->bProneWanted=false;C->UnCrouch();C->StopJumping();
    }
    if (GetPawn()) LastProgressPosition = GetPawn()->GetActorLocation();
}
void AERBotController::OnPossess(APawn* PossessedPawn)
{
    Super::OnPossess(PossessedPawn);
    ResetLife();
    Route.Reset();
    for (TActorIterator<AERBotPatrolPoint> It(GetWorld()); It; ++It) Route.Add(*It);
    Route.Sort([](const AERBotPatrolPoint& A, const AERBotPatrolPoint& B) { return A.RouteIndex < B.RouteIndex; });
    float Nearest = TNumericLimits<float>::Max();
    for (int32 I = 0; I < Route.Num(); ++I)
    {
        const float Distance = FVector::DistSquared2D(PossessedPawn->GetActorLocation(), Route[I]->GetActorLocation());
        if (Distance < Nearest) { Nearest = Distance; RoutePosition = I; }
    }
    if (auto* BotCharacter = Cast<AERCharacter>(PossessedPawn)) BotCharacter->GetCharacterMovement()->MaxWalkSpeed = 1500.f / 3.6f;
}
bool AERBotController::HasSight(AERCharacter* Other) const
{
    const auto* BotCharacter = Cast<AERCharacter>(GetPawn());
    if (!BotCharacter || !Other || Other == BotCharacter || Other->Health->bDead || !Other->bNetworkReady) return false;
    const auto* SelfState = GetPlayerState<AERPlayerState>();
    const auto* OtherState = Other->GetPlayerState<AERPlayerState>();
    if (!SelfState || !OtherState || OtherState->Team == EERTeam::None || SelfState->Team == OtherState->Team) return false;
    const FVector Start = BotCharacter->Camera->GetComponentLocation();
    const FVector End = Other->GetActorLocation() + FVector(0,0,30);
    if (FVector::DistSquared(Start, End) > FMath::Square(4000.f)) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ERBotSight), false, BotCharacter);
    if (BotCharacter->Weapon) Params.AddIgnoredActor(BotCharacter->Weapon);
    FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) || Hit.GetActor() == Other;
}
void AERBotController::Patrol(AERCharacter* BotCharacter, float DeltaTime)
{
    if (Route.IsEmpty())
    {
        // Labs may have no authored route: steer locally and turn when blocked.
        if (GetWorld()->GetTimeSeconds() >= NextProgressCheckAt)
        {
            if (FVector::DistSquared2D(LastProgressPosition, BotCharacter->GetActorLocation()) < FMath::Square(30.f))
                SetControlRotation(GetControlRotation()+FRotator(0,90,0));
            LastProgressPosition=BotCharacter->GetActorLocation(); NextProgressCheckAt=GetWorld()->GetTimeSeconds()+1;
        }
        BotCharacter->AddMovementInput(GetControlRotation().Vector());
        BotCharacter->SetActorRotation(FRotator(0,GetControlRotation().Yaw,0));
        return;
    }
    FVector Direction = Route[RoutePosition]->GetActorLocation() - BotCharacter->GetActorLocation();
    Direction.Z = 0;
    if (Direction.SizeSquared() < FMath::Square(FMath::Clamp(Route[RoutePosition]->AcceptanceRadius,5.f,90.f)))
    {
        RoutePosition = (RoutePosition + RouteDirection + Route.Num()) % Route.Num();
        Direction = Route[RoutePosition]->GetActorLocation() - BotCharacter->GetActorLocation(); Direction.Z = 0;
    }
    if (GetWorld()->GetTimeSeconds() >= NextProgressCheckAt)
    {
        if (FVector::DistSquared2D(LastProgressPosition, BotCharacter->GetActorLocation()) < FMath::Square(30.f))
        {
            // A player or edited obstacle can block the route: try the other direction.
            RouteDirection *= -1;
            RoutePosition = (RoutePosition + RouteDirection + Route.Num()) % Route.Num();
        }
        LastProgressPosition = BotCharacter->GetActorLocation(); NextProgressCheckAt = GetWorld()->GetTimeSeconds()+2;
    }
    BotCharacter->AddMovementInput(Direction.GetSafeNormal());
    const FRotator Facing = FMath::RInterpTo(GetControlRotation(), Direction.Rotation(), DeltaTime, 5);
    SetControlRotation(Facing); BotCharacter->SetActorRotation(FRotator(0,Facing.Yaw,0));
}
void AERBotController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!HasAuthority()) return;
    auto* Mode = GetWorld()->GetAuthGameMode<AERGameMode>();
    if (!Mode) return;
    const double Now = GetWorld()->GetTimeSeconds();
    if (bAwaitingRespawn)
    {
        if (Now >= RespawnAllowedAt) Mode->RequestRespawn(this);
        return; // A new life never inherits held fire.
    }
    auto* BotCharacter = Cast<AERCharacter>(GetPawn());
    if (!BotCharacter || !BotCharacter->Weapon || BotCharacter->Health->bDead) return;
    auto* Weapon = BotCharacter->Weapon.Get();
    if (!Mode->IsMatchInProgress() || bTrainingTarget)
    {
        Weapon->StopFire(); BotCharacter->GetCharacterMovement()->StopMovementImmediately(); Target.Reset(); return;
    }
    if (bMovementOnly) { Weapon->StopFire(); Target.Reset(); MovementDemo(BotCharacter, DeltaTime); return; }
    if(BotCharacter->bPresetInventory && Weapon->MagazineAmmo+Weapon->ReserveAmmo==0)
    {
        for(int32 Slot=0;Slot<BotCharacter->Inventory.Num();++Slot)
        {
            const auto* Candidate=BotCharacter->Inventory[Slot].Get();
            if(Candidate && Candidate->Data && Candidate->Data->Payload==EERPayload::Bullet && Candidate->MagazineAmmo+Candidate->ReserveAmmo>0)
            { BotCharacter->ServerSelectWeapon(Slot);Weapon=BotCharacter->Weapon.Get();break; }
        }
    }
    if (Weapon->MagazineAmmo == 0 || (!Target.IsValid() && Weapon->MagazineAmmo < 10)) BotCharacter->ServerReload();
    if (Now >= NextPerceptionAt)
    {
        AERCharacter* Nearest = nullptr; float Distance = TNumericLimits<float>::Max();
        for (TActorIterator<AERCharacter> It(GetWorld()); It; ++It)
            if (HasSight(*It))
            {
                const float Candidate = FVector::DistSquared(BotCharacter->GetActorLocation(), It->GetActorLocation());
                if (Candidate < Distance) { Nearest = *It; Distance = Candidate; }
            }
        if (Nearest != Target.Get()) { Weapon->StopFire(); BurstEndsAt = 0; NextBurstAt = Now+.5; }
        Target = Nearest; NextPerceptionAt = Now+.2;
    }
    if (!Target.IsValid() || !HasSight(Target.Get()))
    {
        Weapon->StopFire(); BurstEndsAt = 0; Patrol(BotCharacter, DeltaTime); return;
    }
    BotCharacter->GetCharacterMovement()->StopMovementImmediately();
    NextProgressCheckAt = Now+2; LastProgressPosition = BotCharacter->GetActorLocation();
    const FVector AimPoint = Target->GetActorLocation()+FVector(0,0,30);
    FRotator Desired = (AimPoint-BotCharacter->Camera->GetComponentLocation()).Rotation();
    // Gentle deterministic aiming error and short bursts make the first test opponent usable.
    const float Offset = float(Now)*2.f + float(GetUniqueID()%13);
    Desired.Yaw += FMath::Sin(Offset)*1.6f; Desired.Pitch += FMath::Cos(Offset*.7f)*1.1f;
    const FRotator Aim = FMath::RInterpTo(GetControlRotation(), Desired, DeltaTime, 6);
    SetControlRotation(Aim); BotCharacter->SetActorRotation(FRotator(0,Aim.Yaw,0));
    if (BurstEndsAt > 0 && Now >= BurstEndsAt)
    {
        BotCharacter->ServerStopFire(); BurstEndsAt = 0; NextBurstAt = Now+.85;
    }
    if (BurstEndsAt == 0 && Now >= NextBurstAt && !Weapon->bReloading && Weapon->MagazineAmmo > 0 &&
        FVector::DotProduct(Aim.Vector(), Desired.Vector()) > .998f)
    {
        BotCharacter->ServerStartFire(); BurstEndsAt = Now+.3;
    }
}
