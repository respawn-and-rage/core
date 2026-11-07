#include "ERPlayerController.h"
#include "ERBlockRoom.h"
#include "ERCharacter.h"
#include "ERWeapon.h"
#include "ERMatch.h"
#include "ERMapCatalog.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameSession.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void AERPlayerController::TickActor(float DeltaSeconds,ELevelTick TickType,FActorTickFunction& ThisTickFunction)
{
    Super::TickActor(DeltaSeconds,TickType,ThisTickFunction);
    // PlayerTick runs only for a local controller. Admission deadlines and
    // server frame diagnostics must also advance for remote connections.
    UpdateBlockMapLoading();TickNetworkQuality(DeltaSeconds);
}

void AERPlayerController::SetPawnNetworkReady(bool Ready)
{
    auto* C=Cast<AERCharacter>(GetPawn());
    if(!HasAuthority() || !C || C->Health->bDead)return;
    const bool Changed=C->bNetworkReady!=Ready;
    C->bNetworkReady=Ready;
    if(!Ready)
    {
        if(C->Weapon)C->Weapon->Cancel();
        C->GetCharacterMovement()->StopMovementImmediately();
        C->GetCharacterMovement()->SetMovementMode(MOVE_None);
        C->Health->ProtectionEndServerTime=0;
    }
    else if(Changed)
    {
        C->Health->ProtectionEndServerTime=GetWorld()->GetTimeSeconds()+ERServer::Get(GetWorld(),"Health.Protection");
        C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        C->GetCharacterMovement()->ForceClientAdjustment();
    }
    C->SetActorHiddenInGame(!Ready);C->SetActorEnableCollision(Ready);
    if(Ready){WaitingBlockRoom.Reset();WaitingBlockPawn.Reset();WaitingBlockEpoch=0;}
    if(Changed)
    {
        C->ForceNetUpdate();
        UE_LOG(LogTemp,Display,TEXT("ER_NET_ENTRY ready=%d pawn=%s epoch=%u protection=%.3f"),Ready,*C->GetName(),AcknowledgedBlockEpoch,C->Health->ProtectionEndServerTime);
    }
}

void AERPlayerController::ServerBlockMapReady_Implementation(AERBlockRoom* Room,uint32 Epoch,APawn* ReadyPawn,uint32 Sequence)
{
    if(bWaitingCellRecovery || !Room || Room->GetWorld()!=GetWorld() || !Room->CanMutateBlocks() || !Room->IsMapReady() || Epoch==0 ||
        Room->GetMapEpoch()!=Epoch || ReadyPawn!=GetPawn() || Sequence==0 || Sequence>Room->GetMapSequence() ||
        (WaitingBlockRoom.IsValid() && WaitingBlockRoom.Get()!=Room))return;
    AcknowledgedBlockEpoch=Epoch;AcknowledgedBlockRoom=Room;AcknowledgedBlockPawn=ReadyPawn;
    SetPawnNetworkReady(true);
    const auto* PS=GetPlayerState<AERPlayerState>();
    UE_LOG(LogTemp,Display,TEXT("ER_BLOCK_NET_PLAYER_READY name=%s epoch=%u sequence=%u"),PS?*PS->GetPlayerName():TEXT("unknown"),Epoch,Sequence);
}

void AERPlayerController::ServerBlockMapProgress_Implementation(AERBlockRoom* Room,uint32 Epoch,uint64 Progress)
{
    if(Room && Room==WaitingBlockRoom.Get() && Room->GetWorld()==GetWorld() && Epoch==WaitingBlockEpoch && Epoch==Room->GetMapEpoch())
        MapSyncDeadline.Observe(Progress,FPlatformTime::Seconds());
}

void AERPlayerController::ServerRequestCellRecovery_Implementation(AERBlockRoom* Room,uint32 Epoch)
{
    const double Now=FPlatformTime::Seconds();
    if(Now<NextServerRecoveryAt || !Room || Room->GetWorld()!=GetWorld() || !Room->CanMutateBlocks() || Room->GetMapEpoch()!=Epoch)
    {ClientCellRecoveryResult(false);return;}
    NextServerRecoveryAt=Now+10;
    FString Error;
    const bool Accepted=Room->StartCellRecovery(this,Epoch,Error);
    if(!Accepted)
    {
        UE_LOG(LogTemp,Warning,TEXT("ER_CELL_RECOVERY_REJECT player=%s reason=%s"),*GetName(),*Error);
    }
    else
    {
        // Once admitted, a recovery request must never grant fresh protection
        // or make an active player disappear. The last coherent baseline stays
        // authoritative; the owner pauses input while receiving the checkpoint.
        if(auto* C=Cast<AERCharacter>(GetPawn());C && C->bNetworkReady)
        {
            if(C->Weapon)C->Weapon->Cancel();
            if(!bWaitingCellRecovery || WaitingBlockRoom.Get()!=Room || WaitingBlockPawn.Get()!=C || WaitingBlockEpoch!=Epoch)
                MapSyncDeadline.Reset(Now);
            WaitingBlockRoom=Room;WaitingBlockPawn=C;WaitingBlockEpoch=Epoch;
        }
        else {AcknowledgedBlockEpoch=0;HoldForBlockMap(Room);}
        bWaitingCellRecovery=true;
    }
    ClientCellRecoveryResult(Accepted);
}

void AERPlayerController::ClientCellRecoveryResult_Implementation(bool Accepted)
{
    // Accepted may arrive after the manifest committed on another actor channel.
    if(!Accepted)bCellRecoveryRequested=false;
}

void AERPlayerController::NotifyCellRecoveryApplied(AERBlockRoom* Room,uint32 RecoveryId)
{
    bCellRecoveryRequested=false;AcknowledgedBlockPawn.Reset();
    ServerAckCellRecovery(Room,RecoveryId);
}

void AERPlayerController::ServerAckCellRecovery_Implementation(AERBlockRoom* Room,uint32 RecoveryId)
{
    if(Room && Room->GetWorld()==GetWorld() && Room->CanMutateBlocks() && Room->AcknowledgeCellRecovery(this,RecoveryId))bWaitingCellRecovery=false;
}

void AERPlayerController::UpdateBlockMapLoading()
{
    const double Now=FPlatformTime::Seconds();
    if(HasAuthority() && WaitingBlockRoom.IsValid() && WaitingBlockPawn.Get()==GetPawn())
    {
        if(IsLocalController() && WaitingBlockRoom->IsMapReady()){SetPawnNetworkReady(true);return;}
        if(MapSyncDeadline.Expired(Now))
        {
            UE_LOG(LogTemp,Warning,TEXT("ER_NET_SYNC_TIMEOUT player=%s stalled=%.1f elapsed=%.1f"),*GetName(),Now-MapSyncDeadline.LastProgressAt,Now-MapSyncDeadline.StartedAt);
            if(auto* Mode=GetWorld()->GetAuthGameMode<AERGameMode>();Mode && Mode->GameSession)
                Mode->GameSession->KickPlayer(this,FText::FromString(TEXT("Map synchronization timed out. Please reconnect.")));
            WaitingBlockRoom.Reset();WaitingBlockPawn.Reset();
        }
    }
    if(!IsLocalController() || GetNetMode()!=NM_Client)return;
    TActorIterator<AERBlockRoom> Room(GetWorld());
    auto* C=Cast<AERCharacter>(GetPawn());
    bool ExpectsRoom=false;
    const FString MapName=GetWorld()->GetMapName();
    for(const auto& Entry:ERMaps::All())if(Entry.Procedural && MapName.Contains(FPackageName::GetShortName(Entry.Package))){ExpectsRoom=true;break;}
    ExpectsRoom|=MapName.Contains(TEXT("L_ProceduralLab")) || MapName.Contains(TEXT("L_DestructionLab")) || MapName.Contains(TEXT("L_Killhouse")) || MapName.Contains(TEXT("L_DustMirror"));
    bool GeometryReady=Room && Room->IsMapReady();
    const auto Sync=Room?Room->GetCellSyncStatus():FERCellSyncStatus{};
    bool DebugDelay=false;
#if !UE_BUILD_SHIPPING
    float DelaySeconds=0;
    if(GeometryReady && FParse::Value(FCommandLine::Get(),TEXT("ERMapSyncDelay="),DelaySeconds))
    {
        if(DebugSyncEpoch!=Sync.Epoch){DebugSyncEpoch=Sync.Epoch;DebugSyncReadyAt=Now+FMath::Clamp(DelaySeconds,0.f,30.f);}
        DebugDelay=Now<DebugSyncReadyAt;GeometryReady&=!DebugDelay;
    }
#endif
    const bool Loading=(Room ? !GeometryReady:ExpectsRoom) || Sync.Recovering || bCellRecoveryRequested || (C && !C->bNetworkReady);
    if(Loading!=bBlockMapLoading)
    {
        bBlockMapLoading=Loading;SetIgnoreMoveInput(Loading);
        if(Loading && C){C->SetFireInputHeld(false);C->ServerStopFire();}
    }
    if(C)C->GetCharacterMovement()->SetComponentTickEnabled(!Loading);
    if(!Room)return;
    if(WaitingBlockRoom.Get()!=*Room || Sync.Epoch!=WaitingBlockEpoch)
    {
        WaitingBlockRoom=*Room;WaitingBlockEpoch=Sync.Epoch;LastClientProgress=Sync.ProgressSerial;LastClientProgressAt=Now;bCellRecoveryRequested=false;
        RecoveryAttempts=0;NextRecoveryRequestAt=Now+5;
    }
    if(LastClientProgress!=Sync.ProgressSerial)
    {LastClientProgress=Sync.ProgressSerial;LastClientProgressAt=Now;}
    if(Loading && Now>=NextMapProgressAt)
    {NextMapProgressAt=Now+1;ServerBlockMapProgress(*Room,Sync.Epoch,Sync.ProgressSerial);}
    // A pinned snapshot provides a finite target when live manifests keep moving.
    // Never reset the no-progress deadline merely because a newer sequence arrives.
    if(GeometryReady && !Sync.Recovering && Sync.AppliedSequence==Sync.TargetSequence)RecoveryAttempts=0;
    if(!DebugDelay && !bCellRecoveryRequested && !Sync.Recovering && RecoveryAttempts<2 && Now>=NextRecoveryRequestAt &&
        (Sync.Error || (!GeometryReady && Now-LastClientProgressAt>5) || (Sync.TargetSequence>Sync.AppliedSequence && Sync.LastAppliedAgeSeconds>5)))
    {
        ++RecoveryAttempts;NextRecoveryRequestAt=Now+15;bCellRecoveryRequested=true;AcknowledgedBlockPawn.Reset();ServerRequestCellRecovery(*Room,Sync.Epoch);
    }
    if(Sync.Recovering || !GeometryReady)AcknowledgedBlockPawn.Reset();
    if(GeometryReady && !bCellRecoveryRequested && !Sync.Recovering && C &&
        (AcknowledgedBlockRoom.Get()!=*Room || AcknowledgedBlockEpoch!=Sync.Epoch || AcknowledgedBlockPawn.Get()!=C))
    {
        AcknowledgedBlockEpoch=Sync.Epoch;AcknowledgedBlockRoom=*Room;AcknowledgedBlockPawn=C;
        ServerBlockMapReady(*Room,Sync.Epoch,C,Sync.AppliedSequence);
    }
}

void AERPlayerController::TickNetworkQuality(float DeltaSeconds)
{
    if(HasAuthority() && DeltaSeconds>0)
        ServerFramePeriodMs=ServerFramePeriodMs>0?FMath::Lerp(ServerFramePeriodMs,DeltaSeconds*1000,1.f-FMath::Exp(-DeltaSeconds*3)):DeltaSeconds*1000;
    if(!IsLocalController() || GetNetMode()!=NM_Client)return;
    const double Now=FPlatformTime::Seconds();
    if(Now<NextNetworkSampleAt)return;
    NextNetworkSampleAt=Now+.5;
    ++NetworkSampleToken;
    auto& Pending=NetworkSamples[NetworkSampleToken%UE_ARRAY_COUNT(NetworkSamples)];
    Pending.Token=NetworkSampleToken;Pending.SentAt=Now;Pending.Pending=true;
    ServerNetworkSample(NetworkSampleToken);
}

void AERPlayerController::ServerNetworkSample_Implementation(uint16 Token)
{
    const double Now=FPlatformTime::Seconds();
    if(Now<NextServerSampleAt)return;
    NextServerSampleAt=Now+.4;
    ClientNetworkSample(Token,ServerFramePeriodMs);
}

void AERPlayerController::ClientNetworkSample_Implementation(uint16 Token,float ServerFrameMs)
{
    auto& Pending=NetworkSamples[Token%UE_ARRAY_COUNT(NetworkSamples)];
    if(!Pending.Pending || Pending.Token!=Token)return;
    Pending.Pending=false;const double Now=FPlatformTime::Seconds();
    NetworkQuality.Observe(Now-Pending.SentAt);LastNetworkReplyAt=Now;
    RemoteServerFrameMs=FMath::IsFinite(ServerFrameMs)?FMath::Clamp(ServerFrameMs,0.f,10000.f):0;
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("ERNetworkAudit")))
        UE_LOG(LogTemp,Display,TEXT("ER_NET_QUALITY %s"),*GetNetworkStatusText());
#endif
}

FString AERPlayerController::GetNetworkStatusText() const
{
    if(GetNetMode()!=NM_Client)return {};
    if(!NetworkQuality.Samples || FPlatformTime::Seconds()-LastNetworkReplyAt>3)return TEXT("Connection pending...");
    const auto* Driver=GetWorld()->GetNetDriver();const auto* Connection=Driver?Driver->ServerConnection.Get():nullptr;
    const float Loss=Connection?100*FMath::Max(Connection->GetInLossPercentage().GetAvgLossPercentage(),Connection->GetOutLossPercentage().GetAvgLossPercentage()):0;
    FString Result=FString::Printf(TEXT("Ping %.0f ms  |  jitter %.0f ms  |  loss %.1f%%  |  server %.1f ms"),NetworkQuality.RoundTripMs,NetworkQuality.JitterMs,Loss,RemoteServerFrameMs);
    TActorIterator<AERBlockRoom> Room(GetWorld());
    if(Room)
    {
        const auto Sync=Room->GetCellSyncStatus();
        if(Sync.TargetSequence>Sync.AppliedSequence)Result+=FString::Printf(TEXT("  |  map %.1fs"),Sync.LastAppliedAgeSeconds);
    }
    return Result;
}

FString AERPlayerController::GetMapSyncText() const
{
    TActorIterator<AERBlockRoom> Room(GetWorld());
    if(!Room)return TEXT("Preparing map...");
    const auto Sync=Room->GetCellSyncStatus();
    if(Sync.Error && !Sync.Recovering)return TEXT("Recovering map synchronization...");
    if(Sync.Ready && !Sync.Recovering && !bCellRecoveryRequested)return TEXT("Entering match...");
    const int32 Percent=Sync.ExpectedPages>0?FMath::Clamp(Sync.VerifiedPages*100/Sync.ExpectedPages,0,100):0;
    return FString::Printf(TEXT("Synchronizing map... %d%%"),Percent);
}
