#include "ERRecoilProbe.h"
#include "ERCharacter.h"
#include "ERWeapon.h"
#include "ERMovement.h"
#include "ERPlayerController.h"
#include "ERMatch.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "HAL/PlatformMisc.h"

AERRecoilProbe::AERRecoilProbe(){bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;}
void AERRecoilProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AERRecoilProbe,Phase);}
void AERRecoilProbe::Check(bool Value,const TCHAR* Label)
{Failed|=!Value;UE_LOG(LogTemp,Display,TEXT("ER_RECOIL_%s %s case=%d"),Value?TEXT("PASS"):TEXT("FAIL"),Label,Case);}
void AERRecoilProbe::Advance(int32 Next,float Delay)
{Phase=Next;Deadline=GetWorld()->GetTimeSeconds()+Delay;ForceNetUpdate();}
void AERRecoilProbe::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
#if !UE_BUILD_SHIPPING
    if(!HasAuthority())
    {
        auto* C=Cast<AERCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(),0));
        if(!C || !C->Weapon || !C->Controller)return;
        if(ClientPhase!=Phase)
        {
            ClientPhase=Phase;
            if(Phase==100){C->ServerStartFire();}
            if(Phase==101){C->ServerStopFire();}
        }
        if(Phase>=100 && !SawOwner && C->GetRecoilRotation().Pitch>.1f)
        {
            FMinimalViewInfo View;C->CalcCamera(0,View);
            const FRotator Expected=(C->Controller->GetControlRotation()+C->GetRecoilRotation()).GetNormalized();
            const bool Good=View.Rotation.Equals(Expected,.001f) && C->GetViewRotation().Equals(Expected,.001f) &&
                C->Controller->GetControlRotation().Equals(FRotator(45,180,0),.01f);
            UE_LOG(LogTemp,Display,TEXT("ER_RECOIL_CLIENT_%s owner_camera_and_input"),Good?TEXT("PASS"):TEXT("FAIL"));
            SawOwner=Good;
        }
        if(Phase>=102 && SawOwner && !SawRecovery && C->GetRecoilRotation().IsNearlyZero(.001f))
        {SawRecovery=true;UE_LOG(LogTemp,Display,TEXT("ER_RECOIL_CLIENT_PASS owner_recovery"));}
        return;
    }
    auto* Mode=GetWorld()->GetAuthGameMode<AERGameMode>();if(!Mode || !Mode->IsMatchInProgress())return;
    if(!Host || !Client)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
            if(auto* PC=Cast<AERPlayerController>(It->Get())){if(PC->IsLocalController())Host=PC;else Client=PC;}
        if(!Host || !Client)return;
    }
    auto* H=Cast<AERCharacter>(Host->GetPawn());auto* C=Cast<AERCharacter>(Client->GetPawn());
    if(!H || !C || !H->Weapon || !C->Weapon)return;
    const double Now=GetWorld()->GetTimeSeconds();if(Now<Deadline)return;
    static const int32 Slots[]={1,2,3,4,5,5,5,6,6,6};
    static const int32 Scopes[]={0,0,0,0,0,1,2,0,1,2};
    if(Phase==0)
    {
        H->Health->ProtectionEndServerTime=C->Health->ProtectionEndServerTime=Now+60;
        H->TeleportTo(FVector(-1000,-1500,95),FRotator::ZeroRotator);
        Host->SetControlRotation(FRotator(45,0,0));Host->ClientSetRotation(FRotator(45,0,0));
        H->ServerSelectWeapon(Slots[Case]);auto* W=H->Weapon.Get();
        // Each mode is a separate two-shot case; AWP's five-round magazine
        // otherwise empties during the third mode before testing accumulation.
        W->MagazineAmmo=W->Data->MagazineSize;
        W->ClearScope();for(int32 I=0;I<Scopes[Case];++I)W->ServerCycleScope();
        H->RecoilState=FERWeaponRecoilState();
        const auto P=W->GetRecoilMode();const uint32 Count=W->AuthorizedShotCount;
        H->ServerStartFire();H->ServerStopFire();
        Check(W->Data->bUseWorksheetRecoil && W->AuthorizedShotCount==Count+1,TEXT("all_firearms_authorized_shot"));
        const double Angle=FMath::Abs(FMath::RadiansToDegrees(FMath::Atan2(W->LastRecoilImpulse.Y,W->LastRecoilImpulse.X)));
        Check(W->LastRecoilImpulse.Size()>=P.Magnitude-P.MagnitudeVariance-.001 &&
            W->LastRecoilImpulse.Size()<=P.Magnitude+P.MagnitudeVariance+.001 && Angle<=P.AngleVariance+.001,
            TEXT("actual_shot_uses_active_sheet_recoil_mode"));
        Check(H->RecoilState.Velocity.Equals(W->LastRecoilImpulse,.001),TEXT("one_impulse_per_shell_including_shotgun"));
        Check(H->GetRecoilRotation().IsNearlyZero(.001f),TEXT("first_bullet_precedes_its_own_kick"));
        const auto State=H->RecoilState;
        H->ServerStartFire();H->ServerStopFire();
        Check(W->AuthorizedShotCount==Count+1 && H->RecoilState.Velocity.Equals(State.Velocity),TEXT("cooldown_rejection_adds_no_recoil"));
        Advance(1,.08f);return;
    }
    if(Phase==1)
    {
        auto* W=H->Weapon.Get();
        Check(H->GetRecoilRotation().Pitch>.1f && Host->GetControlRotation().Equals(FRotator(45,0,0),.001),TEXT("real_recoil_preserves_input_rotation"));
        const FRotator Kick=H->GetRecoilRotation();
        Host->SetControlRotation(FRotator(45,0,0)-Kick);
        Check(H->GetViewRotation().Equals(FRotator(45,0,0),.001),TEXT("mouse_input_can_compensate_recoil"));
        Host->SetControlRotation(FRotator(89,0,0));
        Check(H->GetViewRotation().Pitch<=89.f,TEXT("recoil_at_vertical_limit_does_not_flip_camera"));
        Host->SetControlRotation(FRotator(45,0,0));
        const auto State=H->RecoilState;const uint32 Count=W->AuthorizedShotCount;
        const int32 Ammo=W->MagazineAmmo;W->MagazineAmmo=0;
        H->ServerStartFire();H->ServerStopFire();W->MagazineAmmo=Ammo;
        Check(W->AuthorizedShotCount==Count && H->RecoilState.ServerTime==State.ServerTime &&
            H->RecoilState.Velocity.Equals(State.Velocity),TEXT("empty_shot_adds_no_recoil"));
        W->Reload();H->ServerStartFire();H->ServerStopFire();
        Check(W->bReloading && W->AuthorizedShotCount==Count && H->RecoilState.ServerTime==State.ServerTime,
            TEXT("reload_rejection_adds_no_recoil"));
        W->Cancel();
        H->ServerSelectWeapon(0);
        Check(H->RecoilState.Velocity.Equals(State.Velocity),TEXT("holstering_does_not_reset_recoil"));
        H->ServerSelectWeapon(Slots[Case]);
        W->ClearScope();for(int32 I=0;I<Scopes[Case];++I)W->ServerCycleScope();
        Advance(2,FMath::Max(.02f,W->Data->ShotInterval-.08f+.03f));return;
    }
    if(Phase==2)
    {
        auto* W=H->Weapon.Get();const auto Before=ERWeaponRecoil::At(H->RecoilState,Now);
        const FRotator Aim=H->GetViewRotation();const FVector Eye=H->Camera->GetComponentLocation();
        const FVector Muzzle=W->HasOpticalScope() && W->ScopeLevel ? Eye:Eye+Aim.RotateVector(FVector(45,12,-12));
        const FVector Expected=(Eye+Aim.Vector()*W->Data->HitscanRange-Muzzle).GetSafeNormal();
        const uint32 Count=W->AuthorizedShotCount;H->ServerStartFire();H->ServerStopFire();
        Check(W->AuthorizedShotCount==Count+1 && W->LastShotAimDirection.Equals(Expected,.0001),TEXT("next_bullet_uses_server_recoiled_eyes_once"));
        Check(H->RecoilState.Velocity.Equals(Before.Velocity+W->LastRecoilImpulse,.001),TEXT("second_impulse_accumulates_with_remaining_recoil"));
        ++Case;if(Case<10){Advance(0,W->Data->ShotInterval+.03f);return;}
        Client->SetControlRotation(FRotator(45,180,0));Client->ClientSetRotation(FRotator(45,180,0));
        C->ServerSelectWeapon(4);C->RecoilState=FERWeaponRecoilState();C->ForceNetUpdate();
        Advance(99,.3f);return;
    }
    if(Phase==99){Advance(100,.5f);return;}
    if(Phase==100)
    {
        Check(C->Weapon->AuthorizedShotCount>=2 && C->GetRecoilRotation().Pitch>.1f,TEXT("remote_rpc_burst_accumulates_on_authority"));
        Advance(101,4.f);return;
    }
    if(Phase==101)
    {
        Check(C->GetRecoilRotation().IsNearlyZero(.001f),TEXT("authority_recoil_recovers_after_burst"));
        Advance(102,.4f);return;
    }
    if(Phase==102)
    {
        UE_LOG(LogTemp,Display,TEXT("ER_RECOIL_COMPLETE result=%s"),Failed?TEXT("FAIL"):TEXT("PASS"));
        FPlatformMisc::RequestExitWithStatus(false,Failed?1:0);Advance(103,1000);
    }
#endif
}
