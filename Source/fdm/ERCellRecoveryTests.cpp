#include "ERBlockRoom.h"
#include "ERCellRecovery.h"
#include "ERPlayerController.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERCellRecoveryTest,"EclipseRecoil.Destruction.Cells.RecoveryCheckpoint",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERCellRecoveryTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues Values;Values.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);if(!World)return false;
    auto* Server=World->SpawnActor<AERBlockRoom>();
    if(!TestTrue(TEXT("recovery fixture builds"),Server->Regenerate())){World->DestroyWorld(false);return false;}
    Server->PublishCellGeneration();
    auto* Player=World->SpawnActor<AERPlayerController>();auto* Other=World->SpawnActor<AERPlayerController>();
    auto* Client=World->SpawnActor<AERBlockRoom>();Client->SetRole(ROLE_SimulatedProxy);Client->bNetworkBegunPlay=true;Client->Network=Server->Network;Client->OnRep_Network();
    TArray<AERCellNetShard*> LivePages;
    for(const auto& Weak:Server->AuthorityShards)
    {
        auto* Page=World->SpawnActor<AERCellNetShard>();Page->Room=Client;Page->Header=Weak->Header;LivePages.Add(Page);Client->ReceiveCellShard(Page);
    }
    TestTrue(TEXT("initial live baseline is ready"),Client->IsMapReady());
    auto Publish=[&]()
    {
        TSet<int32> Dirty;
        for(int32 C=0;C<Server->CellWorld.GetChunks().Num();++C)if(Server->CellWorld.GetChunks()[C].Revision)
        {
            TArray<uint8> Mask;Server->CellWorld.EncodeChunk(C,Mask);const int32 Page=C/AERCellNetShard::Capacity;
            Server->AuthorityShards[Page]->SetChunk(C,Server->CellWorld.GetChunks()[C].Revision,MoveTemp(Mask),false);Dirty.Add(Page);
        }
        for(int32 I:Dirty){auto* Page=Server->AuthorityShards[I].Get();Page->FinalizePayload();++Page->Header.Version;Server->Network.PageVersions[I]=Page->Header.Version;}
        ++Server->Network.Sequence;
    };
    auto ServerManifest=[&]()
    {
        for(const auto& Actor:Server->RecoveryTransfers.Last().Actors)if(auto* Manifest=Cast<AERCellRecoveryManifest>(Actor.Get()))return Manifest;
        return static_cast<AERCellRecoveryManifest*>(nullptr);
    };
    auto Deliver=[&](AERCellRecoveryManifest* Source,bool CheckIncomplete,AERBlockRoom* Destination=nullptr)
    {
        auto* Target=Destination ? Destination:Client;
        auto* Manifest=World->SpawnActor<AERCellRecoveryManifest>();Manifest->Room=Target;Manifest->RecoveryId=Source->RecoveryId;
        Manifest->Descriptor=Source->Descriptor;Manifest->Pages=Source->Pages;
        const uint32 Before=Target->GetStateDigest();Target->ReceiveCellRecovery(Manifest);
        if(CheckIncomplete)TestEqual(TEXT("manifest without masks never commits"),Target->GetStateDigest(),Before);
        TArray<TPair<AERCellRecoveryShard*,AERCellRecoveryShard*>> Pending;
        for(const auto& Actor:Server->RecoveryTransfers.Last().Actors)if(auto* Page=Cast<AERCellRecoveryShard>(Actor.Get()))
        {
            TestTrue(TEXT("checkpoint shards are owner-only"),Page->bOnlyRelevantToOwner && !Page->bAlwaysRelevant && Page->GetOwner()==Player);
            auto* Received=World->SpawnActor<AERCellRecoveryShard>();Received->Room=Target;Received->RecoveryId=Source->RecoveryId;Received->Header=Page->Header;
            Target->ReceiveCellShard(Received);Pending.Emplace(Page,Received);
        }
        if(CheckIncomplete)TestEqual(TEXT("headers alone never commit"),Target->GetStateDigest(),Before);
        for(int32 I=Pending.Num()-1;I>=0;--I){Pending[I].Value->Chunks.Items=Pending[I].Key->Chunks.Items;Target->ReceiveCellShard(Pending[I].Value);}
        return Manifest;
    };
    Server->CellWorld.DetachCells({{0,2,0,4}});Publish();const uint32 CheckpointDigest=Server->GetStateDigest();
    FString Error;
    TestTrue(TEXT("one owner checkpoint starts"),Server->StartCellRecovery(Player,Server->Network.Epoch,Error));
    if(Server->RecoveryTransfers.IsEmpty()){World->DestroyWorld(false);return false;}
    auto* Frozen=ServerManifest();const uint32 FrozenId=Frozen->RecoveryId,FrozenSequence=Frozen->Descriptor.Sequence;
    TestTrue(TEXT("manifest is owner-only"),Frozen->bOnlyRelevantToOwner && !Frozen->bAlwaysRelevant && Frozen->GetOwner()==Player);
    TestTrue(TEXT("duplicate requests reuse a pinned checkpoint"),Server->StartCellRecovery(Player,Server->Network.Epoch,Error));
    TestEqual(TEXT("one in-flight checkpoint per owner"),Server->RecoveryTransfers.Num(),1);
    Server->CellWorld.DetachCells({{0,3,0,4}});Publish();Client->Network=Server->Network;Client->OnRep_Network();
    TestTrue(TEXT("live target advances independently"),Client->Network.Sequence>FrozenSequence);
    auto* Delayed=World->SpawnActor<AERBlockRoom>();Delayed->SetRole(ROLE_SimulatedProxy);
    Deliver(Frozen,false,Delayed);
    TestFalse(TEXT("checkpoint arriving before the room descriptor cannot open readiness"),Delayed->IsMapReady());
    Delayed->bNetworkBegunPlay=true;Delayed->Network=Server->Network;Delayed->OnRep_Network();
    TestTrue(TEXT("early checkpoint actors are rediscovered after the room descriptor"),Delayed->IsMapReady());
    TestEqual(TEXT("reordered checkpoint retains its immutable state"),Delayed->GetStateDigest(),CheckpointDigest);
    TestEqual(TEXT("reordered checkpoint does not wait for the live target"),Delayed->AppliedSequence,FrozenSequence);
    auto* Received=Deliver(Frozen,true);
    TestEqual(TEXT("a complete older checkpoint commits despite a newer live target"),Client->AppliedSequence,FrozenSequence);
    TestEqual(TEXT("checkpoint contents remain immutable during live destruction"),Client->GetStateDigest(),CheckpointDigest);
    TestTrue(TEXT("checkpoint collision is complete"),Client->IsMapReady() && Client->ValidateCells());
    for(int32 I=0;I<LivePages.Num();++I)
    {LivePages[I]->Header=Server->AuthorityShards[I]->Header;LivePages[I]->Chunks.Items=Server->AuthorityShards[I]->Chunks.Items;LivePages[I]->OnRep_State();}
    TestEqual(TEXT("live deltas resume from the checkpoint"),Client->GetStateDigest(),Server->GetStateDigest());
    uint32 Current=Client->GetStateDigest();Client->ReceiveCellRecovery(Received);
    TestEqual(TEXT("a repeated older checkpoint cannot roll state back"),Client->GetStateDigest(),Current);
    TestFalse(TEXT("another owner cannot acknowledge a checkpoint"),Server->AcknowledgeCellRecovery(Other,FrozenId));
    TestTrue(TEXT("the owner releases checkpoint actors"),Server->AcknowledgeCellRecovery(Player,FrozenId));
    TestFalse(TEXT("duplicate acknowledgments are rejected"),Server->AcknowledgeCellRecovery(Player,FrozenId));

    TestTrue(TEXT("a checkpoint can be in flight while live state advances"),Server->StartCellRecovery(Player,Server->Network.Epoch,Error));
    auto* Obsolete=ServerManifest();const uint32 ObsoleteId=Obsolete->RecoveryId;
    Server->CellWorld.DetachCells({{0,4,0,4}});Publish();Client->Network=Server->Network;Client->OnRep_Network();
    for(int32 I=0;I<LivePages.Num();++I)
    {LivePages[I]->Header=Server->AuthorityShards[I]->Header;LivePages[I]->Chunks.Items=Server->AuthorityShards[I]->Chunks.Items;LivePages[I]->OnRep_State();}
    Current=Client->GetStateDigest();const uint32 BeforeErrorSequence=Client->AppliedSequence;

    auto* Bad=LivePages[0];Bad->SetChunk(0,999,{1,0});++Bad->Header.Version;
    Client->Network.PageVersions[0]=Bad->Header.Version;++Client->Network.Sequence;
    AddExpectedError(TEXT("ER_BLOCK_NET_INCOMPATIBLE state"),EAutomationExpectedErrorFlags::Contains,1);
    Client->OnRep_Network();
    TestTrue(TEXT("bad payload transitions to an explicit non-ready error"),Client->GetCellSyncStatus().Error && !Client->IsMapReady());
    TestEqual(TEXT("invalid transaction preserves the last complete logical state"),Client->GetStateDigest(),Current);
    Deliver(Obsolete,false);
    TestTrue(TEXT("obsolete checkpoint does not reopen readiness after a newer payload error"),Client->GetCellSyncStatus().Error && !Client->IsMapReady());
    TestEqual(TEXT("obsolete checkpoint never rolls the applied sequence back"),Client->AppliedSequence,BeforeErrorSequence);
    TestTrue(TEXT("obsolete checkpoint is acknowledged so a bounded retry can request a newer repair"),!Client->GetCellSyncStatus().Recovering && Client->GetCellSyncStatus().RecoveryId==ObsoleteId);
    TestTrue(TEXT("obsolete checkpoint storage can be released"),Server->AcknowledgeCellRecovery(Player,ObsoleteId));
    Publish();Client->Network=Server->Network;Client->OnRep_Network();
    TestTrue(TEXT("a valid recovery can start after a payload error"),Server->StartCellRecovery(Player,Server->Network.Epoch,Error));
    auto* Repair=ServerManifest();const uint32 RepairId=Repair->RecoveryId;
    Deliver(Repair,true);
    TestTrue(TEXT("complete recovery restores readiness and clears the error"),Client->IsMapReady() && !Client->GetCellSyncStatus().Error);
    TestEqual(TEXT("repair preserves exact authoritative state"),Client->GetStateDigest(),Server->GetStateDigest());
    Server->AcknowledgeCellRecovery(Player,RepairId);

    auto* Source=Server->AuthorityShards[0].Get();auto Original=Source->Chunks.Items[0].Mask;
    Source->Chunks.Items[0].Mask.SetNumZeroed(32*1024*1024+1);
    TestFalse(TEXT("per-client byte budget rejects an oversized checkpoint before actors spawn"),Server->StartCellRecovery(Player,Server->Network.Epoch,Error));
    TestTrue(TEXT("budget rejection has a useful explanation"),Error.Contains(TEXT("budget")));
    TestTrue(TEXT("budget rejection leaves no transfer"),Server->RecoveryTransfers.IsEmpty());
    Source->Chunks.Items[0].Mask=MoveTemp(Original);
    TestTrue(TEXT("bounded checkpoint can restart"),Server->StartCellRecovery(Player,Server->Network.Epoch,Error));
    const SIZE_T ActualBytes=Server->RecoveryTransfers[0].Bytes;Server->RecoveryTransfers[0].Bytes=128*1024*1024;
    TestFalse(TEXT("aggregate byte budget prevents per-connection multiplication"),Server->StartCellRecovery(Other,Server->Network.Epoch,Error));
    Server->RecoveryTransfers[0].Bytes=ActualBytes;const uint32 ExpiredId=Server->RecoveryTransfers[0].Id;
    Server->RecoveryTransfers[0].ExpiresAt=FPlatformTime::Seconds()-1;
    TestFalse(TEXT("expired checkpoint acknowledgments cannot release gameplay gates"),Server->AcknowledgeCellRecovery(Player,ExpiredId));
    TestTrue(TEXT("expired checkpoint storage is reclaimed"),Server->RecoveryTransfers.IsEmpty());
    TestFalse(TEXT("stale generation recovery is rejected"),Server->StartCellRecovery(Player,Server->Network.Epoch+1,Error));
    World->DestroyWorld(false);return true;
}
#endif
