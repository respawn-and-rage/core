#include "ERAccuracyProbe.h"
#include "ERCharacter.h"
#include "ERPlayerController.h"
#include "ERWeapon.h"
#include "ERMovement.h"
#include "ERMatch.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "HAL/PlatformMisc.h"

AERAccuracyProbe::AERAccuracyProbe() { bReplicates = true; bAlwaysRelevant = true; PrimaryActorTick.bCanEverTick = true; }
void AERAccuracyProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AERAccuracyProbe,Phase);
}
void AERAccuracyProbe::Advance(int32 Next, float Delay) { Phase=Next; Deadline=GetWorld()->GetTimeSeconds()+Delay; ForceNetUpdate(); }
void AERAccuracyProbe::Check(bool Value, const TCHAR* Label)
{
    Failed |= !Value;
    UE_LOG(LogTemp,Display,TEXT("ER_ACCURACY_%s %s"),Value ? TEXT("PASS") : TEXT("FAIL"),Label);
}
void AERAccuracyProbe::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
#if !UE_BUILD_SHIPPING
    const float SpreadScale=ERServer::Get(GetWorld(),"Weapons.Spread");
    if (!HasAuthority())
    {
        auto* Pawn = Cast<AERCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(),0));
        if (!Pawn || !Pawn->Weapon || !Pawn->Weapon->Data) return;
        auto* Weapon = Pawn->Weapon.Get();
        if (!SawOwnerBase && FMath::IsNearlyEqual(Weapon->GetSpreadDegrees(),ERWeaponAccuracy::Degrees(7.01f*SpreadScale),.01f))
        {
            SawOwnerBase=true; UE_LOG(LogTemp,Display,TEXT("ER_ACCURACY_CLIENT_PASS owner_base_data"));
        }
        if (ClientPhase != Phase)
        {
            ClientPhase=Phase;
            if (Phase==7) Pawn->ServerStartFire();
            if (Phase==8) Pawn->ServerStopFire();
        }
        if (!SawOwnerBloom && Phase>=7 && Weapon->GetBloomDegrees()>.2f*SpreadScale && Weapon->MagazineAmmo<30)
        {
            SawOwnerBloom=true;
            UE_LOG(LogTemp,Display,TEXT("ER_ACCURACY_CLIENT_PASS owner_ammo_bloom_replicated ammo=%d spread=%.3f"),Weapon->MagazineAmmo,Weapon->GetSpreadDegrees());
        }
        if (!SawOwnerRecovery && SawOwnerBloom && Phase>=9 && Weapon->GetBloomDegrees()<.001f)
        {
            SawOwnerRecovery=true; UE_LOG(LogTemp,Display,TEXT("ER_ACCURACY_CLIENT_PASS owner_clock_recovers_bloom"));
        }
        return;
    }
    auto* Mode = GetWorld()->GetAuthGameMode<AERGameMode>();
    if (!Mode || !Mode->IsMatchInProgress()) return;
    if (!Host || !Client)
    {
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
            if (auto* PC=Cast<AERPlayerController>(It->Get())) { if (PC->IsLocalController()) Host=PC; else Client=PC; }
        if (!Host || !Client) return;
    }
    auto* H=Cast<AERCharacter>(Host->GetPawn()); auto* C=Cast<AERCharacter>(Client->GetPawn());
    if (!H || !C || !H->Weapon || !C->Weapon) return;
    const double Now=GetWorld()->GetTimeSeconds();
    auto* Move=Cast<UERMovement>(H->GetCharacterMovement());
    if (Phase==0)
    {
        H->TeleportTo(FVector(-1000,-1500,95),FRotator::ZeroRotator);
        C->TeleportTo(FVector(1000,-1500,95),FRotator(0,180,0));
        Host->SetControlRotation(FRotator::ZeroRotator); Host->ClientSetRotation(FRotator::ZeroRotator);
        Client->SetControlRotation(FRotator(0,180,0)); Client->ClientSetRotation(FRotator(0,180,0));
        H->Health->ProtectionEndServerTime=C->Health->ProtectionEndServerTime=Now+60;
        Move->SetMovementMode(MOVE_Walking);Move->Velocity=FVector::ZeroVector;
        const FVector SavedVelocity=Move->Velocity;const auto SavedPosture=Move->Locomotion;
        for(int32 Slot=1;Slot<=6;++Slot)
        {
            auto* Weapon=H->Inventory[Slot].Get();H->ServerSelectWeapon(Slot);
            Check(Weapon->Data->bUseWorksheetAccuracy,TEXT("all_firearms_use_workbook_accuracy"));
            Weapon->FiringInaccuracyAtLastShot=0;Weapon->AccuracyLandingSpeed=0;
            for(int32 Aim=0;Aim<=(Weapon->HasOpticalScope()?1:0);++Aim)
            {
                if(Aim)Weapon->ServerCycleScope();
                const auto& P=Weapon->GetAccuracyMode();
                for(int32 Low=0;Low<3;++Low)
                {
                    Move->Locomotion.HalfHeight=Low==0?92.5f:Low==1?62.5f:34.f;
                    Move->Locomotion.ProneBlend=Low==2?1.f:0.f;
                    for(float Speed:{0.f,138.889f,277.778f,500.f,P.MaxSpeed*2.54f,P.MaxSpeed*2.54f*1.5f})
                    {
                        Move->Velocity=FVector(Speed,0,0);
                        const float Raw=P.Spread+(Low?P.Crouch:P.Stand)+P.Move*FMath::Clamp(Speed/(P.MaxSpeed*2.54f),0.f,1.f);
                        Check(FMath::IsNearlyEqual(Weapon->GetSpreadDegrees(),ERWeaponAccuracy::Degrees(Raw),.001f),
                            TEXT("runtime_worksheet_posture_speed_and_scope_cone"));
                    }
                }
            }
        }
        Move->Locomotion=SavedPosture;
        Move->Velocity=SavedVelocity;H->ServerSelectWeapon(4);
        auto* AK=H->Weapon.Get();
        AK->FiringInaccuracyAtLastShot=100;AK->AccuracyBurstShots=1;
        AK->AccuracyRecoveryTime=.368f;AK->AccuracyRecoveryServerTime=Now-.1;
        const float BeforeCrouch=AK->GetFiringInaccuracy();
        Move->Locomotion.HalfHeight=62.5f;AK->UpdateAccuracyRecovery();
        Check(FMath::IsNearlyEqual(BeforeCrouch,AK->GetFiringInaccuracy(),.0001f) &&
            FMath::IsNearlyEqual(AK->AccuracyRecoveryTime,.305257f,.00001f),TEXT("crouching_changes_recovery_slope_without_rewriting_history"));
        AK->AccuracyRecoveryServerTime=Now-.1;const float BeforeStand=AK->GetFiringInaccuracy();
        Move->Locomotion.HalfHeight=92.5f;AK->UpdateAccuracyRecovery();
        Check(FMath::IsNearlyEqual(BeforeStand,AK->GetFiringInaccuracy(),.0001f) && AK->AccuracyRecoveryTime==.368f,
            TEXT("standing_does_not_resurrect_recovered_inaccuracy"));
        AK->FiringInaccuracyAtLastShot=0;Move->Locomotion=SavedPosture;
        Advance(1,.5f); return;
    }
    if (Phase==3 || Phase==4) H->AddMovementInput(FVector(1,0,0));
    if (Phase==6 && ConeSamples<10 && Now>=Deadline)
    {
        H->ServerStartFire(); H->ServerStopFire();
        const auto* Weapon=H->Weapon.Get();
        const float Angle=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Weapon->LastShotAimDirection,Weapon->LastShotDirection),-1.,1.)));
        SamplesWithinCone &= Angle<=Weapon->LastShotSpreadDegrees+.01f;
        if (ConeSamples==0) FirstDirection=Weapon->LastShotDirection;
        else SamplesVary |= !FirstDirection.Equals(Weapon->LastShotDirection,.0001f);
        const FVector TraceDirection = (Weapon->LastShotTraceEnd - Weapon->LastShotTraceStart).GetSafeNormal();
        SamplesWithinCone &= Weapon->LastShotTraceCount == 1 && TraceDirection.Equals(Weapon->LastShotDirection, .001f);
        for (TActorIterator<AERProjectile> It(GetWorld()); It; ++It)
            if (It->GetOwner() == Weapon && It->Payload == EERPayload::Bullet) SamplesWithinCone = false;
        ++ConeSamples; Deadline=Now+.11; return;
    }
    if (Now<Deadline) return;
    switch (Phase)
    {
    case 1:
        Standing=H->Weapon->GetSpreadDegrees();
        Check(FMath::IsNearlyEqual(Standing,ERWeaponAccuracy::Degrees(7.01f*SpreadScale),.001f),TEXT("standing_base_cone"));
        H->Crouch(); Advance(2,.5f); break;
    case 2:
        Check(H->bIsCrouched && H->Weapon->GetSpreadDegrees()<Standing,TEXT("crouch_narrows_cone"));
        H->UnCrouch(); Move->bWalkHeld=true; Advance(3,.7f); break;
    case 3:
        Walking=H->Weapon->GetSpreadDegrees();
        Check(H->GetVelocity().Size2D()>130 && Walking>Standing,TEXT("walking_adds_actual_movement_spread"));
        Move->bWalkHeld=false; Advance(4,.7f); break;
    case 4:
        Running=H->Weapon->GetSpreadDegrees();
        Check(H->GetVelocity().Size2D()>270 && Running>Walking,TEXT("running_wider_than_walking"));
        H->Jump(); Advance(5,.2f); break;
    case 5:
        Check(Move->IsFalling() && H->Weapon->GetSpreadDegrees()>Running,TEXT("jump_adds_airborne_spread"));
        Host->SetControlRotation(FRotator(65,0,0));Advance(6,.7f); break;
    case 6:
        Check(ConeSamples==10 && SamplesWithinCone,TEXT("ten_hitscan_traces_within_server_cone_no_bullet_actors"));
        Check(H->Weapon->AccuracyLandingSpeed>0 && H->Weapon->AccuracyLandingServerTime>Now-3,
            TEXT("real_jump_landing_records_authority_measured_impulse"));
        Check(SamplesVary,TEXT("server_samples_different_directions"));
        Check(H->Weapon->FiringInaccuracyAtLastShot>=7.8f*SpreadScale && FMath::IsFinite(H->Weapon->GetFiringInaccuracy()),TEXT("worksheet_fire_impulses_accumulate_without_legacy_cap"));
        H->Health->ProtectionEndServerTime=Now+60;
        AmmoBeforeBurst=C->Weapon->MagazineAmmo; Advance(7,.65f); break;
    case 7:
        Check(C->Weapon->MagazineAmmo<AmmoBeforeBurst && C->Weapon->GetSpreadDegrees()>Standing+.2f*SpreadScale,TEXT("remote_rpc_burst_adds_server_bloom"));
        Advance(8,.4f); break;
    case 8:
        Check(!C->Weapon->bReloading && C->Weapon->GetBloomDegrees()>0,TEXT("stopped_burst_recovers_gradually"));
        Advance(9,2.5f); break;
    case 9:
        Check(C->Weapon->GetBloomDegrees()<.001f && FMath::IsNearlyEqual(C->Weapon->GetSpreadDegrees(),Standing,.01f),TEXT("server_bloom_returns_to_base"));
        C->Weapon->MagazineAmmo=0; C->Weapon->ForceNetUpdate();
        C->ServerStartFire(); C->ServerStopFire(); Advance(10,.5f); break;
    case 10:
        Check(C->Weapon->GetBloomDegrees()<.001f,TEXT("rejected_empty_shot_adds_no_bloom"));
        UE_LOG(LogTemp,Display,TEXT("ER_ACCURACY_COMPLETE result=%s"),Failed ? TEXT("FAIL") : TEXT("PASS"));
        FPlatformMisc::RequestExitWithStatus(false,Failed ? 1 : 0); Advance(11,1000); break;
    }
#endif
}
