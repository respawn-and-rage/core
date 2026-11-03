#include "ERIndustrialMapProbe.h"
#include "ERIndustrialFence.h"
#include "ERIndustrialMapTestUtils.h"
#include "ERProceduralMap.h"
#include "ERCharacter.h"
#include "ERMovement.h"
#include "ERLadder.h"
#include "ERPlayerController.h"
#include "ERWeapon.h"
#include "ERMatch.h"
#include "ERBotController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "TimerManager.h"
AERIndustrialMapProbe::AERIndustrialMapProbe(){bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;}
void AERIndustrialMapProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps)const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AERIndustrialMapProbe,Phase);DOREPLIFETIME(AERIndustrialMapProbe,ExpectedDigest);}
void AERIndustrialMapProbe::Check(bool Value,const TCHAR* Label)
{Failed|=!Value;UE_LOG(LogTemp,Display,TEXT("ER_INDUSTRIAL_PROBE_%s %s"),Value ? TEXT("PASS"):TEXT("FAIL"),Label);}
void AERIndustrialMapProbe::Publish(int32 Next,AERProceduralMap* Map)
{Phase=Next;ExpectedDigest=Map->GetStateDigest();ForceNetUpdate();UE_LOG(LogTemp,Display,TEXT("ER_INDUSTRIAL_STATE phase=%d digest=%u epoch=%u"),Phase,ExpectedDigest,Map->GetMapEpoch());}
void AERIndustrialMapProbe::Capture(AERProceduralMap* Map,int32 View)
{
    if(!FApp::CanEverRender())return;
    auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0);if(!PC)return;
    auto* Camera=GetWorld()->SpawnActor<ACameraActor>();const auto& L=Map->GetLayout();
    const bool Ground=View!=0;
    Camera->SetActorLocation(Ground ? FVector(-L.ArenaSize.X*.35,-L.CentralBuildingSize.Y*.5-700,210):FVector(0,0,9000));
    Camera->SetActorRotation(Ground ? FRotator(-3,40,0):FRotator(-90,90,0));
    Camera->GetCameraComponent()->FieldOfView=Ground ? 85:68;PC->SetViewTarget(Camera);
    if(View==2 && !L.Containers.IsEmpty())
    {
        const auto& C=L.Containers[0];Camera->SetActorLocation(C.Frame.TransformPosition({0,C.Size.Y*.5+700,150}));
        Camera->SetActorRotation((C.Frame.TransformPosition({0,0,0})-Camera->GetActorLocation()).Rotation());
    }
    if(View==3)
    {
        // Inspect a real concrete face with an unobstructed view, avoiding the
        // timber cover that often fills the ordinary ground camera.
        for(const auto& P:Map->GetCellWorld().GetPanels())if(P.Material==EERBlockMaterial::Concrete && P.Surface && P.Size.X>=200 && P.Size.Z>=150)
        {
            const FVector Target=P.Transform.TransformPosition(P.Size*.5),Normal=P.Transform.GetRotation().GetAxisY();
            bool Found=false;
            for(float Side:{-1.f,1.f})
            {
                const FVector Eye=Target+Normal*Side*450;FHitResult Hit;
                if(GetWorld()->LineTraceSingleByChannel(Hit,Eye,Target,ECC_Visibility) && Hit.GetComponent() && Hit.GetComponent()->GetName().StartsWith(FString::Printf(TEXT("Cells_P%d_C"),P.Id)))
                {Camera->SetActorLocation(Eye);Camera->SetActorRotation((Target-Eye).Rotation());Found=true;break;}
            }
            if(Found)break;
        }
    }
    if(View==4)
    {
        const auto* Fence=L.FixedPieces.FindByPredicate([&](const auto& P){return P.PawnBarrier && P.Transform.GetLocation().Y<-L.ArenaSize.Y*.5+10 && P.Transform.GetLocation().Z<ERIndustrialFence::PlinthHeightCm+ERIndustrialFence::ModuleHeightCm;});
        if(Fence)
        {
            const FVector Target(Fence->Transform.GetLocation().X+180,Fence->Transform.GetLocation().Y,(ERIndustrialFence::PlinthHeightCm+ERIndustrialFence::ModuleHeightCm)*.5f);
            Camera->SetActorLocation(Target+FVector(120,470,70));
            Camera->SetActorRotation((Target-Camera->GetActorLocation()).Rotation());
            Camera->GetCameraComponent()->FieldOfView=65;
        }
    }
    if(View==5)
    {
        const FVector Target=L.CentralBuildingCenter+FVector(0,0,L.CentralBuildingSize.Z*.4f);
        Camera->SetActorLocation(L.CentralBuildingCenter+FVector(-L.CentralBuildingSize.X*1.25f,-L.CentralBuildingSize.Y, L.CentralBuildingSize.Z+1000));
        Camera->SetActorRotation((Target-Camera->GetActorLocation()).Rotation());Camera->GetCameraComponent()->FieldOfView=65;
    }
    if(!Ground)
    {
        Camera->GetCameraComponent()->ProjectionMode=ECameraProjectionMode::Orthographic;
        Camera->GetCameraComponent()->OrthoWidth=FMath::Max(L.ArenaSize.X,L.ArenaSize.Y*16.f/9)*1.08;
    }
    const FString Filename=FPaths::ProjectSavedDir()/FString::Printf(TEXT("Testing/Industrial-%s-rule%d-seed%d.png"),View==5 ? TEXT("Architecture"):View==4 ? TEXT("Fence"):View==3 ? TEXT("Concrete"):View==2 ? TEXT("Container"):Ground ? TEXT("Ground"):TEXT("Plan"),int32(L.CrossingRule),Map->LayoutConfig.Seed);
    FTimerHandle ScreenshotTimer;GetWorldTimerManager().SetTimer(ScreenshotTimer,[Filename](){FScreenshotRequest::RequestScreenshot(Filename,false,false);},.35f,false);
}
void AERIndustrialMapProbe::Tick(float Delta)
{
    Super::Tick(Delta);Elapsed+=Delta;
    TActorIterator<AERProceduralMap> It(GetWorld());auto* Map=It ? *It:nullptr;
    if(!Map){if(Elapsed>60)FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(!HasAuthority())
    {
        if(Phase && SeenPhase!=Phase && Elapsed>NextDiagnostic)
        {
            NextDiagnostic=Elapsed+5;
            UE_LOG(LogTemp,Display,TEXT("ER_INDUSTRIAL_CLIENT_WAIT phase=%d ready=%d current=%d digest=%u expected=%u epoch=%u sequence=%u removed=%lld"),Phase,Map->IsMapReady(),Map->IsSnapshotCurrent(),Map->GetStateDigest(),ExpectedDigest,Map->GetMapEpoch(),Map->GetMapSequence(),Map->GetCellWorld().GetRemovedCount());
        }
        if(Phase && SeenPhase!=Phase && Map->IsSnapshotCurrent() && Map->GetStateDigest()==ExpectedDigest)
        {
            bool Good=Map->ValidateCells();FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERIndustrialPeer),false);
            for(TActorIterator<APawn> Pawn(GetWorld());Pawn;++Pawn)Q.AddIgnoredActor(*Pawn);
            Good&=ERIndustrialTest::FactoryPaletteMatches(*Map);
            Good&=ERIndustrialTest::IntermediateCoverMatches(*Map,Phase==2 || Phase==3);
            const auto& L=Map->GetLayout();
            Good&=ERIndustrialTest::PerimeterMatches(*Map,Q);
            Good&=ERIndustrialTest::FenceAperturesMatch(*Map,Q);
            if(!L.Containers.IsEmpty())Good&=ERIndustrialTest::ContainerBlocked(*GetWorld(),L.Containers[0],Q)==(Phase==1 || Phase==4);
            if(!L.WallBridges.IsEmpty())
            {
                Good&=ERIndustrialTest::WallBlocked(*GetWorld(),ERIndustrialTest::BreachPoint(L),Q)==(Phase==1 || Phase==4);
                if(Phase!=2)Good&=ERIndustrialTest::BridgeSupported(*GetWorld(),L.WallBridges[0],Q)==(Phase==1 || Phase==4);
            }
            for(const auto& Path:L.CrossingRoutes)for(int32 I=1;I<Path.Num();++I)
                Good&=!GetWorld()->SweepSingleByChannel(Hit,Path[I-1],Path[I],FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,92.5),Q);
            UE_LOG(LogTemp,Display,TEXT("ER_INDUSTRIAL_CLIENT_%s phase=%d digest=%u epoch=%u"),Good ? TEXT("PASS"):TEXT("FAIL"),Phase,ExpectedDigest,Map->GetMapEpoch());SeenPhase=Phase;
        }
        return;
    }
    auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0);auto* C=PC ? Cast<AERCharacter>(PC->GetPawn()):nullptr;
    if(!C || !C->Weapon)return;
    const bool Network=GetNetMode()!=NM_Standalone;
    // Fast material review uses intact production geometry and real Metal rendering.
    if(FParse::Param(FCommandLine::Get(),TEXT("ERFactoryFinishGallery")))
    {
        if(!Phase && Elapsed>3)
        {Check(ERIndustrialTest::FactoryPaletteMatches(*Map),TEXT("factory_palette_applied_to_rendered_metal"));Check(ERIndustrialTest::IntermediateCoverMatches(*Map),TEXT("intermediate_cover_access_and_concealment"));Capture(Map);Publish(1,Map);
            FTimerHandle ArchitectureTimer;GetWorldTimerManager().SetTimer(ArchitectureTimer,FTimerDelegate::CreateWeakLambda(this,[this,Map](){Capture(Map,5);}),1.5f,false);}
        if(Phase && Elapsed>7 && Gallery==0){Capture(Map,2);++Gallery;}
        if(Phase && Elapsed>11 && Gallery==1){Capture(Map,1);++Gallery;}
        if(Phase && Elapsed>15 && Gallery==2){Capture(Map,3);++Gallery;}
        if(Phase && Elapsed>19 && Gallery==3){Capture(Map,4);++Gallery;}
        if(Elapsed>23){UE_LOG(LogTemp,Display,TEXT("ER_INDUSTRIAL_PROBE_COMPLETE result=%s"),Failed ? TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);}
        return;
    }
    const float DamageAt=DamageTime,RegenAt=DamageTime+(Network ? 43:8),EndAt=DamageTime+(Network ? 70:18);
    if(Phase==0 && Elapsed>3)
    {
        Check(Map->IsMapReady() && Map->ValidateCells(),TEXT("initial_map_ready"));
        Check(ERIndustrialTest::FactoryPaletteMatches(*Map),TEXT("factory_palette_applied_to_rendered_metal"));
        FCollisionQueryParams FenceQ(SCENE_QUERY_STAT(ERFenceProbe),false);
        Check(ERIndustrialTest::FenceAperturesMatch(*Map,FenceQ),TEXT("steel_module_bars_and_apertures"));
        Check(ERIndustrialTest::IntermediateCoverMatches(*Map),TEXT("intermediate_cover_access_and_concealment"));
        int32 Crouched=0;for(const auto& Site:Map->GetLayout().CoverSites)Crouched+=Site.HalfHeight<70;
        UE_LOG(LogTemp,Display,TEXT("ER_INDUSTRIAL_COVER sites=%d standing=%d crouched=%d"),Map->GetLayout().CoverSites.Num(),Map->GetLayout().CoverSites.Num()-Crouched,Crouched);
        const auto SpawnPoint=C->GetActorLocation();
        const float SpawnGround=Map->GetLayout().CircularArena ? ERProcedural::CentralGroundHeight(Map->GetLayout(),FVector2D(SpawnPoint)):0;
        Check(SpawnPoint.Z-SpawnGround>90 && SpawnPoint.Z-SpawnGround<102,TEXT("actual_spawn_supported"));
        DamageTime=FMath::Max(Network ? 35.f:20.f,8+Map->GetLayout().StairRoutes.Num()*3.5f+Map->GetLayout().WallAccessFlights.Num()*2.5f);
        for(const auto& Desc:Map->GetLayout().Ladders)DamageTime+=Desc.Height/144.f+2.f;
        Capture(Map);Publish(1,Map);
    }
    if(Phase==1 && Elapsed<DamageAt)
    {
        const auto& Paths=Map->GetLayout().StairRoutes;
        if(Flight<Paths.Num())
        {
            const auto& Path=Paths[Flight];
            const FVector Direction=(Path.Last()-Path[0]).GetSafeNormal2D();
            const bool WallAccess=Map->GetLayout().WallAccessFlights.Contains(Flight);
            // Continue down the opposite stair: exercise a complete courtyard
            // crossing, rather than just teleporting away at its upper landing.
            const FVector Goal=WallAccess ? Path.Last()+Direction*700:Path.Last();
            if(!Walking)
            {
                C->TeleportTo(Path[0],Direction.Rotation());C->GetCharacterMovement()->StopMovementImmediately();
                C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);WalkStarted=Elapsed;WalkPeakZ=0;Walking=true;
            }
            WalkPeakZ=FMath::Max(WalkPeakZ,float(C->GetActorLocation().Z));
            if(FVector::DotProduct(C->GetActorLocation()-Goal,Direction)<-15)C->AddMovementInput(Direction);
            else
            {
                Check(WalkPeakZ>Path.Last().Z-25,TEXT("actual_pawn_walked_stair_flight"));
                if(const auto* Site=Map->GetLayout().CoverSites.FindByPredicate([&](const auto& Cover){return Cover.FiringStair==Flight;}))
                {
                    Check(FMath::Abs(C->GetActorLocation().Z-Site->FiringPosition.Z)<25,TEXT("actual_pawn_reached_cover_firing_platform"));
                    PC->SetControlRotation(Direction.Rotation());C->SetActorRotation(Direction.Rotation());
                    const FVector Eye=C->GetPawnViewLocation();FHitResult FiringHit;FCollisionQueryParams FiringQuery(SCENE_QUERY_STAT(ERCoverFiring),false,C);
                    Check(!GetWorld()->LineTraceSingleByChannel(FiringHit,Eye,Eye+Direction*160,ECC_GameTraceChannel1,FiringQuery),TEXT("actual_cover_firing_sight_clear"));
                    const auto ShotsBefore=C->Weapon->AuthorizedShotCount;C->Weapon->StartFire();C->Weapon->StopFire();
                    Check(C->Weapon->AuthorizedShotCount==ShotsBefore+1 && FVector::Dist2D(C->Weapon->LastShotTraceStart,C->Weapon->LastShotTraceEnd)>120,TEXT("actual_rifle_fires_over_cover"));
                }
                if(WallAccess)Check(C->GetActorLocation().Z>90 && C->GetActorLocation().Z<110,TEXT("actual_pawn_crossed_3m_wall_and_descended"));
                C->GetCharacterMovement()->StopMovementImmediately();++Flight;Walking=false;
            }
            if(Walking && Elapsed-WalkStarted>(WallAccess ? 6:4)){Check(false,TEXT("stair_flight_timeout"));++Flight;Walking=false;}
        }
        else if(LadderIndex<Map->GetLayout().Ladders.Num())
        {
            const auto& Desc=Map->GetLayout().Ladders[LadderIndex];
            const FVector Direction=-Desc.Frame.GetUnitAxis(EAxis::X);
            auto* M=Cast<UERMovement>(C->GetCharacterMovement());
            if(!LadderClimbing)
            {
                C->TeleportTo(Desc.Frame.TransformPosition({55,0,95}),Direction.Rotation(),false,true);
                M->StopMovementImmediately();M->SetMovementMode(MOVE_Walking);M->RetryAfter=0;M->bLadderNeedsRelease=false;
                PC->SetControlRotation(Direction.Rotation());WalkStarted=Elapsed;SawLadder=false;LadderClimbing=true;
            }
            const FVector Local=Desc.Frame.InverseTransformPosition(C->GetActorLocation());
            SawLadder|=M->IsOnLadder();
            if(SawLadder && !M->IsTraversing() && Local.X< -60 && Local.Z>Desc.DeckHeight+90)
            {Check(true,TEXT("actual_pawn_climbed_generated_ladder"));++LadderIndex;LadderClimbing=false;}
            else C->AddMovementInput(Direction);
            if(LadderClimbing && Elapsed-WalkStarted>Desc.Height/144.f+3.f)
            {
                UE_LOG(LogTemp,Display,TEXT("ER_INDUSTRIAL_LADDER_TIMEOUT index=%d local=%s mode=%d/%d"),LadderIndex,*Local.ToString(),M->MovementMode.GetValue(),M->CustomMovementMode);
                Check(false,TEXT("generated_ladder_timeout"));++LadderIndex;LadderClimbing=false;
            }
        }
    }
    if(Phase==1 && Elapsed>=DamageAt)
    {
        Check(Flight==Map->GetLayout().StairRoutes.Num(),TEXT("all_floors_both_stairs_walked"));
        Check(LadderIndex==Map->GetLayout().Ladders.Num(),TEXT("all_generated_ladders_climbed"));
        // An actual authorized rifle shot after reducing a local band to 1HP.
        float WallX=Map->GetLayout().CentralBuildingCenter.X-Map->GetLayout().CentralBuildingSize.X*.5;
        const float ShotY=Map->GetLayout().CircularArena ? 60.f:200.f;
        for(const auto& Footprint:Map->GetLayout().CentralFootprints)
            if(ShotY>Footprint.Min.Y && ShotY<Footprint.Max.Y){WallX=Footprint.Min.X;break;}
        C->TeleportTo({WallX+220,ShotY,Map->GetLayout().CentralBuildingCenter.Z+100},FRotator(0,180,0));C->GetCharacterMovement()->StopMovementImmediately();PC->SetControlRotation(FRotator(0,180,0));
        FHitResult Hit;FERCellId Id;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERIndustrialShot),false,C);
        const FVector Eye=C->GetPawnViewLocation();
        const bool Found=GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye-FVector(500,0,0),ECC_GameTraceChannel1,Q) && Map->ResolveCell(Hit,Id);
        Check(Found,TEXT("authoritative_cell_hit"));
        if(Found)
        {
            TMap<FERCellId,float> Band;const auto& Cells=Map->GetCellWorld();const auto& P=Cells.GetPanels()[Id.Panel];
            for(int32 Z=0;Z<P.Dims.Z;++Z)for(int32 Y=0;Y<P.Dims.Y;++Y)for(int32 X=0;X<P.Dims.X;++X)
            {const FERCellId Cell{Id.Panel,X,Y,Z};if(Cells.IsAlive(Cell) && FMath::Abs(Cells.GetCellCenter(Cell).Z-Hit.ImpactPoint.Z)<80)Band.Add(Cell,Cells.GetHealth(Cell)-1);}
            Map->ApplyCellBatch(Band);
        }
        const auto Before=C->Weapon->AuthorizedShotCount;C->Weapon->StartFire();C->Weapon->StopFire();
        Check(C->Weapon->AuthorizedShotCount==Before+1 && Map->GetCellWorld().GetRemovedCount()>0,TEXT("real_rifle_damages_building"));
        const auto Blast=AERBlockRoom::ApplyWorldExplosion(GetWorld(),Hit.ImpactPoint+Hit.ImpactNormal*30,
            FERBlockExplosionProfile::Linear(220,2400),C);
        Check(Blast.Damaged>0 && Blast.Destroyed>0 && Map->ValidateCells(),TEXT("irregular_blast_publishes_coherent_cell_destruction"));
        const auto& L=Map->GetLayout();
        if(!L.Containers.IsEmpty())
        {
            Check(ERIndustrialTest::ContainerBlocked(*GetWorld(),L.Containers[0],Q),TEXT("steel_container_initially_blocks_route"));
            ERIndustrialTest::BreachContainer(*Map,L.Containers[0]);
            Check(!ERIndustrialTest::ContainerBlocked(*GetWorld(),L.Containers[0],Q),TEXT("container_breach_opens_route"));
        }
        if(!L.WallBridges.IsEmpty())
        {
            Check(ERIndustrialTest::BridgeSupported(*GetWorld(),L.WallBridges[0],Q),TEXT("timber_bridge_initially_supported"));
            ERIndustrialTest::CutBridgeSupports(*Map,L.WallBridges[0]);ERIndustrialTest::BreachWall(*Map);
            Check(!ERIndustrialTest::WallBlocked(*GetWorld(),ERIndustrialTest::BreachPoint(L),Q),TEXT("brick_breach_creates_ground_crossing"));
        }
        if(!L.CoverSites.IsEmpty())
        {
            const auto& Site=L.CoverSites[0];
            ERIndustrialTest::Cut(*Map,Site.Frame,FBox(FVector(-80,-200,0),FVector(80,200,200)),Site.Material);
            Check(ERIndustrialTest::IntermediateCoverMatches(*Map,true),TEXT("destroying_intermediate_cover_exposes_hiding_place"));
        }
        Capture(Map,true);Publish(2,Map);
    }
    if(Phase==2 && Elapsed>=DamageAt+5 && !Map->IsSupportPending())
    {
        Check(Map->ValidateCells(),TEXT("destruction_and_collapse_settled"));Publish(3,Map);
        Capture(Map,1);
        if(FApp::CanEverRender())
        {
            const TWeakObjectPtr<AERProceduralMap> WeakMap=Map;FTimerHandle ContainerTimer;
            GetWorldTimerManager().SetTimer(ContainerTimer,FTimerDelegate::CreateWeakLambda(this,[this,WeakMap](){if(WeakMap.IsValid())Capture(WeakMap.Get(),2);}),1.f,false);
        }
        if(!Map->GetLayout().WallBridges.IsEmpty())
        {
            FCollisionQueryParams Q(SCENE_QUERY_STAT(ERIndustrialBridge),false);Q.AddIgnoredActor(C);
            Check(!ERIndustrialTest::BridgeSupported(*GetWorld(),Map->GetLayout().WallBridges[0],Q) && Map->GetCellWorld().GetDetachedCount()>0,TEXT("cut_supports_collapse_timber_crossing"));
            Check(ERIndustrialTest::BridgeSupported(*GetWorld(),Map->GetLayout().WallBridges[1],Q),TEXT("opposite_bridge_preserved"));
        }
    }
    if(Phase==3 && Elapsed>=RegenAt)
    {
        const int32 PreviousSeed=Map->LayoutConfig.Seed;
        auto* Mode=GetWorld()->GetAuthGameMode<AERGameMode>();
        if(Mode)Mode->ResetRound(false);
        Check(Mode && Map->IsMapReady() && Map->LayoutConfig.Seed!=PreviousSeed,TEXT("current_arena_round_changes_seed_and_epoch"));
        for(TActorIterator<AERBotController> Bot(GetWorld());Bot;++Bot)
        {
            const FVector P=Bot->GetPawn() ? Bot->GetPawn()->GetActorLocation():FVector(1.e6);
            const FVector Half=Map->GetLayout().ArenaSize*.5;
            Check(Bot->TrainingStart && FMath::Abs(P.X)<Half.X-40 && FMath::Abs(P.Y)<Half.Y-40 && P.Z>=90,TEXT("stationary_bot_gets_new_round_start"));
        }
        Check(Map->GetCellWorld().GetRemovedCount()==0,TEXT("new_epoch_restores_building"));Publish(4,Map);Capture(Map);
    }
    if(Phase==4 && Elapsed>=EndAt)
    {
        if(FApp::CanEverRender() && !Network && FParse::Param(FCommandLine::Get(),TEXT("ERIndustrialGallery")) && Gallery<6)
        {
            const int32 Next=FMath::Min(5,FMath::FloorToInt((Elapsed-EndAt)/2));
            if(Next>=Gallery)
            {
                Map->LayoutConfig.Seed=Gallery<3 ? 42:7919;Map->LayoutConfig.CrossingOverride=Gallery%3+1;
                Check(Map->Regenerate(),TEXT("gallery_variant"));Capture(Map,Gallery>=3);++Gallery;
            }
            return;
        }
        UE_LOG(LogTemp,Display,TEXT("ER_INDUSTRIAL_PROBE_COMPLETE result=%s"),Failed ? TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);
    }
}
