#pragma once
#include "CoreMinimal.h"
#include "ERBlockRoom.h"
#include "ERCellNetShard.h"
#include "ERCellRecovery.generated.h"

// A finite immutable checkpoint for one connection, independent of live deltas.
UCLASS(NotBlueprintable)
class FDM_API AERCellRecoveryManifest : public AActor
{
    GENERATED_BODY()
public:
    AERCellRecoveryManifest();
    UPROPERTY(ReplicatedUsing=OnRep_State) TObjectPtr<AERBlockRoom> Room;
    UPROPERTY(ReplicatedUsing=OnRep_State) uint32 RecoveryId=0;
    UPROPERTY(ReplicatedUsing=OnRep_State) FERBlockNetworkDescriptor Descriptor;
    UPROPERTY(ReplicatedUsing=OnRep_State) TArray<FERCellShardHeader> Pages;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UFUNCTION() void OnRep_State();
};

UCLASS(NotBlueprintable)
class FDM_API AERCellRecoveryShard : public AERCellNetShard
{
    GENERATED_BODY()
public:
    AERCellRecoveryShard();
    UPROPERTY(ReplicatedUsing=OnRep_Recovery) uint32 RecoveryId=0;
    virtual bool IsRecoveryTransfer() const override {return true;}
    virtual uint32 GetRecoveryId() const override {return RecoveryId;}
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UFUNCTION() void OnRep_Recovery();
};
