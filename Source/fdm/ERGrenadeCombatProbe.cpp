#include "ERGrenadeProbe.h"
#include "ERCharacter.h"
#include "ERWeapon.h"
#include "ERPlayerController.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "Engine/PointLight.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "InputCoreTypes.h"
#include "ShaderCompiler.h"

void AERGrenadeProbe::TickCombat(float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING
    const double Now=GetWorld()->GetTimeSeconds();
    auto* PC=Cast<AERPlayerController>(GetWorld()->GetFirstPlayerController());
    auto* Local=PC?Cast<AERCharacter>(PC->GetPawn()):nullptr;
    auto Input=[PC](bool Down)
    {
        FInputKeyEventArgs Args;Args.Key=EKeys::LeftMouseButton;Args.Event=Down?IE_Pressed:IE_Released;
        Args.AmountDepressed=Down?1:0;Args.EventTimestamp=FPlatformTime::Cycles64();PC->InputKey(Args);
    };
    if(!HasAuthority())
    {
        if(!Local || !Local->Weapon || Local->Inventory.Num()!=10) return;
        if(Phase==5 && SeenPhase!=5 && Local->Weapon==Local->Inventory[8])
        { Input(true);SeenPhase=5; }
        if(Phase>=6 && Phase<=7 && Local->Weapon->bCookingGrenade && !bRemoteHeldLogged)
        {
            bRemoteHeldLogged=true;
            UE_LOG(LogTemp,Display,TEXT("ER_COOK_REMOTE_HELD hidden=%d bone=%d"),!Local->FirstPersonRifle->IsVisible(),Local->FirstPersonBody->IsBoneHiddenByName(TEXT("upperarm_r")));
        }
        if(Phase==7 && SeenPhase!=7) { Input(false);SeenPhase=7; }
        if(Phase>=8 && !Local->Weapon->bCookingGrenade && bRemoteHeldLogged && !bRemoteReleaseLogged)
        { bRemoteReleaseLogged=true;UE_LOG(LogTemp,Display,TEXT("ER_COOK_REMOTE_RELEASE")); }
        return;
    }
    if(Now<Deadline || Now<4 || !Local || Local->Inventory.Num()!=10) return;
    AERPlayerController* Remote=nullptr;
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if(auto* Candidate=Cast<AERPlayerController>(It->Get());Candidate && !Candidate->IsLocalController()) { Remote=Candidate;break; }
    auto* C=Remote?Cast<AERCharacter>(Remote->GetPawn()):nullptr;
    if(!C || C->Inventory.Num()!=10) return;
    auto Place=[](AERCharacter* Pawn,FVector Position)
    {
        Pawn->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
        Pawn->GetCharacterMovement()->StopMovementImmediately();Pawn->GetCharacterMovement()->DisableMovement();
    };
    auto Wall=[this](FVector Position,FVector Size)
    {
        auto* A=GetWorld()->SpawnActor<AStaticMeshActor>(Position,FRotator::ZeroRotator);
        auto* M=A->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);
        M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        M->SetWorldScale3D(Size/100);M->SetCollisionObjectType(ECC_WorldStatic);
        M->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);M->SetCollisionResponseToAllChannels(ECR_Block);
        return A;
    };
    if(Phase==0)
    {
        if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
        Fixture();PC->SetViewTarget(Local);
        Place(Local,FVector(-450,-300,5092.5));PC->SetControlRotation(FRotator(0,90,0));
        Local->Health->ProtectionEndServerTime=0;Local->ServerSelectWeapon(8);
        Check(Local->Weapon->Data->ExplosionRadius==600 && Local->Weapon->Data->ExplosionInnerRadius==200,TEXT("saved_he_profile_has_two_and_six_metre_envelope"));
        Check(Local->Inventory[7]->Data->ExplosionRadius==600,TEXT("saved_milkor_profile_shares_fragmentation_envelope"));
        Check(Local->Inventory[7]->Data->FuseDuration==3.5f && Local->Inventory[8]->Data->FuseDuration==3.5f && Local->Inventory[9]->Data->FuseDuration==3.5f,TEXT("saved_grenade_fuses_are_three_and_half_seconds"));
        Advance(1,.4f);
    }
    else if(Phase==1)
    {
        Capture(TEXT("Cook00_Ready.png"));AmmoBeforeCook=Local->Weapon->MagazineAmmo;
        CookWeapon=Local->Weapon;Input(true);Advance(2,.65f);
    }
    else if(Phase==2)
    {
        auto* W=CookWeapon.Get();CookProjectile=W?W->CookedGrenade.Get():nullptr;
        Check(W && W->bCookingGrenade && CookProjectile.IsValid() && CookProjectile->bHeld,TEXT("click_arms_a_real_fused_grenade_without_throwing"));
        Check(W && W->MagazineAmmo==AmmoBeforeCook-1,TEXT("arming_consumes_one_grenade_immediately"));
        Check(!Local->FirstPersonRifle->IsVisible() && Local->FirstPersonBody->IsBoneHiddenByName(TEXT("upperarm_r")),TEXT("click_hides_grenade_and_holding_hand"));
        Capture(TEXT("Cook01_Held.png"));
        Local->ServerSelectWeapon(4);Check(Local->Weapon==W,TEXT("armed_grenade_cannot_be_cancelled_by_weapon_switch"));
        W->Reload();Check(!W->bReloading,TEXT("reload_cannot_cancel_cook"));
        if(CookProjectile.IsValid()) { CookProjectile->Damage=0;OriginalFuseEnd=CookProjectile->FuseEndServerTime; }
        PC->SetControlRotation(FRotator(0,90,0));Input(false);Advance(3,.25f);
    }
    else if(Phase==3)
    {
        Check(CookProjectile.IsValid() && !CookProjectile->bHeld && !CookWeapon->bCookingGrenade,TEXT("release_throws_the_same_armed_projectile"));
        if(CookProjectile.IsValid())
        {
            Check(FMath::IsNearlyEqual(OriginalFuseEnd,CookProjectile->FuseEndServerTime,.001),TEXT("release_does_not_restart_fuse"));
            const FVector Offset=CookProjectile->ReleasedFrom-Local->Camera->GetComponentLocation();
            Check(FVector::DotProduct(Offset,FRotator(0,90,0).Vector())<0 && Offset.Z>=-5 && Offset.Z<=0,TEXT("throw_starts_just_above_shoulder_below_eye"));
            Check(CookProjectile->ReleasedVelocity.Z>200,TEXT("overarm_throw_has_upward_ballistic_arc"));
            const FVector StandardThrow=FVector(0,1,.35f).GetSafeNormal()*1200.f;
            Check(FMath::IsNearlyEqual(CookProjectile->ReleasedVelocity.Size2D(),StandardThrow.Size2D()*1.2f,1.f) &&
                FMath::IsNearlyEqual(CookProjectile->ReleasedVelocity.Z,StandardThrow.Z,1.f),TEXT("level_aim_uses_standard_hand_throw_speed_and_arc"));
        }
        Capture(TEXT("Cook02_Released.png"));
        Advance(4,FMath::Max(.1,OriginalFuseEnd-Now+.5));
    }
    else if(Phase==4)
    {
        Check(!CookProjectile.IsValid(),TEXT("cooked_throw_detonates_on_original_deadline"));
        Place(Local,FVector(-30,0,5092.5));PC->SetControlRotation(FRotator(0,180,0));
        Check(AERProjectile::SafeHandPosition(Local,true).X<-10,TEXT("overhead_origin_cannot_spawn_through_wall_behind_head"));
        Place(Local,FVector(-450,-300,5092.5));PC->SetControlRotation(FRotator(0,90,0));
        const FVector Eye=Local->Camera->GetComponentLocation();
        auto* Ceiling=Wall(Eye+FVector(0,0,25),FVector(200,200,10));
        Check(AERProjectile::SafeHandPosition(Local,true).Z<=Eye.Z+17.5f,TEXT("low_ceiling_clips_overhead_origin_safely"));
        Ceiling->Destroy();
        Local->ServerSelectWeapon(4);
        Place(C,FVector(-450,350,5092.5));Remote->SetControlRotation(FRotator(0,-90,0));C->ServerSelectWeapon(8);
        C->Health->ProtectionEndServerTime=0;Advance(5,1.f);
    }
    else if(Phase==5)
    {
        C->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Check(C->Weapon && C->Weapon->bCookingGrenade,TEXT("remote_input_arms_on_server"));
        CookWeapon=C->Weapon;CookProjectile=C->Weapon?C->Weapon->CookedGrenade.Get():nullptr;
        if(CookProjectile.IsValid()) { CookProjectile->Damage=0;OriginalFuseEnd=CookProjectile->FuseEndServerTime; }
        if(RenderCamera.IsValid())
        {
            const FVector Target=C->GetActorLocation()+FVector(0,0,55);
            RenderCamera->SetActorLocation(C->GetActorLocation()+FVector(180,-200,70));
            RenderCamera->SetActorRotation((Target-RenderCamera->GetActorLocation()).Rotation());
            PC->SetViewTarget(RenderCamera.Get());
            // Opt-in fixture fill makes the remote hand/skin readable in captures.
            auto* Fill=GetWorld()->SpawnActor<APointLight>(RenderCamera->GetActorLocation(),FRotator::ZeroRotator);
            Fill->FindComponentByClass<UPointLightComponent>()->SetIntensity(6000);
            Fill->FindComponentByClass<UPointLightComponent>()->SetAttenuationRadius(600);
        }
        Advance(6,.35f);
    }
    else if(Phase==6)
    {
        Check(CookProjectile.IsValid() && CookProjectile->bHeld,TEXT("remote_hold_keeps_grenade_in_hand"));
        Check(C->WorldRifle->IsVisible() && C->GrenadeThrowPoseBlend>.9f,TEXT("remote_cooking_grenade_visible_with_ready_arm_pose"));
        Check(C->GetMesh()->GetSocketLocation(TEXT("hand_r")).Z>C->GetMesh()->GetSocketLocation(TEXT("upperarm_r")).Z,TEXT("ready_pose_raises_hand_above_shoulder"));
        const auto* Grip=C->GetMesh()->GetSkeletalMeshAsset()->FindSocket(TEXT("HandGrip_R"));
        UE_LOG(LogTemp,Display,TEXT("ER_COOK_WORLD_GRIP bone=%s hand=%s socket=%s bounds=%s"),Grip?*Grip->BoneName.ToString():TEXT("none"),
            *C->GetMesh()->GetSocketLocation(TEXT("hand_r")).ToString(),*C->GetMesh()->GetSocketLocation(TEXT("HandGrip_R")).ToString(),*C->WorldRifle->Bounds.Origin.ToString());
        Check(FVector::Dist(C->WorldRifle->Bounds.Origin,C->GetMesh()->GetSocketLocation(TEXT("hand_r")))<35.f,TEXT("visible_cooked_grenade_stays_in_raised_hand"));
        Capture(TEXT("Cook03_RemoteReady.png"));
        Advance(7,.4f);
    }
    else if(Phase==7)
    {
        Check(CookProjectile.IsValid() && !CookProjectile->bHeld && !CookWeapon->bCookingGrenade,TEXT("remote_release_launches_on_server"));
        Check(CookProjectile.IsValid() && FMath::IsNearlyEqual(OriginalFuseEnd,CookProjectile->FuseEndServerTime,.001),TEXT("remote_cook_preserves_deadline"));
        Advance(8,FMath::Max(.1,OriginalFuseEnd-Now+.5));
    }
    else if(Phase==8)
    {
        Check(!CookProjectile.IsValid(),TEXT("remote_cooked_projectile_expires"));
        C->ServerSelectWeapon(4);Place(C,FVector(2400,550,92.5));C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        for(int32 Kind=0;Kind<2;++Kind)
        {
            const FVector Origin(-1000,Kind?1800:-1800,8000);
            const float Distances[]={150,200,300,400,550,600};
            const float Healths[]={0,0,37,72,98.25f,100};
            for(int32 I=0;I<8;++I)
            {
                const FVector Offset=I<6?FVector(Distances[I],0,0):FVector(400,I==6?400:-400,0);
                FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                auto* T=GetWorld()->SpawnActor<AERCharacter>(AERCharacter::StaticClass(),Origin+Offset,FRotator::ZeroRotator,Params);
                T->Health->ProtectionEndServerTime=0;T->Health->Armor=100;Place(T,Origin+Offset);
                DamageTargets.Add(T);ExpectedHealth.Add(I<6?Healths[I]:I==6?100:-1);
            }
            Wall(Origin+FVector(200,200,0),FVector(30,250,500));
            Wall(Origin+FVector(200,-200,-30),FVector(30,250,100));
            auto* P=GetWorld()->SpawnActorDeferred<AERProjectile>(Local->Weapon->ProjectileClass,FTransform(FRotator::ZeroRotator,Origin),Local->Weapon,Local,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            P->Payload=Kind?EERPayload::LaunchedHE:EERPayload::HE;P->Damage=140;P->ExplosionRadius=600;P->ExplosionInnerRadius=200;P->FuseDuration=.05;
            P->Movement->InitialSpeed=P->Movement->MaxSpeed=0;P->Movement->Velocity=FVector::ZeroVector;
            UGameplayStatics::FinishSpawningActor(P,P->GetActorTransform());
        }
        Advance(9,.35f);
    }
    else if(Phase==9)
    {
        for(int32 I=0;I<DamageTargets.Num();++I)
        {
            const auto* T=DamageTargets[I].Get();
            if(ExpectedHealth[I]>=0) Check(T && FMath::IsNearlyEqual(T->Health->Health,ExpectedHealth[I],.3f),
                I%8<2?TEXT("inner_blast_is_lethal_with_full_armor"):I%8==6?TEXT("solid_cover_blocks_all_fragmentation_damage"):TEXT("he_and_milkor_apply_falloff_to_six_metres"));
            else Check(T && T->Health->Health>0 && T->Health->Health<100,TEXT("partial_cover_reduces_exposure_damage"));
        }
        for(const auto& T:DamageTargets) if(T.IsValid()) T->Destroy();DamageTargets.Reset();
        // A live grenade survives the holder's death without a fresh fuse.
        auto* D=GetWorld()->SpawnActor<AERCharacter>(FVector(0,0,10000),FRotator::ZeroRotator);
        D->Weapon->Data=DuplicateObject<UERWeaponData>(Local->Inventory[8]->Data,D->Weapon);
        D->Weapon->Data->Damage=0;D->Weapon->Data->FuseDuration=.7f;D->Weapon->MagazineAmmo=1;
        D->Health->ProtectionEndServerTime=0;D->Health->Armor=0;D->Weapon->StartFire();
        CookProjectile=D->Weapon->CookedGrenade;OriginalFuseEnd=CookProjectile.IsValid()?CookProjectile->FuseEndServerTime:0;
        D->Health->ReceiveDamage(100,nullptr);
        Check(CookProjectile.IsValid() && !CookProjectile->bHeld && FMath::IsNearlyEqual(OriginalFuseEnd,CookProjectile->FuseEndServerTime,.001),TEXT("death_drops_armed_grenade_without_refund_or_fuse_reset"));
        DropStartZ=CookProjectile.IsValid()?CookProjectile->GetActorLocation().Z:0;
        D->Destroy();Advance(10,.25f);
    }
    else if(Phase==10)
    {
        Check(CookProjectile.IsValid() && CookProjectile->GetActorLocation().Z<DropStartZ-10,TEXT("dropped_grenade_accelerates_under_real_gravity"));
        Advance(11,.95f);
    }
    else if(Phase==11)
    {
        Check(!CookProjectile.IsValid(),TEXT("dropped_live_grenade_finishes_fuse"));
        Local->ServerSelectWeapon(8);Local->Health->Health=100;Local->Health->Armor=100;Local->Health->ProtectionEndServerTime=0;
        CookWeapon=Local->Weapon;Local->SetGrenadeInputHeld(true);Local->ServerStartFire();CookProjectile=CookWeapon->CookedGrenade;
        Check(CookProjectile.IsValid() && CookProjectile->bHeld,TEXT("second_grenade_can_be_cooked"));
        OriginalFuseEnd=CookProjectile.IsValid()?CookProjectile->FuseEndServerTime:Now+.1;
        Advance(12,FMath::Max(.1,OriginalFuseEnd-Now+.15));
    }
    else if(Phase==12)
    {
        Check(Local->Health->bDead && !CookWeapon->bCookingGrenade,TEXT("overcook_detonates_in_hand_and_defeats_full_armor"));
        Advance(13,.3f);
    }
    else if(Phase==13)
    {
        C->ServerSelectWeapon(4);Place(C,FVector(-250,0,11922.5));C->Health->ProtectionEndServerTime=0;
        auto* Data=DuplicateObject<UERWeaponData>(C->Weapon->Data,this);
        Data->bUseWorksheetAccuracy=false; // This fixture deliberately requires an exact zero-spread ray.
        Data->StandingSpreadDegrees=Data->RunningSpreadDegrees=Data->AirborneSpreadDegrees=Data->MaxBloomDegrees=Data->BloomPerShotDegrees=0;
        Data->Damage=1;C->Weapon->Data=Data;C->Weapon->MagazineAmmo=20;
        const FTransform T(FRotator::ZeroRotator,FVector(0,0,12000));
        auto* P=GetWorld()->SpawnActorDeferred<AERProjectile>(C->Weapon->ProjectileClass,T,C->Weapon,C,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        P->Payload=EERPayload::HE;P->Damage=0;P->FuseDuration=8;
        P->Movement->InitialSpeed=P->Movement->MaxSpeed=0;P->Movement->Velocity=FVector::ZeroVector;
        UGameplayStatics::FinishSpawningActor(P,T);P->Movement->Deactivate();ShotGrenade=P;
        ShotGrenadeCover=Wall(FVector(-120,0,12000),FVector(10,80,80));
        Remote->SetControlRotation((P->GetActorLocation()-C->Camera->GetComponentLocation()).Rotation());
        C->Weapon->StartFire();C->Weapon->StopFire();
        Check(!P->HasDetonated(),TEXT("solid_cover_blocks_shooting_an_active_grenade"));
        ShotGrenadeCover->Destroy();Advance(14,.2f);
    }
    else if(Phase==14)
    {
        Remote->SetControlRotation((ShotGrenade->GetActorLocation()-C->Camera->GetComponentLocation()).Rotation());
        C->Weapon->StartFire();C->Weapon->StopFire();
        Check(ShotGrenade.IsValid() && ShotGrenade->HasDetonated(),TEXT("authorized_bullet_detonates_active_thrown_grenade_before_fuse"));
        auto* Holder=GetWorld()->SpawnActor<AERCharacter>(Local->GetClass(),FVector(0,500,12000),FRotator::ZeroRotator);
        ShotGrenadeHolder=Holder;Holder->InitializeLoadout(4);Place(Holder,FVector(0,500,12000));Holder->Health->ProtectionEndServerTime=0;
        Holder->ServerSelectWeapon(8);Holder->Weapon->Data=DuplicateObject<UERWeaponData>(Holder->Weapon->Data,this);Holder->Weapon->Data->Damage=0;
        Holder->Weapon->StartFire();ShotGrenade=Holder->Weapon->CookedGrenade;
        Check(ShotGrenade.IsValid() && ShotGrenade->bHeld,TEXT("armed_grenade_in_hand_has_query_target"));
        Place(C,FVector(0,750,12000));Advance(15,.25f);
    }
    else if(Phase==15)
    {
        Remote->SetControlRotation((ShotGrenade->GetActorLocation()-C->Camera->GetComponentLocation()).Rotation());
        C->Weapon->StartFire();C->Weapon->StopFire();
        Check(ShotGrenade.IsValid() && ShotGrenade->HasDetonated(),TEXT("authorized_bullet_detonates_grenade_while_cooking_in_hand"));
        Check(ShotGrenadeHolder.IsValid() && !ShotGrenadeHolder->Weapon->bCookingGrenade && !ShotGrenadeHolder->Weapon->CookedGrenade,TEXT("shot_held_grenade_clears_cook_without_refund_or_duplicate_throw"));
        Advance(16,.3f);
    }
    else if(Phase==16)
    {
        auto Spawn=[&](FVector Position,EERPayload Kind)
        {
            const FTransform T(FRotator::ZeroRotator,Position);
            auto* P=GetWorld()->SpawnActorDeferred<AERProjectile>(C->Weapon->ProjectileClass,T,C->Weapon,C,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            P->Payload=Kind;P->Damage=.01f;P->FuseDuration=8;P->ExplosionRadius=600;
            P->Movement->InitialSpeed=P->Movement->MaxSpeed=0;P->Movement->Velocity=FVector::ZeroVector;
            UGameplayStatics::FinishSpawningActor(P,T);P->Movement->Deactivate();ChainGrenades.Add(P);return P;
        };
        Spawn(FVector(0,0,16000),EERPayload::HE);
        Spawn(FVector(450,0,16000),EERPayload::LaunchedHE);
        Spawn(FVector(900,0,16000),EERPayload::HE); // Outside the first blast: requires a second hop.
        Spawn(FVector(450,280,16000),EERPayload::HE);
        ShotGrenadeCover=Wall(FVector(450,140,16000),FVector(1300,20,1000));
        auto* Holder=GetWorld()->SpawnActor<AERCharacter>(Local->GetClass(),FVector(900,-200,15926.5),FRotator::ZeroRotator);
        ShotGrenadeHolder=Holder;Holder->InitializeLoadout(4);Place(Holder,FVector(900,-200,15926.5));Holder->Health->ProtectionEndServerTime=0;
        Holder->ServerSelectWeapon(8);Holder->Weapon->Data=DuplicateObject<UERWeaponData>(Holder->Weapon->Data,this);
        Holder->Weapon->Data->Damage=.01f;Holder->Weapon->Data->FuseDuration=8;Holder->Weapon->StartFire();
        ChainGrenades.Add(Holder->Weapon->CookedGrenade);
        Place(C,FVector(-250,0,15922.5));Remote->SetControlRotation((ChainGrenades[0]->GetActorLocation()-C->Camera->GetComponentLocation()).Rotation());
        C->Weapon->StartFire();C->Weapon->StopFire();
        Check(ChainGrenades[0]->HasDetonated() && !ChainGrenades[1]->HasDetonated(),TEXT("chain_is_queued_without_recursive_detonation"));
        Advance(17,.15f);
    }
    else if(Phase==17)
    {
        Check(ChainGrenades[1].IsValid() && ChainGrenades[1]->HasDetonated(),TEXT("blast_detonates_nearby_armed_milkor"));
        Check(ChainGrenades[2].IsValid() && ChainGrenades[2]->HasDetonated(),TEXT("chain_reaches_grenade_outside_initial_blast_radius"));
        Check(ChainGrenades[3].IsValid() && !ChainGrenades[3]->HasDetonated(),TEXT("solid_cover_blocks_every_hop_of_grenade_chain"));
        Check(ChainGrenades[4].IsValid() && ChainGrenades[4]->HasDetonated() && !ShotGrenadeHolder->Weapon->bCookingGrenade,TEXT("chain_detonates_exposed_held_grenade_and_clears_cook"));
        // Real swept projectile/capsule contacts, isolated from the chain fixture.
        for(int32 Index=0;Index<4;++Index)
        {
            const FVector Position(0,Index*1000,20000);
            auto* Target=GetWorld()->SpawnActor<AERCharacter>(Local->GetClass(),Position,FRotator::ZeroRotator);
            Place(Target,Position);Target->Health->ProtectionEndServerTime=0;Target->Health->Armor=0;
            Target->Tags.Add(FName(*FString::Printf(TEXT("ContactTarget%d"),Index)));
            FTransform Transform(FRotator::ZeroRotator,Position-FVector(200,0,0));
            auto* Grenade=GetWorld()->SpawnActorDeferred<AERProjectile>(AERProjectile::StaticClass(),Transform,Local,Local);
            Grenade->Payload=Index==0?EERPayload::LaunchedHE:Index==1?EERPayload::HE:Index==2?EERPayload::Smoke:EERPayload::Stun;
            Grenade->Damage=140;Grenade->FuseDuration=3.5f;
            Grenade->Movement->Velocity=FVector(1000,0,0);Grenade->Movement->InitialSpeed=1000;
            Grenade->Tags.Add(FName(*FString::Printf(TEXT("ContactGrenade%d"),Index)));
            Grenade->FinishSpawning(Transform);
        }
        Advance(18,.3f);
    }
    else if(Phase==18)
    {
        for(int32 Index=0;Index<4;++Index)
        {
            AERCharacter* Target=nullptr;AERProjectile* Grenade=nullptr;
            for(TActorIterator<AERCharacter> It(GetWorld());It;++It)
                if(It->ActorHasTag(FName(*FString::Printf(TEXT("ContactTarget%d"),Index))))Target=*It;
            for(TActorIterator<AERProjectile> It(GetWorld());It;++It)
                if(It->ActorHasTag(FName(*FString::Printf(TEXT("ContactGrenade%d"),Index))))Grenade=*It;
            Check(Target && Grenade && Grenade->BounceCount>0,*FString::Printf(TEXT("real_player_contact_%d"),Index));
            if(!Target || !Grenade)continue;
            Check(FMath::IsNearlyEqual(Target->Health->Health,Index==0?65.f:100.f,.1f),*FString::Printf(TEXT("only_launcher_contact_deals_quarter_damage_%d"),Index));
            Check(FMath::Abs(Grenade->Movement->Velocity.X)<80.f && !Grenade->HasDetonated(),*FString::Printf(TEXT("player_contact_soft_bounce_keeps_fuse_%d"),Index));
            FHitResult Contact(Target,Target->GetCapsuleComponent(),Target->GetActorLocation(),-FVector::ForwardVector);
            Grenade->Hit(Grenade->Collision,Target,Target->GetCapsuleComponent(),FVector::ZeroVector,Contact);
            Check(FMath::IsNearlyEqual(Target->Health->Health,Index==0?65.f:100.f,.1f),TEXT("repeated_contact_cannot_stack_damage"));
            Grenade->Destroy();Target->Destroy();
        }
        UE_LOG(LogTemp,Display,TEXT("ER_GRENADE_TEST_COMPLETE result=%s"),bFailed?TEXT("FAIL"):TEXT("PASS"));
        FPlatformMisc::RequestExitWithStatus(false,bFailed?1:0);Advance(19,1000);
    }
#endif
}
