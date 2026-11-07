#include "ERBlockRoom.h"
#include "ERCellRecovery.h"
#include "ERPlayerController.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace
{
constexpr SIZE_T RecoveryClientBytes=32*1024*1024,RecoveryMapBytes=128*1024*1024;
constexpr int32 RecoveryMaxClients=64;
constexpr double RecoveryLifetime=180;
uint64 PayloadKey(uint32 RecoveryId,int32 Page){return (uint64(RecoveryId)<<32)|uint32(Page);}
}

void AERBlockRoom::FailCellSynchronization(const TCHAR* Reason)
{
    bNetworkError=true;bMapReady=false;SetMapCollision(false);
    UE_LOG(LogTemp,Error,TEXT("%s"),Reason);
}

void AERBlockRoom::RecordVerifiedPayload(const AERCellNetShard* Shard)
{
    const uint64 Key=PayloadKey(Shard->GetRecoveryId(),Shard->Header.Index);
    const auto* Previous=VerifiedPayloadVersions.Find(Key);
    if(!Previous || *Previous!=Shard->Header.Version)
    {VerifiedPayloadVersions.Add(Key,Shard->Header.Version);++SyncProgressSerial;}
}

FERCellSyncStatus AERBlockRoom::GetCellSyncStatus() const
{
    FERCellSyncStatus Status;
    Status.Epoch=Network.Epoch;Status.TargetSequence=FMath::Max(Network.Sequence,AppliedSequence);Status.AppliedSequence=HasAuthority() ? Network.Sequence:AppliedSequence;
    Status.ProgressSerial=SyncProgressSerial;Status.ExpectedPages=SyncExpectedPages;Status.VerifiedPages=SyncVerifiedPages;
    Status.Ready=IsMapReady();Status.Error=bNetworkError;Status.Recovering=RecoveryManifest.IsValid();Status.LastApplyMs=LastCellApplyMs;
    Status.RecoveryId=CompletedRecoveryId;if(const auto* Manifest=RecoveryManifest.Get())Status.RecoveryId=Manifest->RecoveryId;
    const double LastProgress=LastCellApplyAt>0 ? LastCellApplyAt:SyncStartedAt;
    Status.LastAppliedAgeSeconds=LastProgress>0 ? FMath::Max(0.,FPlatformTime::Seconds()-LastProgress):0;
    return Status;
}

void AERBlockRoom::ClearCellRecoveries()
{
    if(GetWorld())GetWorldTimerManager().ClearTimer(RecoveryCleanupTimer);
    for(const auto& Transfer:RecoveryTransfers)for(const auto& Actor:Transfer.Actors)if(Actor.IsValid())Actor->Destroy();
    RecoveryTransfers.Reset();RecoveryManifest.Reset();RecoveryShards.Reset();VerifiedPayloadVersions.Reset();
    CompletedRecoveryId=0;SyncExpectedPages=SyncVerifiedPages=0;LastCellApplyAt=0;LastCellApplyMs=0;++SyncProgressSerial;
}

void AERBlockRoom::CleanupCellRecoveries()
{
    const double Now=FPlatformTime::Seconds();
    for(int32 I=RecoveryTransfers.Num()-1;I>=0;--I)
    {
        const auto& Transfer=RecoveryTransfers[I];
        if(Transfer.Player.IsValid() && Now<Transfer.ExpiresAt)continue;
        for(const auto& Actor:Transfer.Actors)if(Actor.IsValid())Actor->Destroy();
        RecoveryTransfers.RemoveAtSwap(I,EAllowShrinking::No);
    }
    if(RecoveryTransfers.IsEmpty() && GetWorld())GetWorldTimerManager().ClearTimer(RecoveryCleanupTimer);
}

bool AERBlockRoom::StartCellRecovery(AERPlayerController* Player,uint32 Epoch,FString& Error)
{
    Error.Reset();
    if(!CanMutateBlocks() || !bGenerationValid || !Player || Player->GetWorld()!=GetWorld() || Epoch==0 || Epoch!=Network.Epoch)
    {Error=TEXT("The map changed before synchronization could start.");return false;}
    CleanupCellRecoveries();
    for(const auto& Transfer:RecoveryTransfers)if(Transfer.Player==Player)return true;
    SIZE_T UsedBytes=0;for(const auto& Transfer:RecoveryTransfers)UsedBytes+=Transfer.Bytes;
    SIZE_T RequiredBytes=sizeof(FERBlockNetworkDescriptor)+Network.PageVersions.GetAllocatedSize()+AuthorityShards.Num()*sizeof(FERCellShardHeader);
    for(const auto& Weak:AuthorityShards)
    {
        const auto* Shard=Weak.Get();
        if(!Shard || Shard->Header.Epoch!=Epoch || !Network.PageVersions.IsValidIndex(Shard->Header.Index) || Shard->Header.Version!=Network.PageVersions[Shard->Header.Index])
        {Error=TEXT("The map checkpoint is not available yet.");return false;}
        RequiredBytes+=Shard->GetPayloadBytes();
        if(RequiredBytes>RecoveryClientBytes){Error=TEXT("This map checkpoint exceeds the synchronization budget.");return false;}
    }
    if(RecoveryTransfers.Num()>=RecoveryMaxClients || UsedBytes+RequiredBytes>RecoveryMapBytes)
    {Error=TEXT("The server is synchronizing other players. Please retry shortly.");return false;}
    FRecoveryTransfer Transfer;Transfer.Player=Player;Transfer.Id=NextRecoveryId++;if(Transfer.Id==0)Transfer.Id=NextRecoveryId++;
    Transfer.Bytes=RequiredBytes;Transfer.ExpiresAt=FPlatformTime::Seconds()+RecoveryLifetime;
    FActorSpawnParameters Params;Params.Owner=Player;
    auto* Manifest=GetWorld()->SpawnActor<AERCellRecoveryManifest>(AERCellRecoveryManifest::StaticClass(),FTransform::Identity,Params);
    if(!Manifest){Error=TEXT("The server could not create a map checkpoint.");return false;}
    Transfer.Actors.Add(Manifest);Manifest->Room=this;Manifest->RecoveryId=Transfer.Id;Manifest->Descriptor=Network;
    Manifest->Pages.Reserve(AuthorityShards.Num());
    for(const auto& Weak:AuthorityShards)
    {
        const auto* Source=Weak.Get();Manifest->Pages.Add(Source->Header);
        if(Source->Header.ChunkCount==0)continue;
        auto* Shard=GetWorld()->SpawnActor<AERCellRecoveryShard>(AERCellRecoveryShard::StaticClass(),FTransform::Identity,Params);
        if(!Shard)
        {
            for(const auto& Actor:Transfer.Actors)if(Actor.IsValid())Actor->Destroy();
            Error=TEXT("The server could not allocate a map checkpoint.");return false;
        }
        Transfer.Actors.Add(Shard);Shard->Room=this;Shard->RecoveryId=Transfer.Id;Shard->Header=Source->Header;
        Shard->Chunks.Items.Reserve(Source->Chunks.Items.Num());
        for(const auto& Item:Source->Chunks.Items)
        {
            auto& Copy=Shard->Chunks.Items.AddDefaulted_GetRef();Copy.Chunk=Item.Chunk;Copy.Revision=Item.Revision;Copy.Mask=Item.Mask;
            Shard->Chunks.MarkItemDirty(Copy);
        }
        Shard->SetLifeSpan(float(RecoveryLifetime));Shard->ForceNetUpdate();
    }
    Manifest->SetLifeSpan(float(RecoveryLifetime));Manifest->ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("ER_CELL_RECOVERY_START id=%u epoch=%u sequence=%u bytes=%llu pages=%d"),Transfer.Id,Epoch,Network.Sequence,uint64(RequiredBytes),Manifest->Pages.Num());
    RecoveryTransfers.Add(MoveTemp(Transfer));
    if(!GetWorldTimerManager().IsTimerActive(RecoveryCleanupTimer))GetWorldTimerManager().SetTimer(RecoveryCleanupTimer,this,&AERBlockRoom::CleanupCellRecoveries,1.f,true);
    return true;
}

bool AERBlockRoom::AcknowledgeCellRecovery(AERPlayerController* Player,uint32 RecoveryId)
{
    if(!HasAuthority() || !Player || Player->GetWorld()!=GetWorld() || RecoveryId==0)return false;
    CleanupCellRecoveries();
    bool Acknowledged=false;
    for(int32 I=0;I<RecoveryTransfers.Num();++I)if(RecoveryTransfers[I].Player==Player && RecoveryTransfers[I].Id==RecoveryId)
    {
        for(const auto& Actor:RecoveryTransfers[I].Actors)if(Actor.IsValid())Actor->Destroy();
        RecoveryTransfers.RemoveAtSwap(I,EAllowShrinking::No);Acknowledged=true;break;
    }
    CleanupCellRecoveries();
    return Acknowledged;
}

void AERBlockRoom::ReceiveCellRecovery(AERCellRecoveryManifest* Manifest)
{
    if(HasAuthority() || !Manifest || Manifest->Room!=this || Manifest->RecoveryId==0 || Manifest->RecoveryId<=CompletedRecoveryId ||
        Manifest->Descriptor.Epoch!=Network.Epoch || Manifest->Descriptor.Sequence==0 || Manifest->Pages.Num()!=Manifest->Descriptor.PageVersions.Num())return;
    if(const auto* Current=RecoveryManifest.Get();Current && Current->RecoveryId>Manifest->RecoveryId)return;
    if(Manifest->Descriptor.Protocol!=Network.Protocol || Manifest->Descriptor.LayoutDigest!=Network.LayoutDigest ||
        Manifest->Descriptor.BlockCount!=Network.BlockCount || Manifest->Pages.Num()!=Network.PageVersions.Num())
    {FailCellSynchronization(TEXT("ER_BLOCK_NET_INCOMPATIBLE recovery descriptor"));return;}
    for(int32 I=0;I<Manifest->Pages.Num();++I)
    {
        const auto& Header=Manifest->Pages[I];
        if(Header.Epoch!=Manifest->Descriptor.Epoch || Header.Index!=I || Header.Version!=Manifest->Descriptor.PageVersions[I] ||
            Header.ChunkCount<0 || Header.ChunkCount>AERCellNetShard::Capacity || (Header.ChunkCount==0 && Header.PayloadDigest!=0))return;
    }
    if(RecoveryManifest.Get()!=Manifest)++SyncProgressSerial;
    RecoveryManifest=Manifest;
    for(auto It=RecoveryShards.CreateIterator();It;++It)if(uint32(It.Key()>>32)<Manifest->RecoveryId)It.RemoveCurrent();
    TryApplyCellRecovery();
}

void AERBlockRoom::TryApplyCellRecovery()
{
    auto* Manifest=RecoveryManifest.Get();
    if(HasAuthority() || !Manifest || BuiltEpoch==0 || BuiltEpoch!=Network.Epoch || Manifest->Descriptor.Epoch!=BuiltEpoch)return;
    const uint32 Id=Manifest->RecoveryId;
    // Live state may overtake a pinned checkpoint, then fail on a later live
    // transaction. Discard/ack the obsolete transfer without clearing that
    // error, allowing the bounded retry to request a newer repair checkpoint.
    const bool Obsolete=Manifest->Descriptor.Sequence<AppliedSequence;
    bool Complete=Obsolete || (Manifest->Descriptor.Sequence==AppliedSequence && !bNetworkError);
    const bool Committed=!Complete;
    if(!Complete)
    {
        TMap<int32,TWeakObjectPtr<AERCellNetShard>> Pages;
        for(int32 I=0;I<Manifest->Pages.Num();++I)
        {
            const auto* Found=RecoveryShards.Find(PayloadKey(Id,I));
            if(Found)Pages.Add(I,*Found);
        }
        Complete=ApplyCellSnapshot(Manifest->Descriptor,Pages,true,&Manifest->Pages);
    }
    if(!Complete)return;
    CompletedRecoveryId=Id;RecoveryManifest.Reset();
    for(auto It=RecoveryShards.CreateIterator();It;++It)if(uint32(It.Key()>>32)<=Id)It.RemoveCurrent();
    for(auto It=VerifiedPayloadVersions.CreateIterator();It;++It)if(uint32(It.Key()>>32)!=0)It.RemoveCurrent();
    UE_LOG(LogTemp,Display,TEXT("ER_CELL_RECOVERY_APPLIED id=%u epoch=%u sequence=%u live=%u apply_ms=%.2f committed=%d obsolete=%d error=%d"),Id,BuiltEpoch,AppliedSequence,Network.Sequence,LastCellApplyMs,Committed,Obsolete,bNetworkError);
    if(auto* Player=Cast<AERPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(),0)))Player->NotifyCellRecoveryApplied(this,Id);
    TryApplyCellNetwork();
}
