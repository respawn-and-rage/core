#include "ERAimPunchProbe.h"
#include "ERCharacter.h"
#include "ERCorpse.h"
#include "ERWounds.h"
#include "ERWeapon.h"
#include "ERPlayerController.h"
#include "ERBotController.h"
#include "ERMatch.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "HAL/PlatformMisc.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

AERAimPunchProbe::AERAimPunchProbe(){bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;}
void AERAimPunchProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AERAimPunchProbe,Phase);}
void AERAimPunchProbe::ConfirmReady(AERPlayerController* Player,int32 Stage)
{if(HasAuthority() && Player==Client && Stage==Phase)ReadyPhase=Stage;}
void AERAimPunchProbe::Check(bool V,const TCHAR* Label)
{Failed|=!V;UE_LOG(LogTemp,Display,TEXT("ER_AIMPUNCH_%s %s case=%d"),V?TEXT("PASS"):TEXT("FAIL"),Label,Case);}
void AERAimPunchProbe::Advance(int32 Next,float Delay)
{Phase=Next;Deadline=GetWorld()->GetTimeSeconds()+Delay;ForceNetUpdate();}
float AERAimPunchProbe::Hit(AERCharacter* Victim,float HP,bool Head)
{
    auto* Shooter=CastChecked<AERCharacter>(Host->GetPawn());auto* Gun=Shooter->Inventory[1].Get();
    FHitResult H;H.ImpactPoint=Victim->GetActorLocation();H.BoneName=Head?TEXT("head"):TEXT("spine_03");
    Gun->LastShotTraceStart=H.ImpactPoint;++Gun->AuthorizedShotCount;
    return UGameplayStatics::ApplyPointDamage(Victim,HP/(Head?Gun->Data->HeadshotMultiplier:1.f),FVector(0,-1,0),H,Host,Gun,UDamageType::StaticClass());
}
void AERAimPunchProbe::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
#if !UE_BUILD_SHIPPING
    const double Now=GetWorld()->GetTimeSeconds();
    if(!HasAuthority())
    {
        auto* C=Cast<AERCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(),0));if(!C || !C->Controller)return;
        if(LocalPhase!=Phase){LocalPhase=Phase;StableFrames=0;ReadyAfter=Now+1.5;}
        const bool ModelReady=C->Weapon && C->Weapon->Data && C->FirstPersonRifle->GetSkeletalMeshAsset()==C->Weapon->Data->Mesh;
        if(DeltaTime>.1f || !ModelReady){StableFrames=0;ReadyAfter=Now+1.5;}else ++StableFrames;
        if(SentPhase!=Phase && StableFrames>=40 && Now>=ReadyAfter)
        {
            CastChecked<AERPlayerController>(C->Controller)->ServerAimPunchProbeReady(this,Phase);SentPhase=Phase;
        }
        if(Phase==99 && OwnerChecks<1000){C->ServerStartFire();OwnerChecks=1000;}
        if(Phase==101 && OwnerChecks>=1000){C->ServerStopFire();OwnerChecks=-1000;}
        if(Phase>=99 && Phase<=101)
        {
            FMinimalViewInfo V;C->CalcCamera(0,V);
            UE_LOG(LogTemp,Display,TEXT("ER_AIMPUNCH_CAMERA pawn=%s real=%.6f pitch=%.6f yaw=%.6f punch=%.6f"),*C->GetName(),FPlatformTime::Seconds(),V.Rotation.Pitch,V.Rotation.Yaw,C->GetAimPunchRotation().Pitch);
        }
        if(!WoundReplicationReported && Phase>=99 && C->Wounds->GetMarks().Num()>0)
        {
            WoundReplicationReported=true;
            UE_LOG(LogTemp,Display,TEXT("ER_WOUND_CLIENT_PASS replicated_marks=%d"),C->Wounds->GetMarks().Num());
        }
        const auto& State=C->GetAimPunchState();
        if(State.Sequence!=ClientSequence && State.Angle.X>.01 && !C->Health->bDead)
        {
            ClientSequence=State.Sequence;FMinimalViewInfo V;C->CalcCamera(0,V);
            const auto Punch=C->GetAimPunchRotation();const auto Recoil=C->GetRecoilRotation();
            auto Expected=(C->Controller->GetControlRotation()+Punch+Recoil).GetNormalized();Expected.Pitch=FMath::Clamp(Expected.Pitch,-89.f,89.f);
            const double Age=GetWorld()->GetGameState()->GetServerWorldTimeSeconds()-State.ServerTime;
            const bool Good=FMath::Abs(FMath::FindDeltaAngleDegrees(V.Rotation.Pitch,Expected.Pitch))<.001 && FMath::Abs(FMath::FindDeltaAngleDegrees(V.Rotation.Yaw,Expected.Yaw))<.001;
            UE_LOG(LogTemp,Display,TEXT("ER_AIMPUNCH_CLIENT_%s owner_camera_once pawn=%s seq=%u hit_time=%.6f server_now=%.6f age_ms=%.3f server_pitch=%.6f owner_pitch=%.6f error=%.7f"),Good?TEXT("PASS"):TEXT("FAIL"),*C->GetName(),State.Sequence,State.ServerTime,State.ServerTime+Age,Age*1000,State.Angle.X,Punch.Pitch,FMath::Abs(V.Rotation.Pitch-Expected.Pitch));
            if(C->Weapon && C->Weapon->HasOpticalScope() && C->Weapon->ScopeLevel)
                UE_LOG(LogTemp,Display,TEXT("ER_AIMPUNCH_CLIENT_%s scoped_camera_and_hud"),Good && V.FOV==C->Weapon->GetScopeFOV()?TEXT("PASS"):TEXT("FAIL"));
            ++OwnerChecks;
            if(FParse::Param(FCommandLine::Get(),TEXT("ERAimPunchGallery")))
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Testing/AimPunch/owner-hit-%u.png"),State.Sequence),false,false);
        }
        if(Phase==102 && !Recovered && C->GetAimPunchRotation().IsNearlyZero(.001f))
        {Recovered=true;UE_LOG(LogTemp,Display,TEXT("ER_AIMPUNCH_CLIENT_PASS owner_recovers"));
            if(FParse::Param(FCommandLine::Get(),TEXT("ERAimPunchGallery")))FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Testing/AimPunch/owner-recovered.png"),false,false);}
        return;
    }
    auto* Mode=GetWorld()->GetAuthGameMode<AERGameMode>();if(!Mode || !Mode->IsMatchInProgress())return;
    if(!Host || !Client)
    {
        for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I)if(auto* P=Cast<AERPlayerController>(I->Get())){if(P->IsLocalController())Host=P;else Client=P;}
        if(!Host || !Client)return;
    }
    auto* H=Cast<AERCharacter>(Host->GetPawn());auto* C=Cast<AERCharacter>(Client->GetPawn());if(!H || !C || !H->Weapon || !C->Weapon || Now<Deadline)return;
    if(Phase==0)
    {
        UE_LOG(LogTemp,Display,TEXT("ER_AIMPUNCH_TARGET pawn=%s"),*C->GetName());
        auto* Floor=GetWorld()->SpawnActor<AStaticMeshActor>();Floor->SetReplicates(true);Floor->SetReplicateMovement(true);Floor->bAlwaysRelevant=true;
        auto* Surface=Floor->GetStaticMeshComponent();Surface->SetMobility(EComponentMobility::Movable);Surface->SetIsReplicated(true);
        Surface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Surface->SetCollisionProfileName(TEXT("BlockAll"));
        Floor->SetActorLocation(FVector(0,0,5000));Floor->SetActorScale3D(FVector(60,60,.2));
        H->TeleportTo(FVector(-1200,0,5104.6),FRotator::ZeroRotator);C->TeleportTo(FVector(1200,0,5104.6),FRotator(0,180,0));
        Host->SetControlRotation(FRotator(45,0,0));Host->ClientSetRotation(FRotator(45,0,0));
        Client->SetControlRotation(FRotator(45,180,0));Client->ClientSetRotation(FRotator(45,180,0));
        H->Health->ProtectionEndServerTime=C->Health->ProtectionEndServerTime=0;H->Health->Armor=C->Health->Armor=0;
        Advance(1,1);return;
    }
    if((Phase==1 || Phase>=99) && ReadyPhase!=Phase)return;
    if(Phase==1)
    {
        H->Health->Health=100;H->ResetAimPunch();H->RecoilState=FERWeaponRecoilState();
        const auto Base=Host->GetControlRotation();const FVector Pos=H->GetActorLocation();
        const int32 WoundsBefore=H->Wounds->GetMarks().Num();
        Check(FMath::IsNearlyEqual(Hit(H,20),20.f,.001f),TEXT("20hp_real_bullet_pipeline"));
        Check(H->Wounds->GetMarks().Num()==WoundsBefore+1,TEXT("accepted_bullet_adds_one_wound"));
        Check(FMath::IsNearlyEqual(H->GetAimPunchRotation().Pitch,.64f,.001f),TEXT("single_hit_exact_impulse"));
        Check(Host->GetControlRotation().Equals(Base) && H->GetActorLocation().Equals(Pos),TEXT("input_and_capsule_unchanged"));
        FMinimalViewInfo V;H->CalcCamera(0,V);
        Check(FMath::IsNearlyEqual(V.Rotation.Pitch,Base.Pitch+.64f,.001f),TEXT("host_camera_no_cosmetic_pitch_duplicate"));
        Host->SetControlRotation(Base-H->GetAimPunchRotation());
        Check(H->GetViewRotation().Equals(Base,.001f),TEXT("counter_steering_restores_aim"));Host->SetControlRotation(Base);
        Hit(H,20);Hit(H,20);Check(H->GetAimPunchRotation().Pitch>1.9f && H->GetAimPunchRotation().Pitch<=2.5f,TEXT("three_hits_stack"));
        H->Health->Health=100;Hit(H,20);Hit(H,20);Check(FMath::IsNearlyEqual(H->GetAimPunchRotation().Pitch,2.5f,.001f),TEXT("burst_cap"));
        H->ResetAimPunch();H->Health->Health=100;H->Health->Armor=100;Hit(H,20);
        const float Actual=100-H->Health->Health;Check(FMath::IsNearlyEqual(H->GetAimPunchRotation().Pitch,(.4f+Actual*.012f)*.6f,.001f),TEXT("armor_uses_actual_mitigation"));
        H->ResetAimPunch();H->Health->Health=100;H->Health->Armor=0;Hit(H,20,true);
        Check(FMath::IsNearlyEqual(H->GetAimPunchRotation().Pitch,.96f,.001f),TEXT("real_head_bone_flag"));
        H->ResetAimPunch();H->Health->ProtectionEndServerTime=Now+10;
        const int32 ProtectedWounds=H->Wounds->GetMarks().Num();Hit(H,20);
        Check(H->GetAimPunchRotation().IsNearlyZero(),TEXT("spawn_protected_no_impulse"));H->Health->ProtectionEndServerTime=0;
        Check(H->Wounds->GetMarks().Num()==ProtectedWounds,TEXT("protected_bullet_adds_no_wound"));
        FHitResult Other;UGameplayStatics::ApplyPointDamage(H,5,FVector::ForwardVector,Other,Host,nullptr,UDamageType::StaticClass());
        Check(H->GetAimPunchRotation().IsNearlyZero(),TEXT("non_bullet_no_impulse"));
        Check(H->Wounds->GetMarks().Num()==ProtectedWounds,TEXT("non_bullet_adds_no_wound"));
        H->Health->Armor=100;
        FHitResult ArmorHit;ArmorHit.BoneName=TEXT("spine_03");ArmorHit.ImpactPoint=H->GetMesh()->GetSocketLocation(ArmorHit.BoneName);ArmorHit.ImpactNormal=FVector::ForwardVector;
        const float HealthBeforeArmorHit=H->Health->Health;
        H->Health->ReceiveDamage(20,Client,0.f,FERMessageDamage(),&ArmorHit);
        Check(H->Health->Health==HealthBeforeArmorHit && H->Health->Armor==90 && H->Wounds->GetMarks().Num()==ProtectedWounds+1,
            TEXT("armor_only_hit_adds_wound_without_health_loss"));
        Check(H->Wounds->GetMarks().Last().Variant>=4 && H->Wounds->GetMarks().Last().Variant<8,
            TEXT("armor_only_selects_fabric_atlas"));
        H->Health->ReceiveDamage(10,Client,1.f,FERMessageDamage(),&ArmorHit);
        Check(H->Health->Health<HealthBeforeArmorHit && H->Health->Armor<90 && H->Wounds->GetMarks().Last().Variant<4,
            TEXT("mixed_health_armor_selects_original_wounds"));
        for(int32 I=1;I<=6;++I)Host->ClientInflictedDamage(float(I),float(I*2));
        Check(Host->DamageNotices.Num()==5 && Host->DamageNotices[0].Health==6 && Host->DamageNotices.Last().Health==2,
            TEXT("damage_notices_newest_first_max_five"));
        Host->DamageNotices.Reset();
        H->Health->Armor=0;
        H->Health->Health=100;H->ResetAimPunch();Hit(H,20);
        --H->Inventory[1]->AuthorizedShotCount;Hit(H,20);
        Check(FMath::IsNearlyEqual(H->GetAimPunchRotation().Pitch,.88f,.001f),TEXT("pellets_share_one_base_impulse"));
        for(TActorIterator<AERCharacter> It(GetWorld());It;++It)if(It->Controller && !It->Controller->IsPlayerController())
        {
            Bot=*It;Bot->Controller->SetActorTickEnabled(false);Bot->GetCharacterMovement()->DisableMovement();
            Bot->TeleportTo(FVector(0,1200,5104.6),FRotator::ZeroRotator);Bot->Health->Health=100;Bot->Health->Armor=0;Bot->Health->ProtectionEndServerTime=0;
            Bot->Controller->SetControlRotation(FRotator(45,90,0));Bot->ServerSelectWeapon(4);Bot->ResetAimPunch();Bot->RecoilState=FERWeaponRecoilState();
            Hit(Bot,20);const auto Aim=Bot->GetEffectiveAimRotation();const FVector Eye=Bot->Camera->GetComponentLocation();
            const FVector Expected=(Eye+Aim.Vector()*Bot->Weapon->Data->HitscanRange-(Eye+Aim.RotateVector(FVector(45,12,-12)))).GetSafeNormal();
            Bot->ServerStartFire();Bot->ServerStopFire();
            Check(Bot->GetAimPunchRotation().Pitch>.6f && Bot->Weapon->LastShotAimDirection.Equals(Expected,.0001f),TEXT("bot_final_firing_vector_uses_punch"));break;
        }
        Check(Bot!=nullptr,TEXT("bot_fixture_present"));
        H->Health->Health=100;H->ResetAimPunch();Advance(2,.15f);return;
    }
    static const int32 Slots[]={1,2,3,4,5,5,5,6,6,6,7};
    static const int32 Scopes[]={0,0,0,0,0,1,2,0,1,2,0};
    if(Phase==2)
    {
        H->Health->Health=100;H->ResetAimPunch();H->RecoilState=FERWeaponRecoilState();
        H->ServerSelectWeapon(Slots[Case]);auto* W=H->Weapon.Get();W->MagazineAmmo=W->Data->MagazineSize;
        W->ClearScope();for(int32 I=0;I<Scopes[Case];++I)W->ServerCycleScope();
        const float SpreadBefore=W->GetSpreadDegrees();Hit(H,20);
        Check(FMath::IsNearlyEqual(SpreadBefore,W->GetSpreadDegrees(),.00001f),TEXT("punch_adds_no_spread_penalty"));
        const auto Aim=H->GetEffectiveAimRotation();const FVector Eye=H->Camera->GetComponentLocation();
        const FVector Muzzle=W->HasOpticalScope() && W->ScopeLevel ? Eye:Eye+Aim.RotateVector(FVector(45,12,-12));
        const FVector Expected=(Eye+Aim.Vector()*W->Data->HitscanRange-Muzzle).GetSafeNormal();
        const uint32 Shots=W->AuthorizedShotCount;H->ServerStartFire();H->ServerStopFire();
        Check(W->AuthorizedShotCount==Shots+1 && W->LastShotAimDirection.Equals(Expected,.0001f),TEXT("all_guns_scopes_launcher_effective_trace_once"));
        ++Case;if(Case<11){Advance(2,W->Data->ShotInterval+.04f);return;}
        // Physical host bullet against the remote victim's actual skeletal hit zones.
        H->ServerSelectWeapon(1);H->ResetAimPunch();H->RecoilState=FERWeaponRecoilState();
        H->Weapon->MagazineAmmo=H->Weapon->Data->MagazineSize;C->Health->Health=100;C->Health->Armor=0;
        Host->SetControlRotation((C->GetMesh()->GetSocketLocation(TEXT("spine_03"))-H->Camera->GetComponentLocation()).Rotation());
        H->ServerStartFire();H->ServerStopFire();
        Check(C->Health->Health<100 && C->GetAimPunchRotation().Pitch>0,TEXT("physical_host_bullet_punches_remote_victim"));
        Check(C->Wounds->GetMarks().Num()>0,TEXT("physical_host_bullet_marks_remote_victim"));
        C->ServerSelectWeapon(2);Advance(99,.5f);return;
    }
    if(Phase==99){C->Health->Health=100;Hit(C,20);Advance(100,.3f);return;}
    if(Phase==100)
    {
        C->Health->Health=100;Hit(C,20);Hit(C,20);Advance(101,.3f);return;
    }
    if(Phase==101)
    {
        C->Health->Health=100;Hit(C,20);
        Check(C->GetAimPunchRotation().Pitch>.1f,TEXT("remote_authoritative_punch_active"));
        C->ServerStopFire();C->RecoilState=FERWeaponRecoilState();C->ServerSelectWeapon(4);
        const auto Aim=C->GetEffectiveAimRotation();const FVector Eye=C->Camera->GetComponentLocation();
        const FVector Expected=(Eye+Aim.Vector()*C->Weapon->Data->HitscanRange-(Eye+Aim.RotateVector(FVector(45,12,-12)))).GetSafeNormal();
        C->ServerStartFire();C->ServerStopFire();
        Check(C->Weapon->LastShotAimDirection.Equals(Expected,.0001f),TEXT("remote_shot_during_punch_uses_authority"));
        Advance(102,4);return;
    }
    if(Phase==102)
    {
        Check(C->GetAimPunchRotation().IsNearlyZero(.001f),TEXT("authority_recovers"));
        C->ServerSelectWeapon(5);C->Weapon->ClearScope();C->Weapon->ServerCycleScope();C->RecoilState=FERWeaponRecoilState();
        Client->SetControlRotation(FRotator(0,180,0));Client->ClientSetRotation(FRotator(0,180,0));
        Advance(104,.5f);return;
    }
    if(Phase==104){C->Health->Health=100;Hit(C,20);Advance(105,.8f);return;}
    if(Phase==105)
    {
        Hit(C,20);C->Health->ReceiveDamage(100,Host);
        Check(C->GetAimPunchRotation().IsNearlyZero(),TEXT("death_clears_punch"));
        Advance(103,.6f);return;
    }
    if(Phase==103)
    {
        bool CorpseWounds=false;
        for(TActorIterator<AERCorpse> It(GetWorld());It;++It)
            CorpseWounds|=It->Wounds && It->Wounds->GetMarks().Num()>0;
        Check(CorpseWounds,TEXT("death_copies_wounds_to_corpse"));
        Client->RespawnAllowedAt=Now;Mode->RequestRespawn(Client);
        auto* Fresh=Cast<AERCharacter>(Client->GetPawn());
        Check(Fresh && Fresh!=C && Fresh->GetAimPunchRotation().IsNearlyZero() && Fresh->GetAimPunchState().Angle.IsZero(),TEXT("respawn_new_pawn_has_no_offset"));
        H->Health->Health=100;Hit(H,20);Host->UnPossess();Host->Possess(H);
        Check(H->GetAimPunchRotation().IsNearlyZero(),TEXT("controller_possession_resets_offset"));
        UE_LOG(LogTemp,Display,TEXT("ER_AIMPUNCH_COMPLETE result=%s"),Failed?TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExit(false);
    }
#endif
}
