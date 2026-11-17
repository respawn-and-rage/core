#include "ERGrenadeProbe.h"
#include "ERCharacter.h"
#include "ERMatch.h"
#include "ERWeapon.h"
#include "ERSmokeCloud.h"
#include "ERGrenadeExplosion.h"
#include "ERPlayerController.h"
#include "Camera/CameraActor.h"
#include "Engine/StaticMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "ShaderCompiler.h"

AERGrenadeProbe::AERGrenadeProbe()
{ bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork; }
void AERGrenadeProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AERGrenadeProbe,Phase); }
void AERGrenadeProbe::Check(bool Value,const TCHAR* Label)
{ bFailed|=!Value;UE_LOG(LogTemp,Display,TEXT("ER_GRENADE_TEST_%s %s"),Value?TEXT("PASS"):TEXT("FAIL"),Label); }
void AERGrenadeProbe::Advance(int32 Next,float Delay)
{ Phase=Next;Deadline=GetWorld()->GetTimeSeconds()+Delay;ForceNetUpdate(); }
void AERGrenadeProbe::Capture(const TCHAR* Name)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("ERGrenadeRender")))
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Testing/Grenades")/Name,false,false);
}
void AERGrenadeProbe::Fixture()
{
    bFixture=true;
    auto Box=[this](FVector P,FVector Size)
    {
        auto* A=GetWorld()->SpawnActor<AStaticMeshActor>(P+FVector(0,0,5000),FRotator::ZeroRotator);
        auto* M=A->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);
        M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        M->SetWorldScale3D(Size/100.f);M->SetCollisionObjectType(ECC_WorldStatic);
        M->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        M->SetCollisionResponseToAllChannels(ECR_Block);return A;
    };
    Box(FVector(0,0,-15),FVector(1400,1400,30));
    // Seal above the complete 4m volume: a 3.5m wall permits a legitimate
    // connected route over its top with the wider horizontal smoke footprint.
    Box(FVector(0,-350,225),FVector(20,500,450));
    Box(FVector(0,350,225),FVector(20,500,450));
    Box(FVector(0,0,35),FVector(20,200,70));
    Box(FVector(0,0,330),FVector(20,200,240));
    Seal=Box(FVector(0,0,140),FVector(20,200,140));
    if(FParse::Param(FCommandLine::Get(),TEXT("ERGrenadeRender")))
        if(auto* PC=GetWorld()->GetFirstPlayerController();PC && PC->IsLocalController())
        {
            const FVector Position(-610,-690,5370);
            auto* Camera=GetWorld()->SpawnActor<ACameraActor>(Position,(FVector(-80,0,5100)-Position).Rotation());
            RenderCamera=Camera;
            PC->SetViewTarget(Camera);
        }
}
void AERGrenadeProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("ERGrenadeCombatProbe"))) { TickCombat(DeltaSeconds);return; }
    if(!bFixture) Fixture();
    const double Now=GetWorld()->GetTimeSeconds();
    if(!HasAuthority())
    {
        for(TActorIterator<AERProjectile> It(GetWorld());It;++It)
        {
            const FVector RootPosition=It->GetActorLocation();
            if (RootPosition.Z>5050 && !It->HasDetonated() &&
                (It->Payload==EERPayload::HE || It->Payload==EERPayload::LaunchedHE))
            {
                FFlightSample& Sample=RemoteFlight[It->Payload==EERPayload::LaunchedHE?1:0];
                const FVector MeshPosition=It->Visual->GetComponentLocation();
                // With no new position from the server, only the rendered mesh should advance.
                if (!Sample.bMovedBetweenUpdates && Sample.bHasSample && It->GetActorTransform().Equals(Sample.RootTransform) &&
                    !MeshPosition.Equals(Sample.MeshPosition,.1f))
                {
                    Sample.bMovedBetweenUpdates=true;
                    UE_LOG(LogTemp,Display,TEXT("ER_GRENADE_REMOTE_INTERPOLATED kind=%s"),
                        It->Payload==EERPayload::LaunchedHE?TEXT("launcher"):TEXT("hand"));
                }
                Sample.bHasSample=true;
                Sample.RootTransform=It->GetActorTransform();
                Sample.MeshPosition=MeshPosition;
            }
            if(!bRemoteSpin && RootPosition.Z>4900 && AERProjectile::UsesFuse(It->Payload))
            {
                if(!RemoteGrenade.IsValid()) { RemoteGrenade=*It;RemoteRotation=It->GetActorQuat(); }
                else if(RemoteGrenade.Get()==*It && !RemoteRotation.Equals(It->GetActorQuat(),.05f))
                {
                    bRemoteSpin=true;
                    UE_LOG(LogTemp,Display,TEXT("ER_GRENADE_REMOTE_TUMBLE kind=%d"),int32(It->Payload));
                }
            }
        }
        if(Phase>=6 && SeenPhase<6)
        {
            if(Seal.IsValid()) Seal->Destroy();
            for(TActorIterator<AERSmokeCloud> It(GetWorld());It;++It)
                if(It->GetActorLocation().Z>4900) It->RebuildContainment();
        }
        if(Phase>=7 && SeenPhase<7)
            for(TActorIterator<AERSmokeCloud> It(GetWorld());It;++It)
                if(It->GetActorLocation().Z>4900)
                {
                    UE_LOG(LogTemp,Display,TEXT("ER_GRENADE_REMOTE_WINDOW filled=%d through=%d solid=%d"),It->FilledCellCount,
                        It->ContainsSmokeAt(FVector(60,0,5140)),It->ContainsSmokeAt(FVector(0,160,5100)));
                    UE_LOG(LogTemp,Display,TEXT("ER_SMOKE_REMOTE_TEAM alpha=%d omega=%d color=%s"),
                        It->SmokeColor.Equals(AERSmokeCloud::ColorForTeam(EERTeam::Alpha)),
                        It->SmokeColor.Equals(AERSmokeCloud::ColorForTeam(EERTeam::Omega)),*It->SmokeColor.ToString());
                }
        SeenPhase=Phase;return;
    }
    if(Now<Deadline || Now<4) return;
    auto* PC=Cast<AERPlayerController>(GetWorld()->GetFirstPlayerController());
    auto* C=PC?Cast<AERCharacter>(PC->GetPawn()):nullptr;
    if(!C || !C->Weapon) return;
    if(Phase==0)
    {
        if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
        int32 Players=0;for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It) ++Players;
        if(Players<2) return;
        Check(C->Inventory.Num()==10 && FMath::IsNearlyEqual(C->Inventory[7]->Tuned()->ProjectileSpeed,1750.f),TEXT("standard_launcher_speed"));
        Check(C->Inventory.Num()==10 && FMath::IsNearlyEqual(C->Inventory[8]->Tuned()->ProjectileSpeed,1200.f) && FMath::IsNearlyEqual(C->Inventory[9]->Tuned()->ProjectileSpeed,1200.f),TEXT("standard_hand_grenade_speed"));
        Check(C->ReferenceArsenal[8]->LaunchRangeMultiplier==1.2f && C->ReferenceArsenal[9]->LaunchRangeMultiplier==1.2f,TEXT("hand_range_bonus_twenty_percent"));
        Check(C->Inventory.Num()==10 && C->Inventory[8]->MagazineAmmo==10 && C->Inventory[9]->MagazineAmmo==10,TEXT("ten_he_and_ten_smoke_for_testing"));
        if(auto* Defaults=C->Weapon->ProjectileClass->GetDefaultObject<AERProjectile>();Defaults->GrenadeExplosionClass)
        {
            auto* Ground=GetWorld()->SpawnActor<AERGrenadeExplosion>(Defaults->GrenadeExplosionClass,FVector(-350,350,5008),FRotator::ZeroRotator);
            auto* Air=GetWorld()->SpawnActor<AERGrenadeExplosion>(Defaults->GrenadeExplosionClass,FVector(-350,350,5350),FRotator::ZeroRotator);
            Check(Ground && Ground->GroundFragmentCount>=3 && Ground->GroundFragmentCount<=6,TEXT("ground_blast_spawns_few_random_floor_chips"));
            Check(Air && Air->GroundFragmentCount==0,TEXT("air_blast_has_no_floor_chips"));
            if(Ground) Ground->Destroy();if(Air) Air->Destroy();
        }
        for(int32 I=0;I<2;++I)
        {
            const FTransform T(FRotator::ZeroRotator,FVector(-450,I?130:-130,5200));
            auto* P=GetWorld()->SpawnActorDeferred<AERProjectile>(C->Weapon->ProjectileClass,T,C->Weapon,C,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            P->Payload=I?EERPayload::LaunchedHE:EERPayload::HE;P->Damage=0;P->FuseDuration=2.4f;
            P->ThrownMesh=I?C->Arsenal[7]->ProjectileMesh:C->Arsenal[8]->Mesh;
            P->Movement->InitialSpeed=P->Movement->MaxSpeed=1000;P->Movement->Velocity=FVector(1000,0,-100);
            P->SetNetUpdateFrequency(15); // Leave client frames between snapshots to measure visual motion.
            UGameplayStatics::FinishSpawningActor(P,T);Grenades.Add(P);InitialRotations.Add(P->GetActorQuat());
        }
        Advance(1,.85f);
    }
    else if(Phase==1)
    {
        for(int32 I=0;I<Grenades.Num();++I)
        {
            auto* P=Grenades[I].Get();
            Check(P && P->Movement->Bounciness>=.18f && P->Movement->Bounciness<=.25f && P->Movement->Friction==.4f,TEXT("reduced_grenade_bounce_and_slide"));
            Check(P && P->BounceCount>0,I?TEXT("launcher_bounces_without_impact_detonation"):TEXT("he_bounces_without_impact_detonation"));
            Check(P && !P->GetActorQuat().Equals(InitialRotations[I],.05f),TEXT("server_tumbles_grenade_during_flight"));
        }
        Capture(TEXT("01_Bounce.png"));Advance(2,1.65f);
    }
    else if(Phase==2)
    {
        Check(Grenades[0].IsValid() && Grenades[1].IsValid(),TEXT("projectiles_remain_for_reliable_fuse_multicast"));
        Capture(TEXT("02_HE.png"));Advance(3,.8f);
    }
    else if(Phase==3)
    {
        Capture(TEXT("02_HE_After.png"));
        Check(!Grenades[0].IsValid() && !Grenades[1].IsValid(),TEXT("both_projectiles_expire_after_their_fuses"));
        auto* State=C->GetPlayerState<AERPlayerState>();const EERTeam OriginalTeam=State->Team;
        for(int32 I=0;I<2;++I)
        {
            State->Team=I?EERTeam::Omega:EERTeam::Alpha;
            const FTransform T(FRotator::ZeroRotator,FVector(I?-1600:-100,0,5003));
            auto* P=GetWorld()->SpawnActorDeferred<AERProjectile>(C->Weapon->ProjectileClass,T,C->Weapon,C,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            P->Payload=EERPayload::Smoke;P->Damage=0;P->FuseDuration=.05f;
            P->Movement->InitialSpeed=P->Movement->MaxSpeed=0;P->Movement->Velocity=FVector::ZeroVector;
            UGameplayStatics::FinishSpawningActor(P,T);
        }
        State->Team=OriginalTeam; // The armed projectiles must retain their captured colors.
        Advance(4,1.5f);
    }
    else if(Phase==4)
    {
        bool Omega=false;
        for(TActorIterator<AERSmokeCloud> It(GetWorld());It;++It)
        {
            if(FMath::Abs(It->GetActorLocation().X+100)<10 && It->GetActorLocation().Z>4900) Cloud=*It;
            if(It->GetActorLocation().X<-1500 && It->GetActorLocation().Z>4900) Omega|=It->SmokeColor.Equals(AERSmokeCloud::ColorForTeam(EERTeam::Omega));
        }
        Check(Cloud.IsValid() && Cloud->SmokeColor.Equals(AERSmokeCloud::ColorForTeam(EERTeam::Alpha)),TEXT("alpha_smoke_retains_thrower_team_color"));
        Check(Omega,TEXT("omega_smoke_retains_thrower_team_color_after_team_restore"));
        if(Cloud.IsValid()) Cloud->AddShot(Cloud->VolumeOrigin+FVector(-100,0,100),Cloud->VolumeOrigin+FVector(-50,0,100));
        Check(Cloud.IsValid() && Cloud->SimSize.Equals(FVector(880,880,400)),TEXT("smoke_4_4_metre_radius_four_metre_height"));
        Check(Cloud.IsValid() && !Cloud->ShapePhase.IsNearlyZero(),TEXT("smoke_has_randomized_shape_phase"));
        Check(Cloud.IsValid() && !Cloud->ContainsSmokeAt(Cloud->VolumeOrigin+FVector(441,0,10)),TEXT("smoke_stays_within_4_4_metre_radius"));
        Check(Cloud.IsValid() && Cloud->ContainsSmokeAt(FVector(-100,0,5100)),TEXT("smoke_fills_source_side"));
        Check(Cloud.IsValid() && !Cloud->ContainsSmokeAt(FVector(60,0,5140)),TEXT("closed_wall_blocks_smoke"));
        Check(Cloud.IsValid() && !Cloud->ContainsSmokeAt(FVector(-100,0,4990)),TEXT("hemisphere_stays_above_floor"));
        Capture(TEXT("03_ClosedWall.png"));
        Advance(5,.4f);
    }
    else if(Phase==5)
    {
        if(Seal.IsValid()) Seal->Destroy();
        if(Cloud.IsValid()) Cloud->RebuildContainment();
        Advance(6,1.f);
    }
    else if(Phase==6)
    {
        Check(Cloud.IsValid() && Cloud->GetActiveShotRadius()>5.f,TEXT("bullet_opening_refills_slowly_after_one_second"));
        Check(Cloud.IsValid() && Cloud->ContainsSmokeAt(FVector(60,0,5140)),TEXT("open_window_passes_smoke"));
        Check(Cloud.IsValid() && !Cloud->ContainsSmokeAt(FVector(0,160,5100)),TEXT("solid_wall_cells_remain_empty"));
        Capture(TEXT("04_OpenWindow.png"));
        if(Cloud.IsValid())
        {
            const FTransform T(FRotator::ZeroRotator,Cloud->VolumeOrigin+FVector(-100,0,140));
            auto* P=GetWorld()->SpawnActorDeferred<AERProjectile>(C->Weapon->ProjectileClass,T,C->Weapon,C,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            P->Payload=EERPayload::HE;P->Damage=0;P->FuseDuration=.55f;
            P->Movement->InitialSpeed=P->Movement->MaxSpeed=300;P->Movement->Velocity=FVector(300,0,0);
            UGameplayStatics::FinishSpawningActor(P,T);
        }
        Advance(7,.4f);
    }
    else if(Phase==7)
    {
        Check(Cloud.IsValid() && Cloud->ProjectileInteractionCount>0,TEXT("moving_grenade_opens_temporary_smoke_wake"));
        if(RenderCamera.IsValid())
        {
            RenderCamera->SetActorLocation(FVector(-140,-20,5100));
            RenderCamera->SetActorRotation(FRotator::ZeroRotator);
        }
        Advance(8,.4f);
    }
    else if(Phase==8)
    {
        Check(Cloud.IsValid() && Cloud->BlastInteractionCount>0,TEXT("detonation_temporarily_pushes_smoke_aside"));
        Check(Cloud.IsValid() && Cloud->LastBlastRadius==400.f && Cloud->LastBlastLifetime>5.f,TEXT("he_dispersion_last_until_cloud_expiry"));
        Check(Cloud.IsValid() && Cloud->GetActiveShotRadius()==0,TEXT("bullet_opening_eventually_refills"));
        if(Cloud.IsValid()) for(int32 I=0;I<5;++I) Cloud->AddShot(Cloud->VolumeOrigin+FVector(-100,0,100),Cloud->VolumeOrigin+FVector(-50,0,100));
        Capture(TEXT("05_InsideSmoke.png"));Advance(10,3.f);
    }
    else if(Phase==9)
    {
        Check(!Cloud.IsValid(),TEXT("replicated_smoke_expires_after_configured_duration"));
        UE_LOG(LogTemp,Display,TEXT("ER_GRENADE_TEST_COMPLETE result=%s"),bFailed?TEXT("FAIL"):TEXT("PASS"));
        FPlatformMisc::RequestExitWithStatus(false,bFailed?1:0);Advance(10,1000);
    }
    else if(Phase==10)
    {
        Check(Cloud.IsValid() && Cloud->GetActiveBlastRadius()>=380.f,TEXT("he_displaced_smoke_barely_regenerates_after_three_seconds"));
        Capture(TEXT("06_HE_Dispersed.png"));
        const double Expiry = Cloud.IsValid() ? Cloud->StartedAtServerTime + Cloud->Duration : Now;
        Advance(9, static_cast<float>(FMath::Max(0.0, Expiry - Now) + .25));
    }
#endif
}
