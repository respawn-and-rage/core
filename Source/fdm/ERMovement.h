#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ERFireInput.h"
#include "ERMovement.generated.h"

struct FERNetworkMoveData : FCharacterNetworkMoveData
{
    uint16 FireSequence=0;
    bool FireHeld=false;
    FERFireWeaponBinding FireWeapon;
    virtual void ClientFillNetworkMoveData(const FSavedMove_Character& Move,ENetworkMoveType Type) override;
    virtual bool Serialize(UCharacterMovementComponent& Movement,FArchive& Ar,UPackageMap* Map,ENetworkMoveType Type) override;
};
struct FERNetworkMoveContainer : FCharacterNetworkMoveDataContainer
{
    FERNetworkMoveData Moves[3];
    FERNetworkMoveContainer() {NewMoveData=&Moves[0];PendingMoveData=&Moves[1];OldMoveData=&Moves[2];}
};

USTRUCT()
struct FERLocomotionState
{
    GENERATED_BODY()
    UPROPERTY() float HalfHeight = 92.5f;
    UPROPERTY() float ProneBlend = 0;
    UPROPERTY() double SprintUntil = 0;
    UPROPERTY() double RecoverUntil = 0;
};

// Intent travels with Unreal's saved moves, including correction/replay.
UCLASS()
class FDM_API UERMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    UERMovement();
    bool bWalkHeld = false;
    bool bLadderNeedsRelease = false;
    bool bClimbHeld = false;
    bool bHighClimb = false;
    bool bSprintHeld = false;
    bool bProneWanted = false;
    bool bPreviousSprintHeld = false;
    double SprintRequestUntil = 0;
    UPROPERTY(ReplicatedUsing=OnRep_Locomotion) FERLocomotionState Locomotion;
    void ApplyServerSettings();
    double SprintDuration() const;
    double RecoveryDuration() const;
    static constexpr double MaxSprintDuration = 15.0;
    static constexpr double RecoveryRatio = 20.0 / MaxSprintDuration;
    bool IsSprinting() const;
    bool IsProne() const { return Locomotion.ProneBlend > .5f; }
    bool IsChangingPosture() const;
    float GetSprintRemaining() const;
    float GetRecoveryRemaining() const;
    float GetStaminaFraction() const;
    float GetEyeOffset() const;
    void ApplyPosturePresentation();
    bool HasProneClearance(const FVector& Position, const FQuat& Rotation, float Blend) const;
    FRotator ConstrainProneRotation(const FRotator& Rotation) const;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Props) const override;
    virtual void Crouch(bool bClientSimulation = false) override {}
    virtual void UnCrouch(bool bClientSimulation = false) override {}
    virtual void UpdateCharacterStateAfterMovement(float DeltaSeconds) override {}
    virtual void SimulateMovement(float DeltaSeconds) override;
    virtual bool ClientUpdatePositionAfterServerUpdate() override;
    // Owner: carry this frame's view in the same packet as a trigger pull.
    void SendMoveThisFrame() {bSendMoveNow=true;}
    FERFireInputState FireInput;
    FERFireWeaponBinding FireWeapon;
    void SetFireInput(bool Held,const FERFireWeaponBinding& SelectedWeapon);
    // Receipt clock is server-owned; only a move accepted by the engine updates it.
    double LastAcceptedMoveServerTime=-1;
    float LastAcceptedMoveTimeStamp=0;
    virtual void MoveAutonomous(float ClientTimeStamp,float DeltaTime,uint8 Flags,const FVector& NewAccel) override;
    virtual void ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData) override;
    virtual void ServerMove_HandleMoveData(const FCharacterNetworkMoveDataContainer& MoveData) override;
    virtual bool CanAttemptJump() const override {return !IsPreMatchFrozen() && (IsOnLadder() || Super::CanAttemptJump());}
    // Replicated match phase: owner prediction and authority agree on the freeze.
    bool IsPreMatchFrozen() const;
    virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
    UFUNCTION() void OnRep_Locomotion();
    virtual float GetMaxSpeed() const override;
    virtual void UpdateFromCompressedFlags(uint8 Flags) override;
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
    virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
    virtual void PhysFalling(float DeltaTime,int32 Iterations) override;
    float LandingSpeed() const;
    virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
    bool FindLedge(FVector& Destination, float& Height, double ApproachReach = 0.0) const;
    bool IsMantling() const { return MovementMode == MOVE_Custom && (CustomMovementMode == 1 || CustomMovementMode == 3); }
    bool IsOnLadder() const { return MovementMode == MOVE_Custom && CustomMovementMode == 2; }
    bool IsTraversing() const { return IsMantling() || IsOnLadder(); }
    virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
    bool CanClimbLadder() const;
    bool IsWeaponAtLadder() const;
    bool ShouldLowerWeaponForLedge() const;
    class AERLadder* FindLadder(bool Entry=false) const;
    float LadderSpeed() const;
    void PhysLadder(float DeltaTime,int32 Iterations);
// Saved moves restore this state when Unreal replays a corrected prediction.
    FVector TraversalStart = FVector::ZeroVector, TraversalEnd = FVector::ZeroVector;
    float TraversalElapsed = 0, TraversalDuration = 0;
    double RetryAfter = 0;
protected:
    virtual bool MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) override;
    virtual bool CanDelaySendingMove(const FSavedMovePtr& NewMove) override;
private:
    FERNetworkMoveContainer NetworkMoveStorage;
    FERFireInputState AcceptedFireInput;
    bool bAcceptedNetworkMove=false;
    TWeakObjectPtr<class USkeletalMesh> GroundingMesh;
    float CachedProneFloorOffset = 0;
    bool bSendMoveNow = false;
    bool bMeasuringFall=false;
    float FallFrameZ=0,FallFrameVelocityZ=0;
    double ServerTime() const;
    void UpdatePosture(float DeltaSeconds);
    void UpdateSprint();
};
