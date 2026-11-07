#include "ERCellNetShard.h"
#include "ERBlockRoom.h"
#include "Net/UnrealNetwork.h"

AERCellNetShard::AERCellNetShard()
{
    bReplicates = true; bAlwaysRelevant = true; SetNetUpdateFrequency(20); PrimaryActorTick.bCanEverTick = false;
}
bool AERCellNetShard::SetChunk(int32 Chunk, uint32 Revision, TArray<uint8>&& Mask, bool Finalize)
{
    bPayloadVerified=false;
    FERCellChunkItem* Item = Chunks.Items.FindByPredicate([Chunk](const auto& I) { return I.Chunk == Chunk; });
    if (Item && Item->Revision == Revision && Item->Mask == Mask) return false;
    if (!Item) { Item = &Chunks.Items.AddDefaulted_GetRef(); Item->Chunk = Chunk; }
    Item->Revision = Revision; Item->Mask = MoveTemp(Mask);
    Chunks.MarkItemDirty(*Item);
    if(Finalize)FinalizePayload();
    return true;
}
void AERCellNetShard::FinalizePayload()
{
    bPayloadVerified=false;
    Header.ChunkCount=Chunks.Items.Num();Header.PayloadDigest=ComputePayloadDigest();
}
uint32 AERCellNetShard::ComputePayloadDigest() const
{
    // Fast-array order can differ between peers. The header and the complete
    // array must describe the same transaction before any mask is committed.
    TArray<int32> Order;for(int32 I=0;I<Chunks.Items.Num();++I)Order.Add(I);
    Order.Sort([&](int32 A,int32 B){return Chunks.Items[A].Chunk<Chunks.Items[B].Chunk;});
    uint32 H=0;
    for(int32 I:Order)
    {
        const auto& Item=Chunks.Items[I];H=HashCombineFast(H,GetTypeHash(Item.Chunk));H=HashCombineFast(H,GetTypeHash(Item.Revision));
        H=FCrc::MemCrc32(Item.Mask.GetData(),Item.Mask.Num(),H);
    }
    return H;
}
bool AERCellNetShard::IsPayloadCurrent() const
{
    if(Header.ChunkCount<0 || Header.ChunkCount>Capacity || Header.ChunkCount!=Chunks.Items.Num())return false;
    if(bPayloadVerified && VerifiedEpoch==Header.Epoch && VerifiedIndex==Header.Index && VerifiedVersion==Header.Version &&
        VerifiedDigest==Header.PayloadDigest && VerifiedArrayKey==Chunks.ArrayReplicationKey && VerifiedCount==Chunks.Items.Num())return true;
    if(Header.PayloadDigest!=ComputePayloadDigest())return false;
    VerifiedEpoch=Header.Epoch;VerifiedIndex=Header.Index;VerifiedVersion=Header.Version;VerifiedDigest=Header.PayloadDigest;
    VerifiedArrayKey=Chunks.ArrayReplicationKey;VerifiedCount=Chunks.Items.Num();bPayloadVerified=true;return true;
}
SIZE_T AERCellNetShard::GetPayloadBytes() const
{
    SIZE_T Bytes = Chunks.Items.GetAllocatedSize();
    for (const auto& I : Chunks.Items) Bytes += I.Mask.GetAllocatedSize();
    return Bytes;
}
void AERCellNetShard::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AERCellNetShard, Room); DOREPLIFETIME(AERCellNetShard, Header); DOREPLIFETIME(AERCellNetShard, Chunks);
}
void AERCellNetShard::OnRep_State()
{
    bPayloadVerified=false;
    if (Room) Room->ReceiveCellShard(this);
}
