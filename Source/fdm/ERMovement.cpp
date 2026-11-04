#include "ERMovement.h"
#include "ERServerSettings.h"
#include "ERCharacter.h"
#include "ERWeapon.h"
#include "ERLadder.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
class FERSavedMove : public FSavedMove_Character
{
public:
    using Super = FSavedMove_Character;
    bool Walk = false, Climb = false, High = false, Sprint = false, Prone = false;
    FVector Start, End;
    float Elapsed = 0, Duration = 0;
    double Retry = 0;
    FERLocomotionState Posture;
    bool PreviousSprint = false, LadderNeedsRelease = false;
    double SprintRequest = 0;
    virtual void Clear() override { Super::Clear(); Walk = Climb = High = Sprint = Prone = false; Elapsed = Duration = 0; Retry = 0; Start = End = FVector::ZeroVector; Posture = FERLocomotionState(); PreviousSprint = LadderNeedsRelease = false; SprintRequest = 0; }
    virtual uint8 GetCompressedFlags() const override
    {
        return Super::GetCompressedFlags() | (Walk ? FLAG_Custom_0 : 0) | (Climb ? FLAG_Custom_1 : 0) | (Sprint ? FLAG_Custom_2 : 0) | (Prone ? FLAG_Custom_3 : 0);
    }
    virtual bool CanCombineWith(const FSavedMovePtr& Other, ACharacter* Character, float MaxDelta) const override
    {
        const auto* Move = static_cast<const FERSavedMove*>(Other.Get());
        return Duration == 0 && Move->Duration == 0 && Walk == Move->Walk && Climb == Move->Climb && Sprint == Move->Sprint && Prone == Move->Prone && Super::CanCombineWith(Other, Character, MaxDelta);
    }
    virtual void SetMoveFor(ACharacter* Character, float Delta, FVector const& Accel, FNetworkPredictionData_Client_Character& Data) override
    {
        Super::SetMoveFor(Character, Delta, Accel, Data);
        const auto* Move = Cast<UERMovement>(Character->GetCharacterMovement());
        LadderNeedsRelease=Move->bLadderNeedsRelease;
        Posture = Move->Locomotion; PreviousSprint = Move->bPreviousSprintHeld; SprintRequest = Move->SprintRequestUntil;
        Walk = Move->bWalkHeld; Climb = Move->bClimbHeld; Sprint = Move->bSprintHeld; Prone = Move->bProneWanted;
        bForceNoCombine |= Move->IsTraversing() || Move->IsChangingPosture() || Sprint || Move->GetRecoveryRemaining() > 0;
        Start=Move->TraversalStart; End=Move->TraversalEnd; Elapsed=Move->TraversalElapsed; Duration=Move->TraversalDuration; Retry=Move->RetryAfter; High=Move->bHighClimb;
    }
    virtual void PrepMoveFor(ACharacter* Character) override
    {
        Super::PrepMoveFor(Character);
        auto* Move = Cast<UERMovement>(Character->GetCharacterMovement());
        Move->bLadderNeedsRelease=LadderNeedsRelease;
        Move->Locomotion = Posture; Move->bPreviousSprintHeld = PreviousSprint; Move->SprintRequestUntil = SprintRequest;
        Character->GetCapsuleComponent()->SetCapsuleHalfHeight(Posture.HalfHeight, true);
        Move->ApplyPosturePresentation();
        Move->bWalkHeld = Walk; Move->bClimbHeld = Climb; Move->bSprintHeld = Sprint; Move->bProneWanted = Prone;
        Move->TraversalStart=Start; Move->TraversalEnd=End; Move->TraversalElapsed=Elapsed; Move->TraversalDuration=Duration; Move->RetryAfter=Retry; Move->bHighClimb=High;
    }
};
class FERPredictionData : public FNetworkPredictionData_Client_Character
{
public:
    using Super = FNetworkPredictionData_Client_Character;
    FERPredictionData(const UCharacterMovementComponent& Movement) : Super(Movement) {}
    virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FERSavedMove()); }
};
}
UERMovement::UERMovement()
{
    SetIsReplicatedByDefault(true);
    SetCrouchedHalfHeight(62.5f);
    MaxWalkSpeed = 1500.f / 3.6f; MaxWalkSpeedCrouched = 150.f;
    MaxAcceleration = 1000; GroundFriction = 3;
    bUseSeparateBrakingFriction = true; BrakingFriction = 0;
    BrakingDecelerationWalking = 850;
    MaxStepHeight = 30; JumpZVelocity = 460; GravityScale = 1.8f; AirControl = .2f;
}
void UERMovement::ApplyServerSettings()
{
    MaxWalkSpeed=ERServer::Get(GetWorld(),"Move.Run")/ .036f;
    MaxWalkSpeedCrouched=ERServer::Get(GetWorld(),"Move.Crouch")/.036f;
    MaxAcceleration=ERServer::Get(GetWorld(),"Move.Acceleration");
    BrakingDecelerationWalking=ERServer::Get(GetWorld(),"Move.Braking");
    GroundFriction=ERServer::Get(GetWorld(),"Move.Friction");
    MaxStepHeight=ERServer::Get(GetWorld(),"Move.Step")*100;
    JumpZVelocity=ERServer::Get(GetWorld(),"Move.Jump")*100;
    GravityScale=ERServer::Get(GetWorld(),"Move.Gravity");AirControl=ERServer::Get(GetWorld(),"Move.AirControl");
}
double UERMovement::SprintDuration() const { return ERServer::Get(GetWorld(),"Move.SprintDuration"); }
double UERMovement::RecoveryDuration() const { return ERServer::Get(GetWorld(),"Move.Recovery"); }
float UERMovement::GetMaxSpeed() const
{
    const auto* C=Cast<AERCharacter>(CharacterOwner);
    if(C && C->Weapon && C->Weapon->bPlacingClaymore)return 0;
    if (IsOnLadder()) return LadderSpeed();
    if (IsMantling()) return 0;
    if (IsChangingPosture()) return ERServer::Get(GetWorld(),"Move.Posture")/.036f;
    if (IsProne()) return ERServer::Get(GetWorld(),"Move.Prone")/.036f;
    if (Locomotion.HalfHeight < 92.f) return ERServer::Get(GetWorld(),"Move.Crouch")/.036f;
    if (IsSprinting()) return ERServer::Get(GetWorld(),"Move.Sprint")/.036f;
    return bWalkHeld ? ERServer::Get(GetWorld(),"Move.Walk")/.036f : ERServer::Get(GetWorld(),"Move.Run")/.036f;
}
void UERMovement::UpdateFromCompressedFlags(uint8 Flags)
{
    Super::UpdateFromCompressedFlags(Flags);
    bWalkHeld = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
    bClimbHeld = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
    bSprintHeld = (Flags & FSavedMove_Character::FLAG_Custom_2) != 0;
    bProneWanted = (Flags & FSavedMove_Character::FLAG_Custom_3) != 0;
}
FNetworkPredictionData_Client* UERMovement::GetPredictionData_Client() const
{
    if (!ClientPredictionData) const_cast<UERMovement*>(this)->ClientPredictionData = new FERPredictionData(*this);
    return ClientPredictionData;
}
bool UERMovement::FindLedge(FVector& Destination, float& Height) const
{
    if (!CharacterOwner || !UpdatedComponent || Locomotion.HalfHeight < 92.f || bProneWanted || IsChangingPosture()) return false;
    const auto* Capsule = CharacterOwner->GetCapsuleComponent();
    const float Half = Capsule->GetScaledCapsuleHalfHeight(), Radius = Capsule->GetScaledCapsuleRadius();
    const FVector Position = UpdatedComponent->GetComponentLocation();
    const FVector Feet = Position - FVector(0,0,Half);
    const float Step=ERServer::Get(GetWorld(),"Move.Step")*100,Vault=ERServer::Get(GetWorld(),"Move.Vault")*100,Climb=ERServer::Get(GetWorld(),"Move.Climb")*100;
    const FVector Forward = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ERLedge), false, CharacterOwner);
    FHitResult Wall, Top;
    // A short wall feeler prevents mounting distant blocks or ordinary stairs.
    if (!GetWorld()->LineTraceSingleByChannel(Wall, Feet + FVector(0,0,35), Feet + FVector(0,0,35) + Forward*(Radius+25), ECC_Visibility, Params)) return false;
    if (FMath::Abs(Wall.ImpactNormal.Z) > .2f) return false;
    auto SampleTop = [&](float Along,FHitResult& Hit)
    {
        const FVector P=Feet+Forward*Along;
        return GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(0,0,Climb),P+FVector(0,0,Step+.1f),ECC_Visibility,Params) && IsWalkable(Hit);
    };
    // Start just inside the face. A fixed probe55cm beyond it skips thin
    // parapets completely, even when they have a valid walkable top.
    const float Near=FVector::DotProduct(Wall.ImpactPoint-Feet,Forward)+2,Preferred=Radius+55;
    if(!SampleTop(Near,Top))return false;
    Height = Top.ImpactPoint.Z - Feet.Z;
    if (Height <= Step || Height > Climb || (Height > Vault && !bClimbHeld) || (IsFalling() && !bClimbHeld)) return false;
    // Keep the old landing distance on broad obstacles. On a narrow top,
    // stop at its first edge and land inside the supported strip. Sampling
    // the interval also prevents choosing a different platform across a gap.
    float Landing=Near,Last=Near;
    const int32 Samples=FMath::Clamp(FMath::CeilToInt((Preferred-Near)/8),1,16);
    for(int32 I=1;I<=Samples;++I)
    {
        const float Along=FMath::Lerp(Near,Preferred,float(I)/Samples);FHitResult Candidate;
        if(SampleTop(Along,Candidate) && FMath::Abs(Candidate.ImpactPoint.Z-Top.ImpactPoint.Z)<2.5f){Last=Landing=Along;continue;}
        float Edge=Along;
        for(int32 Refine=0;Refine<3;++Refine)
        {
            const float Mid=(Last+Edge)*.5f;
            if(SampleTop(Mid,Candidate) && FMath::Abs(Candidate.ImpactPoint.Z-Top.ImpactPoint.Z)<2.5f)Last=Mid;else Edge=Mid;
        }
        Landing=(Near+Last)*.5f;break;
    }
    const FVector Ahead=Feet+Forward*Landing;
    Destination = FVector(Ahead.X, Ahead.Y, Top.ImpactPoint.Z + Half + 2.4f);
    // Test the full standing body, including ceiling clearance, before moving.
    return !GetWorld()->OverlapBlockingTestByChannel(Destination, FQuat::Identity, UpdatedComponent->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Radius,Half), Params);
}
void UERMovement::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
    const auto* Placer=Cast<AERCharacter>(CharacterOwner);
    if(Placer && Placer->Weapon && Placer->Weapon->bPlacingClaymore)
    {
        bProneWanted=false;bWantsToCrouch=Placer->Weapon->IsClaymorePlacementLow();
        bSprintHeld=false;Velocity=FVector::ZeroVector;Acceleration=FVector::ZeroVector;
        UpdateSprint();
        UpdatePosture(DeltaSeconds);return;
    }
    if(Acceleration.IsNearlyZero())bLadderNeedsRelease=false;
    UpdatePosture(DeltaSeconds);
    UpdateSprint();
    if(!IsTraversing() && (IsMovingOnGround() || IsFalling()) && GetWorld()->GetTimeSeconds()>=RetryAfter && CanClimbLadder())
        if(auto* Ladder=FindLadder(true))
        {
            const FVector Local=Ladder->GetActorTransform().InverseTransformPosition(UpdatedComponent->GetComponentLocation());
            FHitResult Hit;
            SafeMoveUpdatedComponent(Ladder->ClimbPosition(Local.Z-CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())-UpdatedComponent->GetComponentLocation(),UpdatedComponent->GetComponentQuat(),true,Hit);
            if(!Hit.IsValidBlockingHit()){Velocity=FVector::ZeroVector;SetMovementMode(MOVE_Custom,2);}
        }
    if (IsTraversing() || (!IsMovingOnGround() && !IsFalling()) || Acceleration.IsNearlyZero() || GetWorld()->GetTimeSeconds() < RetryAfter) return;
    if (FVector::DotProduct(Acceleration.GetSafeNormal2D(), CharacterOwner->GetActorForwardVector()) < .5f) return;
    FVector Destination; float Height;
    if (!FindLedge(Destination, Height)) return;
    TraversalStart = UpdatedComponent->GetComponentLocation(); TraversalEnd = Destination;
    TraversalElapsed = 0; bHighClimb = Height > ERServer::Get(GetWorld(),"Move.Vault")*100;
    TraversalDuration = (bHighClimb ? .75f + Height*.002f : .30f + Height*.003f)*ERServer::Get(GetWorld(),"Move.TraversalTime");
    Velocity = FVector::ZeroVector;
    SetMovementMode(MOVE_Custom, 1);
}
void UERMovement::PhysCustom(float DeltaTime, int32 Iterations)
{
    if(IsOnLadder()){PhysLadder(DeltaTime,Iterations);return;}
    if (!IsMantling()) { Super::PhysCustom(DeltaTime, Iterations); return; }
    // Simulated proxies use the server's replicated position and smoothing.
    if (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) return;
    if (TraversalDuration <= SMALL_NUMBER)
    {
        // A correction can deliver a custom mode before a local trajectory exists.
        // Never sweep to an uninitialized target; let the server correct position.
        bHighClimb = false; SetMovementMode(MOVE_Falling); return;
    }
    TraversalElapsed += DeltaTime;
    const float Alpha = FMath::Clamp(TraversalElapsed / TraversalDuration, 0.f, 1.f);
    FVector Next = TraversalStart;
    Next.Z = FMath::Lerp(TraversalStart.Z, TraversalEnd.Z, FMath::SmoothStep(0.f,.65f,Alpha));
    const float Across = FMath::SmoothStep(.65f,1.f,Alpha);
    Next.X = FMath::Lerp(TraversalStart.X, TraversalEnd.X, Across);
    Next.Y = FMath::Lerp(TraversalStart.Y, TraversalEnd.Y, Across);
    FHitResult Hit;
    SafeMoveUpdatedComponent(Next - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true, Hit);
    Velocity = FVector::ZeroVector;
    if (Hit.IsValidBlockingHit() || Alpha >= 1)
    {
        RetryAfter = GetWorld()->GetTimeSeconds() + .25;
        bHighClimb = false; TraversalDuration = 0;
        SetMovementMode(MOVE_Falling);
    }
}

double UERMovement::ServerTime() const
{
    const auto* State = GetWorld()->GetGameState();
    return State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}
bool UERMovement::IsSprinting() const { return Locomotion.SprintUntil > ServerTime(); }
float UERMovement::GetSprintRemaining() const { return FMath::Max(0.f, float(Locomotion.SprintUntil - ServerTime())); }
float UERMovement::GetRecoveryRemaining() const { return IsSprinting() ? 0.f : FMath::Max(0.f, float(Locomotion.RecoverUntil - ServerTime())); }
float UERMovement::GetStaminaFraction() const
{
    if (IsSprinting()) return FMath::Clamp(GetSprintRemaining() / float(SprintDuration()), 0.f, 1.f);
    return FMath::Clamp(1.f - GetRecoveryRemaining() / float(RecoveryDuration()), 0.f, 1.f);
}
bool UERMovement::IsChangingPosture() const
{
    const float Target = bProneWanted ? 34.f : (bWantsToCrouch ? 62.5f : 92.5f);
    return !FMath::IsNearlyEqual(Locomotion.HalfHeight, Target, .1f) ||
        !FMath::IsNearlyEqual(Locomotion.ProneBlend, bProneWanted ? 1.f : 0.f, .01f);
}
void UERMovement::UpdateSprint()
{
    const double Now = ServerTime();
    const auto* C = Cast<AERCharacter>(CharacterOwner);
    const bool Allowed = C && !C->Health->bDead && IsMovingOnGround() && !IsTraversing() &&
        !bWalkHeld && !bWantsToCrouch && !bProneWanted && Locomotion.HalfHeight >= 92.f &&
        FVector::DotProduct(Acceleration.GetSafeNormal2D(), C->GetActorForwardVector()) > .5f;
#if !UE_BUILD_SHIPPING
    if (bSprintHeld && !bPreviousSprintHeld && FParse::Param(FCommandLine::Get(), TEXT("ERMovementProbe")))
        UE_LOG(LogTemp,Display,TEXT("ER_SPRINT_REQUEST role=%d allowed=%d ground=%d walk=%d crouch=%d prone=%d half=%.1f acceleration=%s forward=%s recovery=%.1f"),
            int32(C->GetLocalRole()), Allowed, IsMovingOnGround(), bWalkHeld, bWantsToCrouch, bProneWanted, Locomotion.HalfHeight,
            *Acceleration.ToString(), *C->GetActorForwardVector().ToString(), GetRecoveryRemaining());
#endif
    // Only a fresh gesture can start a sprint, including with partially restored stamina.
    if (bSprintHeld && !bPreviousSprintHeld && GetStaminaFraction() > .001f) SprintRequestUntil = Now + .25;
    if (!bSprintHeld) SprintRequestUntil = 0;
    // The gesture can arrive one saved move before its forward acceleration.
    if (bSprintHeld && SprintRequestUntil > Now && Allowed && GetStaminaFraction() > .001f)
    {
        Locomotion.SprintUntil = Now + SprintDuration() * GetStaminaFraction();
        Locomotion.RecoverUntil = Locomotion.SprintUntil + RecoveryDuration();
        SprintRequestUntil = 0;
    }
    if (IsSprinting() && (!bSprintHeld || !Allowed))
    {
        // Include stamina already spent before this gesture; interruptions never refill it.
        const double Spent = SprintDuration() - GetSprintRemaining();
        Locomotion.SprintUntil = Now;
        Locomotion.RecoverUntil = Now + RecoveryDuration() / SprintDuration() * Spent;
    }
    bPreviousSprintHeld = bSprintHeld;
}
void UERMovement::UpdatePosture(float DeltaSeconds)
{
    if (!CharacterOwner || !UpdatedComponent || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) return;
    const auto* C = Cast<AERCharacter>(CharacterOwner);
    if ((C && C->Health->bDead) || IsTraversing()) return;
    // Enter low postures only on the floor; keep an existing posture when walking off an edge.
    if (!IsMovingOnGround() && Locomotion.HalfHeight >= 92.f) return;
    auto* Capsule = CharacterOwner->GetCapsuleComponent();
    const float Old = Capsule->GetUnscaledCapsuleHalfHeight();
    const bool EnterCrouchFirst = bProneWanted && Locomotion.ProneBlend <= 0.f && Old > 62.5f;
    const bool LowerToProne = bProneWanted && !EnterCrouchFirst;
    const float Target = LowerToProne ? 34.f : ((EnterCrouchFirst || bWantsToCrouch) ? 62.5f : 92.5f);
    const bool ProneTransition = LowerToProne || Locomotion.ProneBlend > 0;
    const float Rate = ProneTransition ? 58.5f / ERServer::Get(GetWorld(),"Move.ProneTime") : 30.f / ERServer::Get(GetWorld(),"Move.CrouchTime");
    const float Next = FMath::FInterpConstantTo(Old, Target, DeltaSeconds, Rate);
    const float Scale = Capsule->GetShapeScale();
    const FVector Shift(0, 0, (Next - Old) * Scale);
    // Reserve the complete lying footprint before lowering the body. The upright
    // capsule alone cannot detect a wall alongside the future head or feet.
    if (LowerToProne && Locomotion.ProneBlend < 1.f &&
        !HasProneClearance(UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), 1.f)) return;
    if (Next > Old)
    {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(ERPosture), false, CharacterOwner);
        // Check the eventual posture too: do not half-stand into an obstructing ceiling.
        if (GetWorld()->OverlapBlockingTestByChannel(UpdatedComponent->GetComponentLocation() + FVector(0,0,(Target-Old)*Scale),
            FQuat::Identity, UpdatedComponent->GetCollisionObjectType(),
            FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Target*Scale), Params)) return;
    }
    Capsule->SetCapsuleHalfHeight(Next, true);
    UpdatedComponent->MoveComponent(Shift, UpdatedComponent->GetComponentQuat(), false);
    Locomotion.HalfHeight = Next;
    Locomotion.ProneBlend = FMath::FInterpConstantTo(Locomotion.ProneBlend, LowerToProne ? 1.f : 0.f, DeltaSeconds, 1.f / ERServer::Get(GetWorld(),"Move.ProneTime"));
    CharacterOwner->bIsCrouched = !LowerToProne && Next < 92.f;
    bForceNextFloorCheck = true;
    ApplyPosturePresentation();
}
bool UERMovement::HasProneClearance(const FVector& Position, const FQuat& Rotation, float Blend) const
{
    if (!CharacterOwner || Blend <= 0.f) return true;
    const auto* Capsule = CharacterOwner->GetCapsuleComponent();
    const float Scale = Capsule->GetShapeScale();
    const FVector Centre = Position + Rotation.RotateVector(FVector(30.f * Blend, 0, 35.f - Capsule->GetUnscaledCapsuleHalfHeight()) * Scale);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERProneClearance), false, CharacterOwner);
    for(float Side : {-5.f,0.f,5.f})
        if(GetWorld()->OverlapBlockingTestByChannel(Centre+Rotation.RotateVector(FVector(0,Side*Blend*Scale,0)),
            Rotation * FRotator(90,0,0).Quaternion(), Capsule->GetCollisionObjectType(),
            FCollisionShape::MakeCapsule(32.f * Scale, (32.f + 83.f * Blend) * Scale), Query))return false;
    return true;
}
FRotator UERMovement::ConstrainProneRotation(const FRotator& Rotation) const
{
    if (!UpdatedComponent || Locomotion.ProneBlend <= 0.f) return Rotation;
    const FQuat Start = UpdatedComponent->GetComponentQuat(), End = Rotation.Quaternion();
    const int32 Steps = FMath::Max(1, FMath::CeilToInt32(FMath::RadiansToDegrees(Start.AngularDistance(End)) / 2.0));
    FQuat Accepted = Start;
    for (int32 I = 1; I <= Steps; ++I)
    {
        const FQuat Candidate = FQuat::Slerp(Start, End, double(I) / Steps);
        if (!HasProneClearance(UpdatedComponent->GetComponentLocation(), Candidate, Locomotion.ProneBlend)) break;
        Accepted = Candidate;
    }
    return Accepted.Rotator();
}
bool UERMovement::MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit, ETeleportType Teleport)
{
    if (!bSweep || !CharacterOwner || Teleport != ETeleportType::None)
        return Super::MoveUpdatedComponentImpl(Delta, NewRotation, bSweep, OutHit, Teleport);
    const FQuat Rotation = ConstrainProneRotation(NewRotation.Rotator()).Quaternion();
    const auto* Capsule = CharacterOwner->GetCapsuleComponent();
    const float Scale = Capsule->GetShapeScale(), Blend = Locomotion.ProneBlend;
    const FVector Centre = UpdatedComponent->GetComponentLocation() + Rotation.RotateVector(FVector(30.f * Blend,0,35.f-Capsule->GetUnscaledCapsuleHalfHeight()) * Scale);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERProneMove), false, CharacterOwner);
    FHitResult BodyHit;
    bool Blocked=false;
    const auto* Player = Cast<AERCharacter>(CharacterOwner);
    const auto* Data = Player && Player->Weapon ? Player->Weapon->Data.Get() : nullptr;
    if (Data && Data->Mesh && Data->bWorldReferencePose && !Player->IsWeaponStowed() && !Player->Health->bDead)
    {
        const FVector Forward = Rotation.GetForwardVector();
        const auto Bounds = Data->Mesh->GetBounds();
        const double MuzzleReach = FMath::Max(0.0, Bounds.Origin.X + Bounds.BoxExtent.X - Data->WorldStockContact.X) * Data->WorldWeaponScale;
        // Reserve the horizontal space occupied by the ready weapon. The box
        // begins in front of the torso and stays above the floor during sweeps.
        const double Start = 20.0 * Scale;
        const double End = FMath::Max(Start+8.0, FMath::Lerp(52.0,115.0,double(Blend))*Scale + MuzzleReach);
        const FVector WeaponCentre = UpdatedComponent->GetComponentLocation() + Forward*((Start+End)*.5)
            + FVector::UpVector*(FMath::Lerp(39.5,25.0-Capsule->GetUnscaledCapsuleHalfHeight(),double(Blend)))*Scale;
        FHitResult Cover;
        if (GetWorld()->SweepSingleByChannel(Cover, WeaponCentre, WeaponCentre+Delta, Rotation,
            Capsule->GetCollisionObjectType(), FCollisionShape::MakeBox(FVector((End-Start)*.5,18*Scale,9*Scale)), Query)
            && FMath::Abs(Cover.ImpactNormal.Z)<.5 && FVector::DotProduct(Delta,Cover.ImpactNormal)<0)
        {
            BodyHit=Cover;
            BodyHit.Location-=WeaponCentre-UpdatedComponent->GetComponentLocation();
            Blocked=true;
        }
    }
    if (Blend>0.f)
    for(float Side : {-5.f,0.f,5.f})
    {
        const FVector SideCentre=Centre+Rotation.RotateVector(FVector(0,Side*Blend*Scale,0));
        FHitResult Candidate;
        if(GetWorld()->SweepSingleByChannel(Candidate,SideCentre,SideCentre+Delta,
            Rotation*FRotator(90,0,0).Quaternion(),Capsule->GetCollisionObjectType(),
            FCollisionShape::MakeCapsule(32.f*Scale,(32.f+83.f*Blend)*Scale),Query) && (!Blocked || Candidate.Time<BodyHit.Time))
        {
            BodyHit=Candidate;
            BodyHit.Location-=SideCentre-UpdatedComponent->GetComponentLocation();
            Blocked=true;
        }
    }
    if(Blocked)
    {
        BodyHit.TraceStart=UpdatedComponent->GetComponentLocation();
        BodyHit.TraceEnd=BodyHit.TraceStart+Delta;
    }
    const float Fraction = Blocked ? FMath::Max(0.f, BodyHit.Time - .001f) : 1.f;
    FHitResult CapsuleHit;
    const bool Moved = Super::MoveUpdatedComponentImpl(Delta * Fraction, Rotation, true, &CapsuleHit, Teleport);
    CapsuleHit.Time *= Fraction;
    if (OutHit) *OutHit = CapsuleHit.bBlockingHit ? CapsuleHit : Blocked ? BodyHit : CapsuleHit;
    return Moved;
}
float UERMovement::GetEyeOffset() const
{
    // Feet stay fixed. The eye and camera share this physical offset, including transitions.
    const float H = Locomotion.HalfHeight;
    return H >= 62.5f ? FMath::GetMappedRangeValueClamped(FVector2D(62.5f,92.5f), FVector2D(50.f,77.5f), H) :
        FMath::GetMappedRangeValueClamped(FVector2D(34.f,62.5f), FVector2D(11.f,50.f), H);
}
void UERMovement::ApplyPosturePresentation()
{
    auto* C = Cast<AERCharacter>(CharacterOwner);
    if (!C) return;
    C->BaseEyeHeight = GetEyeOffset();
    C->Camera->SetRelativeLocation(FVector(0,0,C->BaseEyeHeight));
    const float Blend = Locomotion.ProneBlend;
    C->ProneHitVolume->SetCollisionEnabled(Blend > .5f && !C->Health->bDead ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    // FeetOffset seats the idle clip on the capsule bottom. Walking hovers that bottom
    // 2.15cm and the live idle stands about .85cm taller, so seat soles on the real floor.
    constexpr float FloorContact = 3.f;
    const float FeetOffset = (C->GetTeamAppearance().Mesh ? C->GetTeamAppearance().FeetOffset : -92.5f) - FloorContact;
    float ProneFloorOffset=0;
    if(const auto* SkinMesh=C->GetTeamAppearance().Mesh.Get())
        GConfig->GetFloat(TEXT("RespawnAndRage.ProneGrounding"),*SkinMesh->GetPathName(),ProneFloorOffset,GGameIni);
    const FVector MeshLocation = FMath::Lerp(FVector(0,0,FeetOffset + 92.5f - Locomotion.HalfHeight), FVector(-55,0,-18+FMath::Clamp(ProneFloorOffset,0.f,30.f)), Blend);
    const FQuat Rotation = FQuat(FVector::YAxisVector, Blend * PI * .5f) * FRotator(0,-90,0).Quaternion();
    C->GetMesh()->SetRelativeLocationAndRotation(MeshLocation, Rotation);
    C->CacheInitialMeshOffset(MeshLocation, Rotation.Rotator());
    // Layer posture over Unreal's proxy offsets instead of discarding network smoothing.
    if (!C->IsLocallyControlled() && NetworkSmoothingMode != ENetworkSmoothingMode::Disabled &&
        (C->GetLocalRole() == ROLE_SimulatedProxy || GetNetMode() == NM_ListenServer))
        SmoothClientPosition_UpdateVisuals();
}
bool UERMovement::CanDelaySendingMove(const FSavedMovePtr& NewMove)
{
    // Stationary moves otherwise wait up to 83ms, and the shot would use an older view.
    const bool bDelay=!bSendMoveNow && Super::CanDelaySendingMove(NewMove);
    bSendMoveNow=false;
    return bDelay;
}
void UERMovement::OnRep_Locomotion()
{
    if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
        CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(Locomotion.HalfHeight, true);
    ApplyPosturePresentation();
}
void UERMovement::SimulateMovement(float DeltaSeconds)
{
    Super::SimulateMovement(DeltaSeconds);
    ApplyPosturePresentation();
}
bool UERMovement::DoJump(bool bReplayingMoves, float DeltaTime)
{
    const auto* C=Cast<AERCharacter>(CharacterOwner);
    if(C && C->Weapon && C->Weapon->bPlacingClaymore)return false;
    if(IsOnLadder())
    {
        const auto* Ladder=FindLadder();
        const FVector Out=Ladder ? Ladder->GetActorForwardVector():-CharacterOwner->GetActorForwardVector();
        bLadderNeedsRelease=true;SetMovementMode(MOVE_Falling);Velocity=Out*180+FVector(0,0,180);RetryAfter=GetWorld()->GetTimeSeconds()+.6;return true;
    }
    return Locomotion.HalfHeight >= 92.f && !bProneWanted && !IsChangingPosture() && Super::DoJump(bReplayingMoves, DeltaTime);
}
void UERMovement::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UERMovement, Locomotion);
}

bool UERMovement::ClientUpdatePositionAfterServerUpdate()
{
    // Unreal restores its own jump/crouch inputs after replay. Preserve our current
    // physical key intents too: an older saved move must not release a newly held key.
    const bool Walk = bWalkHeld, Climb = bClimbHeld, Sprint = bSprintHeld, Prone = bProneWanted;
    const bool PreviousSprint = bPreviousSprintHeld;
    const double Request = SprintRequestUntil, SprintUntil = Locomotion.SprintUntil, RecoverUntil = Locomotion.RecoverUntil;
    const bool Updated = Super::ClientUpdatePositionAfterServerUpdate();
    bWalkHeld = Walk; bClimbHeld = Climb; bSprintHeld = Sprint; bProneWanted = Prone;
    bPreviousSprintHeld = PreviousSprint; SprintRequestUntil = Request;
    Locomotion.SprintUntil = SprintUntil; Locomotion.RecoverUntil = RecoverUntil;
    return Updated;
}

void UERMovement::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
    Super::OnMovementModeChanged(PreviousMovementMode,PreviousCustomMode);
    if(auto* C=Cast<AERCharacter>(CharacterOwner); C && C->HasAuthority() && C->IsWeaponStowed())
        C->Weapon->Cancel();
}
bool UERMovement::CanClimbLadder() const
{
    const auto* C=Cast<AERCharacter>(CharacterOwner);
    return C && !C->Health->bDead && Locomotion.HalfHeight>=92 && !bProneWanted && !bWantsToCrouch &&
        !IsChangingPosture();
}
float UERMovement::LadderSpeed() const
{
    const auto* C=Cast<AERCharacter>(CharacterOwner);
    return ERServer::Get(GetWorld(),C && C->CanHoldWeaponOnLadder()?FName("Move.LadderLight"):FName("Move.LadderHeavy"))/.036f;
}
AERLadder* UERMovement::FindLadder(bool Entry) const
{
    if(!CharacterOwner || !UpdatedComponent || (Entry && bLadderNeedsRelease))return nullptr;
    const float Half=CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    AERLadder* Best=nullptr;float Distance=MAX_flt;
    for(TActorIterator<AERLadder> It(GetWorld());It;++It)
    {
        if(It->IsActorBeingDestroyed())continue;
        const FVector P=It->GetActorTransform().InverseTransformPosition(UpdatedComponent->GetComponentLocation());
        const float Feet=P.Z-Half;
        const bool FromRoof=Entry && FMath::Abs(Feet-It->DeckHeight)<15 && P.X>=-130 && P.X<30;
        if(FMath::Abs(P.Y)>60 || (P.X<30 && !FromRoof) || P.X>115 || Feet< -35 || Feet>It->Height+8)continue;
        const FVector Toward=FromRoof ? It->GetActorForwardVector():-It->GetActorForwardVector();
        if(Entry && (Acceleration.IsNearlyZero() || FVector::DotProduct(Acceleration.GetSafeNormal2D(),Toward)<.5f))continue;
        if(!It->HasRung(Feet))continue;
        const float D=FMath::Square(P.X-55)+FMath::Square(P.Y);
        if(D<Distance){Distance=D;Best=*It;}
    }
    return Best;
}
void UERMovement::PhysLadder(float DeltaTime,int32 Iterations)
{
    if(CharacterOwner->GetLocalRole()==ROLE_SimulatedProxy)return;
    auto* Ladder=FindLadder();
    if(!Ladder || !CanClimbLadder())
    {bLadderNeedsRelease=true;SetMovementMode(MOVE_Falling);RetryAfter=GetWorld()->GetTimeSeconds()+.35;StartNewPhysics(DeltaTime,Iterations);return;}
    const float Half=CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Local=Ladder->GetActorTransform().InverseTransformPosition(UpdatedComponent->GetComponentLocation());
    const float Input=FMath::Clamp(FVector::DotProduct(Acceleration,-Ladder->GetActorForwardVector())/GetMaxAcceleration(),-1.f,1.f);
    const float Feet=Local.Z-Half,Next=Feet+Input*LadderSpeed()*DeltaTime;
    FVector Target=Ladder->ClimbPosition(FMath::Clamp(Next,0.f,Ladder->Height),Half);
    // The rails project above the parapet. Sweep across only after clearing it.
    if(Next>=Ladder->Height && Input>0)
    {
        Target=Ladder->Landing(Half);Target.Z=UpdatedComponent->GetComponentLocation().Z;
        TraversalStart=UpdatedComponent->GetComponentLocation();TraversalEnd=Target;
        TraversalElapsed=0;TraversalDuration=.45f;bHighClimb=false;
        Velocity=FVector::ZeroVector;SetMovementMode(MOVE_Custom,3);
        return;
    }
    FHitResult Hit;const FVector Before=UpdatedComponent->GetComponentLocation();
    SafeMoveUpdatedComponent(Target-Before,UpdatedComponent->GetComponentQuat(),true,Hit);
    Velocity=(UpdatedComponent->GetComponentLocation()-Before)/FMath::Max(DeltaTime,SMALL_NUMBER);
    if(Next<=0 && Input<0){bLadderNeedsRelease=true;SetMovementMode(MOVE_Falling);Velocity=FVector::ZeroVector;RetryAfter=GetWorld()->GetTimeSeconds()+.5;}
}

void UERMovement::PhysFalling(float DeltaTime,int32 Iterations)
{
    // UE integrates velocity for the whole substep before the impact sweep.
    // Bound that velocity by energy at the actual impact position, so a long
    // frame cannot turn a safe drop into damage or inflate a six-meter fall.
    TGuardValue<bool> Measuring(bMeasuringFall,true);
    TGuardValue<float> Height(FallFrameZ,UpdatedComponent->GetComponentLocation().Z);
    TGuardValue<float> Speed(FallFrameVelocityZ,Velocity.Z);
    Super::PhysFalling(DeltaTime,Iterations);
}
float UERMovement::LandingSpeed() const
{
    const float Down=FMath::Max(0.f,-Velocity.Z);
    if(!bMeasuringFall || !UpdatedComponent)return Down;
    const float Energy=FMath::Square(FallFrameVelocityZ)+2*FMath::Abs(GetGravityZ())*(FallFrameZ-UpdatedComponent->GetComponentLocation().Z);
    return FMath::Min(Down,FMath::Sqrt(FMath::Max(0.f,Energy)));
}
