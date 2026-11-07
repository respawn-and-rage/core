#include "ERNetworkReadinessProbe.h"
#include "ERPlayerController.h"
#include "ERCharacter.h"
#include "ERWeapon.h"
#include "ERBlockRoom.h"
#include "ERMatch.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

AERNetworkReadinessProbe::AERNetworkReadinessProbe(){PrimaryActorTick.bCanEverTick=true;}
void AERNetworkReadinessProbe::Check(bool Passed,const TCHAR* Label)
{
    Failed|=!Passed;
    UE_LOG(LogTemp,Display,TEXT("ER_NET_READINESS_%s %s"),Passed?TEXT("PASS"):TEXT("FAIL"),Label);
}
void AERNetworkReadinessProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
    if(!HasAuthority())return;
    TActorIterator<AERBlockRoom> Room(GetWorld());if(!Room)return;
    AERPlayerController* RemotePC=nullptr;
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if(auto* PC=Cast<AERPlayerController>(It->Get());PC && !PC->IsLocalController()){RemotePC=PC;break;}
    auto* C=RemotePC?Cast<AERCharacter>(RemotePC->GetPawn()):nullptr;
    if(!C || !C->Weapon)return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(!BeganWaiting)BeganWaiting=Now;
    if(Now-BeganWaiting>45){Check(false,TEXT("entry_timeout"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(Phase==0 && Now-BeganWaiting>=5)
    {
        Check(!C->bNetworkReady,TEXT("slow_client_stays_pending_past_protection_duration"));
        Check(C->Health->IsProtected() && C->IsHidden() && !C->GetActorEnableCollision(),TEXT("pending_pawn_is_hidden_noncolliding_and_protected"));
        Check(C->GetCharacterMovement()->MovementMode==MOVE_None,TEXT("pending_movement_is_server_disabled"));
        const float BeforeHealth=C->Health->Health;
        Check(C->Health->ReceiveDamage(10000,nullptr)==0 && C->Health->Health==BeforeHealth,TEXT("pending_pawn_rejects_damage"));
        const int32 BeforeAmmo=C->Weapon->MagazineAmmo;
        C->Weapon->StartFire();C->Weapon->StopFire();
        Check(C->Weapon->MagazineAmmo==BeforeAmmo,TEXT("pending_pawn_cannot_fire"));
        RemotePC->ServerBlockMapReady_Implementation(*Room,Room->GetMapEpoch()-1,C,Room->GetMapSequence());
        Check(!C->bNetworkReady,TEXT("stale_epoch_ack_cannot_release_pawn"));
        RemotePC->ServerBlockMapReady_Implementation(*Room,Room->GetMapEpoch(),nullptr,Room->GetMapSequence());
        Check(!C->bNetworkReady,TEXT("wrong_pawn_ack_cannot_release_pawn"));
        Phase=1;
    }
    else if(Phase==1 && C->bNetworkReady)
    {
        Check(C->Health->ProtectionEndServerTime-Now>ERServer::Get(GetWorld(),"Health.Protection")-1,TEXT("protection_starts_on_actual_entry"));
        Check(!C->IsHidden() && C->GetActorEnableCollision() && C->GetCharacterMovement()->IsMovingOnGround(),TEXT("ready_pawn_is_playable"));
        const double Protection=C->Health->ProtectionEndServerTime;
        RemotePC->ServerBlockMapReady_Implementation(*Room,Room->GetMapEpoch(),C,Room->GetMapSequence());
        Check(C->Health->ProtectionEndServerTime==Protection,TEXT("duplicate_ack_never_renews_protection"));
        const int32 BeforeAmmo=C->Weapon->MagazineAmmo;
        C->Weapon->StartFire();C->Weapon->StopFire();
        Check(C->Weapon->MagazineAmmo<BeforeAmmo,TEXT("entered_pawn_can_fire"));
        const FERCellId Damaged{0,0,0,0};
        TMap<FERCellId,float> Damage;Damage.Add(Damaged,TNumericLimits<float>::Max());
        Check(Room->ApplyCellBatch(Damage).Destroyed==1,TEXT("recovery_contains_a_real_damage_payload"));
        RemotePC->ServerRequestCellRecovery_Implementation(*Room,Room->GetMapEpoch());
        Check(RemotePC->IsCellRecoveryPending(),TEXT("owner_checkpoint_requested"));
        Check(C->bNetworkReady && !C->IsHidden() && !C->Health->IsProtected(),TEXT("recovery_request_does_not_grant_invulnerability"));
        Phase=2;
    }
    else if(Phase==2 && !RemotePC->IsCellRecoveryPending())
    {
        Check(C->bNetworkReady && !C->Health->IsProtected(),TEXT("recovery_ack_keeps_existing_combat_state"));
        UE_LOG(LogTemp,Display,TEXT("ER_NET_READINESS_COMPLETE result=%s wait=%.2f"),Failed?TEXT("FAIL"):TEXT("PASS"),Now-BeganWaiting);
        FPlatformMisc::RequestExitWithStatus(false,Failed?1:0);Phase=3;
    }
#endif
}
