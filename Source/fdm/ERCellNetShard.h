#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "ERCellNetShard.generated.h"

// Alive mask of one modified chunk (run-length). Intact chunks never appear:
// clients rebuild them from the deterministic layout and its fingerprint.
USTRUCT()
struct FERCellChunkItem : public FFastArraySerializerItem
{
    GENERATED_BODY()
    UPROPERTY() int32 Chunk = 0;
    UPROPERTY() uint32 Revision = 0;
    UPROPERTY() TArray<uint8> Mask;
};
USTRUCT()
struct FERCellChunkArray : public FFastArraySerializer
{
    GENERATED_BODY()
    UPROPERTY() TArray<FERCellChunkItem> Items;
    bool NetDeltaSerialize(FNetDeltaSerializeInfo& Parms)
    { return FFastArraySerializer::FastArrayDeltaSerialize<FERCellChunkItem, FERCellChunkArray>(Items, Parms, *this); }
};
template<> struct TStructOpsTypeTraits<FERCellChunkArray> : public TStructOpsTypeTraitsBase2<FERCellChunkArray> { enum { WithNetDeltaSerializer = true }; };
USTRUCT()
struct FERCellShardHeader
{
    GENERATED_BODY()
    UPROPERTY() uint32 Epoch = 0;
    UPROPERTY() int32 Index = 0;
    UPROPERTY() uint32 Version = 0;
    UPROPERTY() int32 ChunkCount = 0;
    UPROPERTY() uint32 PayloadDigest = 0;
};
// One bounded channel for up to Capacity consecutive chunks; the room's
// descriptor manifest lists every shard version before clients apply anything.
UCLASS(NotBlueprintable)
class FDM_API AERCellNetShard : public AActor
{
    GENERATED_BODY()
public:
    static constexpr int32 Capacity = 128;
    AERCellNetShard();
    virtual bool IsRecoveryTransfer() const {return false;}
    virtual uint32 GetRecoveryId() const {return 0;}
    UPROPERTY(ReplicatedUsing=OnRep_State) TObjectPtr<class AERBlockRoom> Room;
    UPROPERTY(ReplicatedUsing=OnRep_State) FERCellShardHeader Header;
    UPROPERTY(ReplicatedUsing=OnRep_State) FERCellChunkArray Chunks;
    // Authority: replace or add the chunk's mask; returns true if it changed.
    bool SetChunk(int32 Chunk, uint32 Revision, TArray<uint8>&& Mask, bool Finalize = true);
    // Finalize once after all writes belonging to one manifest version.
    void FinalizePayload();
    uint32 ComputePayloadDigest() const;
    bool IsPayloadCurrent() const;
    const FERCellChunkItem* FindChunk(int32 Chunk) const { return Chunks.Items.FindByPredicate([Chunk](const auto& I) { return I.Chunk == Chunk; }); }
    SIZE_T GetPayloadBytes() const;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UFUNCTION() void OnRep_State();
private:
    mutable bool bPayloadVerified=false;
    mutable uint32 VerifiedEpoch=0,VerifiedVersion=0,VerifiedDigest=0;
    mutable int32 VerifiedIndex=INDEX_NONE,VerifiedArrayKey=INDEX_NONE,VerifiedCount=0;
};
