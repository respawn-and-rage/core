#include "ERDustTemplateProbe.h"
#include "ERAlleyDestruction.h"
#include "ERProceduralMap.h"
#include "ERCharacter.h"
#include "ERWeapon.h"
#include "ERMovement.h"
#include "ERPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/HUD.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UnrealClient.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Algo/Reverse.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

AERDustTemplateProbe::AERDustTemplateProbe()
{
    bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;
}
void AERDustTemplateProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AERDustTemplateProbe,SnapshotPhase);DOREPLIFETIME(AERDustTemplateProbe,ExpectedDigest);DOREPLIFETIME(AERDustTemplateProbe,DeckPlayer);DOREPLIFETIME(AERDustTemplateProbe,ArchitectureOutside);DOREPLIFETIME(AERDustTemplateProbe,ArchitectureInside);DOREPLIFETIME(AERDustTemplateProbe,ArchitectureRoof);
}
void AERDustTemplateProbe::Check(bool Good,const TCHAR* Label)
{
    Failed|=!Good;UE_LOG(LogTemp,Display,TEXT("ER_DUST_%s %s"),Good ? TEXT("PASS"):TEXT("FAIL"),Label);
}
void AERDustTemplateProbe::Publish(int32 Next,AERProceduralMap* Map)
{
    Phase=Next;SnapshotPhase=Next;ExpectedDigest=Map->GetStateDigest();ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("ER_DUST_STATE phase=%d digest=%u epoch=%u"),Phase,ExpectedDigest,Map->GetMapEpoch());
}
// Resolve the market floor or the authored Short A deck as a movement base.
static FVector DeckStand(const UWorld* World,const FVector& P)
{
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ERDustDeck),false);FHitResult Hit;
    for(TActorIterator<APawn> Pawn(World);Pawn;++Pawn)Q.AddIgnoredActor(*Pawn);
    return World->LineTraceSingleByChannel(Hit,P+FVector(0,0,500),P-FVector(0,0,50),ECC_WorldStatic,Q) ? Hit.ImpactPoint+FVector(0,0,94):P+FVector(0,0,414);
}
void AERDustTemplateProbe::PlaceRemote(bool Latest,const FVector& P)
{
    // Remote controllers in join order: the initial client first, the late one last.
    AERCharacter* Target=nullptr;
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {
        auto* C=It->Get() && !It->Get()->IsLocalController() ? Cast<AERCharacter>(It->Get()->GetPawn()):nullptr;
        if(C){Target=C;if(!Latest)break;}
    }
    if(!Target || !Target->GetPlayerState())return;
    auto* Move=Target->GetCharacterMovement();Target->TeleportTo(P,FRotator::ZeroRotator);Move->StopMovementImmediately();
    Move->SetMovementMode(MOVE_Walking);Move->ForceClientAdjustment();DeckPlayer=Target->GetPlayerState();ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("ER_DUST_REMOTE_PLACED name=%s pos=%s"),*DeckPlayer->GetPlayerName(),*P.ToString());
}
void AERDustTemplateProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    Elapsed+=Delta;TActorIterator<AERProceduralMap> It(GetWorld());if(!It)return;
    auto* Map=*It;if(!Map->IsMapReady())return;
    const auto& L=Map->GetLayout();
    auto RoadBlast=[](){const auto* HE=LoadObject<UERWeaponData>(nullptr,TEXT("/Game/EclipseRecoil/Weapons/LowPoly/DA_Grenade_RGD5"));return HE ? HE->BlockExplosion:FERBlockExplosionProfile();};
    if(!HasAuthority())
    {
        if((SnapshotPhase==1 || SnapshotPhase==6 || SnapshotPhase==7) && SeenPhase!=SnapshotPhase && Map->IsSnapshotCurrent() && Map->GetStateDigest()==ExpectedDigest)
        {
            FCollisionQueryParams Q(SCENE_QUERY_STAT(ERDustPeer),false);FHitResult Hit;const FVector P=L.TownCrossing;
            // Sample replicated environment collision, excluding the host or
            // another pawn currently walking through the tested underpass.
            for(TActorIterator<APawn> Pawn(GetWorld());Pawn;++Pawn)Q.AddIgnoredActor(*Pawn);
            const bool Cells=Map->ValidateCells();
            const bool Plan=L.TownBuildings.Num()>=12 && (L.SeededDistrict ? L.TownGates.Num()==L.DistrictAccesses*L.DistrictWallCount:L.TownGates.Num()==3);
            const bool Deck=GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(0,0,500),P-FVector(0,0,50),ECC_WorldStatic,Q) && FMath::IsNearlyEqual(Hit.ImpactPoint.Z,L.SeededDistrict ? 0.f:320.f,1.f);
            const bool Lower=!GetWorld()->SweepSingleByChannel(Hit,L.LowerRoute[0],L.LowerRoute.Last(),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,92.5),Q);
            bool Road=true,Facade=true,Roof=true;
            if(L.UnderpassAxis)
            {
                const auto Hole=L.UnderpassBreaches[0].GetCenter();
                Road=GetWorld()->LineTraceSingleByChannel(Hit,Hole+FVector(0,0,150),Hole-FVector(0,0,400),ECC_WorldStatic,Q) && FMath::IsNearlyEqual(Hit.ImpactPoint.Z,SnapshotPhase==6 ? -360.f:0.f,1.f);
            }
            if(FParse::Param(FCommandLine::Get(),TEXT("ERAlleyArchitecture")) && SnapshotPhase!=7)
            {
                Facade=GetWorld()->SweepTestByChannel(ArchitectureOutside+FVector(0,0,30),ArchitectureInside+FVector(0,0,30),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,92.5),Q)==(SnapshotPhase==1);
                const bool RoofHit=GetWorld()->LineTraceSingleByChannel(Hit,ArchitectureRoof+FVector(0,0,100),ArchitectureRoof-FVector(0,0,150),ECC_WorldStatic,Q);
                Roof=SnapshotPhase==1 ? RoofHit && FMath::Abs(Hit.ImpactPoint.Z-ArchitectureRoof.Z)<2:!RoofHit || Hit.ImpactPoint.Z<ArchitectureRoof.Z-100;
            }
            const bool Good=Cells && Plan && Deck && Lower && Road && Facade && Roof;
            UE_LOG(LogTemp,Display,TEXT("ER_DUST_CLIENT_CHECKS phase=%d cells=%d plan=%d deck=%d lower=%d road=%d facade=%d roof=%d"),SnapshotPhase,Cells,Plan,Deck,Lower,Road,Facade,Roof);
            UE_LOG(LogTemp,Display,TEXT("ER_DUST_CLIENT_%s phase=%d digest=%u epoch=%u"),Good ? TEXT("PASS"):TEXT("FAIL"),SnapshotPhase,ExpectedDigest,Map->GetMapEpoch());SeenPhase=SnapshotPhase;
        }
        // Phase 6 puts the initial client on the deck, phase 7 (after the seed rebuild) the late one.
        auto* PC=GetWorld()->GetFirstPlayerController();auto* C=PC ? Cast<AERCharacter>(PC->GetPawn()):nullptr;
        if((SnapshotPhase==6 || SnapshotPhase==7) && SeenPhase==SnapshotPhase && BaseChecked!=SnapshotPhase && C && PC->PlayerState && DeckPlayer==PC->PlayerState)
        {
            if(BaseClock<0)BaseClock=Elapsed;
            const auto* Base=C->GetCharacterMovement()->GetMovementBase();const FVector Target=DeckStand(GetWorld(),L.TownCrossing);
            const bool On=Base && Base->GetOwner()==Map && C->GetCharacterMovement()->IsMovingOnGround() && FVector::Dist2D(C->GetActorLocation(),Target)<40 && FMath::Abs(C->GetActorLocation().Z-Target.Z)<30;
            if(On || Elapsed-BaseClock>10)
            {
                UE_LOG(LogTemp,Display,TEXT("ER_DUST_CLIENT_BASE_%s phase=%d name=%s base=%s pos=%s target=%s"),On ? TEXT("PASS"):TEXT("FAIL"),SnapshotPhase,*PC->PlayerState->GetPlayerName(),Base ? *Base->GetName():TEXT("none"),*C->GetActorLocation().ToString(),*Target.ToString());
                BaseChecked=SnapshotPhase;BaseClock=-1;
            }
        }
        return;
    }
    auto* PC=Cast<AERPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(),0));
    auto* C=PC ? Cast<AERCharacter>(PC->GetPawn()):nullptr;
    if(!C || !C->Weapon || (!L.SeededDistrict && L.TownGates.Num()<2)){if(Elapsed>50)FPlatformMisc::RequestExitWithStatus(false,1);return;}
    auto* Move=C->GetCharacterMovement();
    if(FParse::Param(FCommandLine::Get(),TEXT("ERAlleyArchitecture"))){TickArchitecture(Map,C,PC);return;}
    // Opt-in art gallery: fixed cameras are held across many rendered frames
    // after compilation, so captures cannot inherit a just-teleported FPS view.
    if(FParse::Param(FCommandLine::Get(),TEXT("ERAlleyGallery")))
    {
        const bool Plans=FParse::Param(FCommandLine::Get(),TEXT("ERAlleyPlans"));
        const bool TowerOnly=!Plans && FParse::Param(FCommandLine::Get(),TEXT("ERAlleyTowerOnly"));
        const bool PassageOnly=!Plans && FParse::Param(FCommandLine::Get(),TEXT("ERAlleyPassageOnly"));
        const bool ModulesOnly=!Plans && FParse::Param(FCommandLine::Get(),TEXT("ERAlleyModulesOnly"));
        static constexpr int32 PlanSeeds[]={42,73,511,7919};
        if(Phase==0)
        {
            if(Elapsed<12)return;
#if WITH_EDITOR
            FAssetCompilingManager::Get().FinishAllCompilation();
#endif
            C->SetActorHiddenInGame(true);Move->StopMovementImmediately();PC->ClientSetHUD(AHUD::StaticClass());
            Phase=90;GateIndex=ModulesOnly ? 14:PassageOnly ? 10:TowerOnly ? 6:0;Waypoint=0;StageTime=Elapsed;return;
        }
        static const TCHAR* Names[]={TEXT("Plan-42"),TEXT("Courtyard-A"),TEXT("Long"),TEXT("Underpass"),TEXT("Short-A"),TEXT("Gate"),TEXT("Rooftop"),TEXT("Interior"),TEXT("Ramp"),TEXT("Plan-7919"),TEXT("Tunnel-Entry"),TEXT("Tunnel-Lower"),TEXT("Road-Intact"),TEXT("Road-Breached"),TEXT("Houses"),TEXT("Interior"),TEXT("Roof-Rims")};
        if(Phase==90 && Waypoint==0)
        {
            if(Plans)
            {
                Map->LayoutConfig.Seed=PlanSeeds[GateIndex];Check(Map->Regenerate(),TEXT("gallery_aerial_seed"));
                UE_LOG(LogTemp,Display,TEXT("ER_DUST_AERIAL seed=%d size=%s digest=%u"),PlanSeeds[GateIndex],*L.ArenaSize.ToString(),Map->GetStateDigest());
                if(L.SeededDistrict)UE_LOG(LogTemp,Display,TEXT("ER_DISTRICT_AERIAL seed=%d accesses=%d bends=%d plazas=%d towers=%d walls=%d open_depth=%.0f"),PlanSeeds[GateIndex],L.DistrictAccesses,L.DistrictBends,L.TownCourts.Num()-2,L.DistrictTowers.Num(),L.DistrictWallCount,L.DistrictOpenHalfDepth*2);
                if(L.UnderpassAxis)UE_LOG(LogTemp,Display,TEXT("ER_PASSAGE_AERIAL seed=%d axis=%d cover=%d center_cover=%d"),PlanSeeds[GateIndex],L.UnderpassAxis,L.LowCovers.Num(),L.CentralLowCovers);
            }
            else if(GateIndex==9){Map->LayoutConfig.Seed=7919;Check(Map->Regenerate(),TEXT("gallery_second_seed"));}
            FVector Eye,Target;bool Plan=Plans || GateIndex==0 || GateIndex==9;
            if(ModulesOnly)
            {
                const auto* H=L.Houses.FindByPredicate([](const auto& House){return House.RoofAccess && !House.Entrances.IsEmpty();});
                if(!H)H=&L.Houses[0];
                if(GateIndex==14)
                {
                    FCollisionQueryParams Q(SCENE_QUERY_STAT(ERModuleCamera),false);Q.AddIgnoredActor(C);bool Found=false;
                    for(const auto& House:L.Houses)
                    {
                        if(House.Entrances.IsEmpty())continue;
                        const auto& Entry=House.Entrances[0];const auto Out=(Entry[0]-Entry[1]).GetSafeNormal2D();const FVector Side(-Out.Y,Out.X,0);
                        const auto Size=House.Frame.GetScale3D();FVector Aim=Entry[1]+Out*145;Aim.Z=House.Frame.GetLocation().Z+Size.Z*.55;
                        for(float Lift:{350.f,600.f})for(float Distance:{1000.f,800.f,500.f,250.f})for(float Offset:{0.f,250.f,-250.f})
                        {
                            if(Found)continue;
                            const auto Candidate=Aim+Out*Distance+Side*Offset+FVector(0,0,Lift);
                            if(FMath::Abs(Candidate.X)>L.ArenaSize.X*.5-60 || FMath::Abs(Candidate.Y)>L.ArenaSize.Y*.5-60)continue;
                            if(!GetWorld()->LineTraceTestByChannel(Candidate,Aim,ECC_Visibility,Q))
                            {Eye=Candidate;Target=Aim;Found=true;}
                        }
                        if(Found)break;
                    }
                    Check(Found,TEXT("module_gallery_exterior_has_clear_camera"));
                }
                else if(GateIndex==15)
                {
                    TArray<FVector> Entry;Check(ERProcedural::FindAlleyInteriorRoute(GetWorld(),L,Entry),TEXT("module_gallery_interior"));
                    Eye=Entry[Entry.Num()/2]+FVector(0,0,65);Target=Entry[0]+FVector(0,0,65);
                }
                else
                {
                    const auto& R=L.RoofTransfers[0];Target=(R[1]+R[2])*.5-FVector(0,0,60);
                    const auto Along=(R[2]-R[1]).GetSafeNormal2D();const FVector Side(-Along.Y,Along.X,0);
                    Eye=Target-Along*280+Side*240+FVector(0,0,350);
                }
            }
            else if(GateIndex>=10 && !Plans && L.UnderpassAxis)
            {
                const auto& R=L.UnderpassRamps[0];const auto D=(R.Exit-R.Entry).GetSafeNormal2D();
                if(GateIndex==10){Eye=R.Entry+D*60+FVector(0,0,180);Target=R.Exit+FVector(0,0,140);}
                else if(GateIndex==11){const int32 Mid=L.UnderpassRoute.Num()/2;Eye=L.UnderpassRoute[Mid]+FVector(0,0,65);Target=L.UnderpassRoute[FMath::Min(Mid+6,L.UnderpassRoute.Num()-1)]+FVector(0,0,65);}
                else
                {
                    Target=L.UnderpassBreaches[0].GetCenter();Eye=Target+FVector(0,0,1000);
                    if(GateIndex==13)AERBlockRoom::ApplyWorldExplosion(GetWorld(),Target+FVector(0,0,10),RoadBlast(),nullptr);
                }
            }
            else if(Plan){Eye=FVector(0,0,11000);Target=FVector::ZeroVector;}
            else if(GateIndex==1){Target=L.TownCourts[3].GetCenter()+FVector(0,0,180);Eye=Target+FVector(0,700,420);}
            else if(GateIndex==2){Target=L.TownCourts[3].GetCenter()+FVector(0,0,150);Eye=L.TownCourts[4].GetCenter()+FVector(0,0,180);Eye.X=L.ArenaSize.X*.42;Target.X=Eye.X;}
            else if(GateIndex==3){Eye=L.LowerRoute[0]+FVector(0,0,65);Target=L.TownCrossing+FVector(0,0,160);}
            else if(GateIndex==4){Eye=L.TownCrossing+FVector(0,280,480);Target=L.TownCrossing+FVector(0,-700,370);}
            else if(GateIndex==5 && !L.TownGates.IsEmpty()){const auto& G=L.TownGates[0];Eye=G.Frame.TransformPosition(FVector(0,-550,170));Target=G.Frame.TransformPosition(FVector(0,0,190));}
            else if(GateIndex==5){Eye={0,-L.DistrictOpenHalfDepth,170};Target={0,0,190};}
            else if(GateIndex==6 && L.SeededDistrict)
            {
                const auto& F=L.DistrictTowers[0];const float H=L.DistrictTowerHeights[0];Target=F.TransformPosition({-220,0,H*.65f});
                FCollisionQueryParams Q(SCENE_QUERY_STAT(ERDistrictTowerCamera),false);Q.AddIgnoredActor(C);bool Clear=false;
                for(float Lift:{400.f,700.f,1000.f})
                {
                    Eye=F.TransformPosition({900,900,H+Lift});FHitResult Hit;
                    if(GetWorld()->LineTraceSingleByChannel(Hit,Eye,Target,ECC_Visibility,Q))
                    {const auto P=F.InverseTransformPosition(Hit.ImpactPoint);Clear=P.X>=-700 && P.X<=30 && FMath::Abs(P.Y)<350 && P.Z<=H+120;}
                    if(Clear)break;
                }
                Check(Clear,TEXT("tower_gallery_camera_has_clear_view"));
            }
            else if(GateIndex==6 && !L.RooftopRoutes.IsEmpty())
            {const auto& R=L.RooftopRoutes[0];Target=R.Last()+FVector(0,0,30);Eye=R[R.Num()/2]+FVector(0,0,240);}
            else if(GateIndex==7)
            {TArray<FVector> Entry;if(ERProcedural::FindAlleyInteriorRoute(GetWorld(),L,Entry)){Eye=Entry[1]+FVector(0,0,66);Target=Entry[0]+FVector(0,0,66);}else{Eye=FVector(0,0,800);Target=FVector::ZeroVector;}}
            else{Eye=L.GroundRoutes[0][1]+FVector(0,0,70);Target=L.GroundRoutes[0][2]+FVector(0,0,50);}
            auto* Camera=GetWorld()->SpawnActor<ACameraActor>(Eye,Plan ? FRotator(-90,-90,0):(Target-Eye).Rotation());
            Camera->GetCameraComponent()->SetFieldOfView(78);
            if(Plan)
            {
                Camera->GetCameraComponent()->SetProjectionMode(ECameraProjectionMode::Orthographic);
                Camera->GetCameraComponent()->SetAspectRatio(Plans ? 1.f:16.f/9);
                Camera->GetCameraComponent()->OrthoWidth=FMath::Max(L.ArenaSize.X,L.ArenaSize.Y*(Plans ? 1.f:16.f/9))*1.06;
            }
            PC->SetViewTarget(Camera);Waypoint=1;StageTime=Elapsed;
        }
        else if(Phase==90 && Waypoint==1 && Elapsed-StageTime>3)
        {
            const FString Name=Plans ? FString::Printf(TEXT("Aerial-%d"),PlanSeeds[GateIndex]):FString(Names[GateIndex]);
            const FString Prefix=ModulesOnly ? TEXT("Testing/MiddleEasternModules-"):L.UnderpassAxis ? TEXT("Testing/MiddleEasternPassages-"):L.SeededDistrict ? TEXT("Testing/MiddleEasternCore-"):TEXT("Testing/MiddleEasternAlleys-");
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/Prefix+Name+TEXT(".png"),false,false);Waypoint=2;StageTime=Elapsed;
        }
        else if(Phase==90 && Waypoint==2 && Elapsed-StageTime>1)
        {
            if(++GateIndex==(ModulesOnly ? 17:Plans ? 4:TowerOnly ? 7:L.UnderpassAxis ? 14:10)){UE_LOG(LogTemp,Display,TEXT("ER_DUST_COMPLETE result=%s gallery=1"),Failed ? TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);Phase=91;}
            else Waypoint=0;
        }
        return;
    }
    auto Place=[&](FVector P,float Yaw){Move->StopMovementImmediately();C->TeleportTo(P,FRotator(0,Yaw,0));PC->SetControlRotation(FRotator(0,Yaw,0));Move->SetMovementMode(MOVE_Walking);};
    auto Capture=[&](const FString& Name){if(FApp::CanEverRender())FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Testing")/Name,true,false);};
    auto Follow=[&](const TArray<FVector>& Path,float Reach=38.f)
    {
        if(Waypoint>=Path.Num()){Move->StopMovementImmediately();return true;}
        const FVector Target=Path[Waypoint],Position=C->GetActorLocation();
        if(FVector::Dist2D(Position,Target)<Reach && FMath::Abs(Position.Z-Target.Z)<45){++Waypoint;return Waypoint>=Path.Num();}
        FVector Direction=Target-Position;Direction.Z=0;Direction.Normalize();
        C->AddMovementInput(Direction);PC->SetControlRotation(Direction.Rotation());return false;
    };
    if(Phase==0 && Elapsed>4)
    {
        Check(Map->ValidateCells() && L.TownBuildings.Num()>=12 && (L.SeededDistrict ? L.TownCourts.Num()>=4:L.TownCourts.Num()==6),TEXT("plan_city_parcels_and_courts"));
        PC->SetIgnoreLookInput(true);
        Publish(1,Map);StageTime=Elapsed;
        if(L.TownGates.IsEmpty()){Place(L.LowerRoute[0],0);Phase=2;Check(L.SeededDistrict && L.DistrictWallCount==0,TEXT("open_center_has_no_city_wall_gates"));}
        else{const auto& G=L.TownGates[0];Place(G.Passage[0],G.Frame.Rotator().Yaw+90);}
    }
    else if(Phase==1)
    {
        if(Elapsed-StageTime<1.6f)PC->SetControlRotation(FRotator(0,L.TownGates[0].Frame.Rotator().Yaw+90,0));
        // Give the player camera a rendered frame after teleporting before
        // capturing the gate; movement must not carry it through the leaves.
        if(!CapturedGate)
        {
            if(Elapsed-StageTime<.6f)return;
            Capture(TEXT("DustTown-WoodenGates.png"));CapturedGate=true;return;
        }
        // A screenshot can be consumed by the render thread after this tick.
        // Hold the frontal camera before starting the sideways approach.
        if(Elapsed-StageTime<1.6f)return;
        const auto& G=L.TownGates[GateIndex];
        if(Follow(G.Passage,10.f))
        {
            Check(true,L.SeededDistrict ? TEXT("actual_pawn_passes_generated_district_arch"):TEXT("actual_pawn_passes_opposite_swing_arched_gate"));Waypoint=0;
            if(++GateIndex<L.TownGates.Num()){const auto& Next=L.TownGates[GateIndex];Place(Next.Passage[0],Next.Frame.Rotator().Yaw+90);StageTime=Elapsed;}
            else{Place(L.LowerRoute[0],0);Phase=2;StageTime=Elapsed;}
        }
        if(Elapsed-StageTime>18){Check(false,TEXT("arched_gate_timeout"));Phase=2;Waypoint=0;Place(L.LowerRoute[0],0);StageTime=Elapsed;}
    }
    else if(Phase==2)
    {
        if(Follow(L.LowerRoute) || Elapsed-StageTime>12)
        {
            Check(FVector::Dist2D(C->GetActorLocation(),L.LowerRoute.Last())<50 && C->GetActorLocation().Z<110,L.SeededDistrict ? TEXT("actual_pawn_crosses_market_lane"):TEXT("actual_pawn_walks_CT_under_Short_A"));
            Capture(TEXT("DustTown-Underpass.png"));Place(L.UpperRoute[0],0);Waypoint=0;Phase=3;StageTime=Elapsed;
        }
    }
    else if(Phase==3)
    {
        if(!ReachedUpper && (L.SeededDistrict || C->GetActorLocation().Z>408))
        {ReachedUpper=true;Capture(TEXT("DustTown-ShortA.png"));}
        if(Follow(L.UpperRoute) || Elapsed-StageTime>70)
        {
            Check(ReachedUpper && FVector::Dist2D(C->GetActorLocation(),L.UpperRoute.Last())<60 && FMath::Abs(C->GetActorLocation().Z-L.UpperRoute.Last().Z)<45,L.SeededDistrict ? TEXT("actual_pawn_crosses_open_market_court"):TEXT("actual_pawn_climbs_and_follows_bent_Short_A_to_A_court"));
            Move->StopMovementImmediately();Phase=L.GradedTerrain ? 30:4;StageTime=Elapsed;Waypoint=0;
        }
    }
    else if(Phase==30)
    {
        if(TraversalIndex>=FMath::Min(2,L.RooftopRoutes.Num()))
        {Check(L.RooftopRoutes.Num()>=2,TEXT("multiple_external_roof_staircases"));Phase=31;TraversalStarted=false;return;}
        const auto& Route=L.RooftopRoutes[TraversalIndex];
        if(!TraversalStarted){Place(Route[0],0);Waypoint=0;StageTime=Elapsed;TraversalStarted=true;}
        const bool Reached=Follow(Route,12.f);
        const auto* Roof=Cast<UInstancedStaticMeshComponent>(Move->GetMovementBase());
        if(Reached && Move->IsMovingOnGround() && ((Roof && Roof->GetStaticMesh() && Roof->GetStaticMesh()->GetName().Contains(TEXT("House_"))) || (Move->GetMovementBase() && Move->GetMovementBase()->ComponentHasTag(TEXT("ERDestructibleShell")))))
        {Check(true,TEXT("actual_pawn_climbs_external_switchback_to_roof"));++TraversalIndex;TraversalStarted=false;}
        else if(Elapsed-StageTime>25)
        {Check(false,TEXT("roof_stair_timeout"));UE_LOG(LogTemp,Display,TEXT("ER_DUST_ROOF index=%d waypoint=%d pos=%s target=%s"),TraversalIndex,Waypoint,*C->GetActorLocation().ToString(),*Route[FMath::Min(Waypoint,Route.Num()-1)].ToString());++TraversalIndex;TraversalStarted=false;}
    }
    else if(Phase==31)
    {
        if(!TraversalStarted)
        {
            const bool Found=ERProcedural::FindAlleyInteriorRoute(GetWorld(),L,TraversalPath);Check(Found,TEXT("visible_house_opening_has_standing_capsule_passage"));
            if(!Found){Phase=32;TraversalIndex=0;return;}
            Place(TraversalPath[0],0);Waypoint=0;StageTime=Elapsed;TraversalStarted=true;
        }
        if(Follow(TraversalPath,15.f) || Elapsed-StageTime>15)
        {Check(Waypoint>=TraversalPath.Num(),TEXT("actual_pawn_enters_house_and_returns_through_opening"));Phase=L.RoofTransfers.IsEmpty() ? 32:38;TraversalIndex=0;TraversalStarted=false;}
    }
    else if(Phase==38)
    {
        if(!TraversalStarted)
        {
            const bool Found=ERProcedural::FindAlleyRoofRimRoute(GetWorld(),L,TraversalPath);
            Check(Found,TEXT("low_roof_rim_has_a_standing_landing"));
            if(!Found){Phase=37;return;}
            Place(TraversalPath[0],(TraversalPath[1]-TraversalPath[0]).Rotation().Yaw);Waypoint=0;StageTime=Elapsed;TraversalStarted=true;
        }
        if(Follow(TraversalPath,8) && Move->IsMovingOnGround() && FMath::Abs(C->GetActorLocation().Z-(TraversalPath.Last().Z-6))<12)
        {Check(true,TEXT("actual_pawn_steps_onto_25cm_roof_rim"));Phase=37;TraversalStarted=false;}
        else if(Elapsed-StageTime>10){Check(false,TEXT("low_roof_rim_step_timeout"));Phase=37;TraversalStarted=false;}
    }
    else if(Phase==37)
    {
        if(TraversalIndex>=2){Phase=32;TraversalIndex=0;TraversalStarted=false;return;}
        if(!TraversalStarted)
        {
            const auto* Transfer=L.RoofTransfers.FindByPredicate([](const auto& R)
            {return FMath::Abs(R[1].Z-R[2].Z)>15 && FVector::Dist2D(R[1],R[2])<110;});
            TraversalPath=Transfer ? *Transfer:L.RoofTransfers[0];
            if(TraversalIndex)Algo::Reverse(TraversalPath);
            Place(TraversalPath[0],(TraversalPath.Last()-TraversalPath[0]).Rotation().Yaw);
            C->StopJumping();Waypoint=0;GateIndex=0;StageTime=Elapsed;TraversalStarted=true;
        }
        if(GateIndex==0)
        {
            const TArray<FVector> Approach{TraversalPath[0],TraversalPath[1]};
            if(Follow(Approach,8) && Move->IsMovingOnGround() && FMath::Abs(C->GetActorLocation().Z-(TraversalPath[1].Z-6))<12)
            {
                Check(true,TEXT("actual_pawn_reaches_open_roof_crossing"));
                C->Jump();GateIndex=1;Waypoint=2;
            }
        }
        else
        {
            const bool Reached=Follow(TraversalPath,12);
            if(Reached && Move->IsMovingOnGround() && FMath::Abs(C->GetActorLocation().Z-TraversalPath.Last().Z)<15)
            {Check(true,TEXT("actual_pawn_jumps_between_adjacent_roofs_over_low_rims"));C->StopJumping();++TraversalIndex;TraversalStarted=false;}
        }
        if(TraversalStarted && Elapsed-StageTime>12)
        {
            Check(false,TEXT("roof_rim_or_adjacent_jump_timeout"));
            UE_LOG(LogTemp,Display,TEXT("ER_DUST_ROOF_TRANSFER index=%d stage=%d waypoint=%d pos=%s from=%s to=%s"),TraversalIndex,GateIndex,Waypoint,*C->GetActorLocation().ToString(),*TraversalPath[1].ToString(),*TraversalPath[2].ToString());
            C->StopJumping();++TraversalIndex;TraversalStarted=false;
        }
    }
    else if(Phase==32)
    {
        if(TraversalIndex>=L.GroundRoutes.Num()*2){Phase=L.SeededDistrict ? 33:4;TraversalIndex=0;StageTime=Elapsed;return;}
        if(!TraversalStarted)
        {
            TraversalPath=L.GroundRoutes[TraversalIndex/2];if(TraversalIndex%2)Algo::Reverse(TraversalPath);
            Place(TraversalPath[0],0);Waypoint=0;StageTime=Elapsed;TraversalStarted=true;
        }
        if(Follow(TraversalPath) || Elapsed-StageTime>(L.SeededDistrict ? 45:20))
        {Check(Waypoint>=TraversalPath.Num(),TEXT("actual_pawn_walks_grade_up_and_down"));++TraversalIndex;TraversalStarted=false;}
    }
    else if(Phase==33)
    {
        if(TraversalIndex>=L.Ladders.Num()){Phase=34;TraversalStarted=false;return;}
        const auto& Desc=L.Ladders[TraversalIndex];const auto Direction=-Desc.Frame.GetUnitAxis(EAxis::X);auto* M=Cast<UERMovement>(Move);
        if(!TraversalStarted)
        {
            Place(Desc.Frame.TransformPosition({55,0,95}),Direction.Rotation().Yaw);M->RetryAfter=0;M->bLadderNeedsRelease=false;
            StageTime=Elapsed;TraversalStarted=true;SawTowerLadder=false;
        }
        SawTowerLadder|=M->IsOnLadder();const FVector Local=Desc.Frame.InverseTransformPosition(C->GetActorLocation());
        if(SawTowerLadder && !M->IsTraversing() && M->IsMovingOnGround() && Local.X< -60 && FMath::Abs(Local.Z-Desc.DeckHeight-94)<20)
        {Check(true,TEXT("actual_pawn_climbs_tower_ladder_and_stands_on_deck"));++TraversalIndex;TraversalStarted=false;}
        else C->AddMovementInput(Direction);
        if(TraversalStarted && Elapsed-StageTime>Desc.Height/144.f+5)
        {Check(false,TEXT("district_tower_ladder_timeout"));UE_LOG(LogTemp,Display,TEXT("ER_DISTRICT_LADDER index=%d local=%s mode=%d/%d"),TraversalIndex,*Local.ToString(),M->MovementMode.GetValue(),M->CustomMovementMode);++TraversalIndex;TraversalStarted=false;}
    }
    else if(Phase==34)
    {
        if(!TraversalStarted){TraversalPath=L.DistrictRoutes[0];Place(TraversalPath[0],90);Waypoint=0;StageTime=Elapsed;TraversalStarted=true;}
        if(Follow(TraversalPath,20) || Elapsed-StageTime>150)
        {Check(Waypoint>=TraversalPath.Num(),TEXT("actual_pawn_follows_generated_route_from_Alpha_to_Omega"));Phase=L.UnderpassAxis ? 35:4;TraversalIndex=0;StageTime=Elapsed;TraversalStarted=false;}
    }
    else if(Phase==35)
    {
        if(!TraversalStarted)
        {TraversalPath=L.UnderpassRoute;if(TraversalIndex)Algo::Reverse(TraversalPath);Place(TraversalPath[0],0);Waypoint=0;StageTime=Elapsed;TraversalStarted=true;}
        if(Follow(TraversalPath,20) || Elapsed-StageTime>180)
        {
            Check(Waypoint>=TraversalPath.Num(),TEXT("actual_pawn_walks_bent_lower_passage_and_both_ramps"));
            if(++TraversalIndex<2){TraversalStarted=false;return;}
            const FVector Hole=L.UnderpassBreaches[0].GetCenter();FHitResult Hit;FERCellId Id;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERAlleyRoad),false,C);
            Check(GetWorld()->LineTraceSingleByChannel(Hit,Hole+FVector(0,0,150),Hole-FVector(0,0,50),ECC_GameTraceChannel1,Q) && Map->ResolveCell(Hit,Id),TEXT("upper_road_resolves_damageable_cells"));
            AERBlockRoom::ApplyWorldExplosion(GetWorld(),Hole+FVector(0,0,10),RoadBlast(),nullptr);
            Check(!GetWorld()->SweepSingleByChannel(Hit,Hole+FVector(0,0,100),Hole-FVector(0,0,260),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,92.5),Q),TEXT("road_destruction_opens_real_capsule_hole"));
            Place(Hole+FVector(0,0,100),0);Move->SetMovementMode(MOVE_Falling);Phase=36;StageTime=Elapsed;
        }
    }
    else if(Phase==36 && Elapsed-StageTime>2)
    {
        Check(Move->IsMovingOnGround() && FMath::Abs(C->GetActorLocation().Z+266)<10,TEXT("actual_pawn_falls_through_breached_road_to_lower_floor"));
        Capture(TEXT("Alley-Road-Breached.png"));Phase=4;StageTime=Elapsed;
    }
    else if(Phase==4)
    {
        FCollisionQueryParams Q(SCENE_QUERY_STAT(ERDustShot),false,C);FHitResult Hit;FERCellId Id;bool Found=false;
        const auto& Cells=Map->GetCellWorld();
        for(int32 Index=0;Index<Cells.GetPanels().Num();++Index)
        {
            // Lower part of a side board of the project cover.
            const auto& Panel=Cells.GetPanels()[Index];const FERCellId Low{Index,Panel.Dims.X/2,0,FMath::Min(Panel.Dims.Z/2,10)};
            if(Panel.Material!=EERBlockMaterial::Wood)continue;
            if(Panel.Transform.GetRotation().GetAxisZ().Z<.99 || !Cells.IsAlive(Low))continue;
            const FVector Target=Map->GetActorTransform().TransformPosition(Cells.GetCellCenter(Low));
            if(Target.Z>ERProcedural::AlleyGroundHeight(L,FVector2D(Target))+100)continue;
            for(const FVector D:{FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0)})
            {
                FVector P(Target.X+D.X*300,Target.Y+D.Y*300,100);
                P.Z+=ERProcedural::AlleyGroundHeight(L,FVector2D(P));
                if(GetWorld()->OverlapBlockingTestByChannel(P,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,92.5),Q))continue;
                Place(P,0);const FVector Eye=C->Camera->GetComponentLocation();PC->SetControlRotation((Target-Eye).Rotation());
                if(GetWorld()->LineTraceSingleByChannel(Hit,Eye,Target,ECC_GameTraceChannel1,Q) && Map->ResolveCell(Hit,Id)){Found=true;break;}
            }
            if(Found)break;
        }
        Check(Found,TEXT("project_court_cover_resolves_authoritative_block"));
        if(Found)
        {
            TMap<FERCellId,float> Prep;const auto& Panel=Cells.GetPanels()[Id.Panel];
            for(int32 Z=0;Z<Panel.Dims.Z;++Z)for(int32 Y=0;Y<Panel.Dims.Y;++Y)for(int32 X=0;X<Panel.Dims.X;++X)if(Cells.IsAlive({Id.Panel,X,Y,Z}))Prep.Add({Id.Panel,X,Y,Z},Cells.GetHealth({Id.Panel,X,Y,Z})-1);
            Map->ApplyCellBatch(Prep);
            const uint32 Before=C->Weapon->AuthorizedShotCount;C->Weapon->StartFire();C->Weapon->StopFire();
            Check(C->Weapon->AuthorizedShotCount==Before+1 && Map->GetCellWorld().GetRemovedCount()>0,TEXT("real_weapon_damages_project_wood"));
            AERBlockRoom::ApplyWorldExplosion(GetWorld(),Hit.ImpactPoint+Hit.ImpactNormal*30,220,2400,C);
        }
        Phase=5;StageTime=Elapsed;
    }
    else if(Phase==5 && Elapsed-StageTime>6)
    {
        Check(Map->ValidateCells() && Map->GetCellWorld().GetRemovedCount()>0,TEXT("court_damage_and_collapse_mappings"));Publish(6,Map);StageTime=Elapsed;
        if(FApp::CanEverRender())
        {
            auto* Camera=GetWorld()->SpawnActor<ACameraActor>(FVector(0,0,11000),FRotator(-90,-90,0));
            Camera->GetCameraComponent()->SetProjectionMode(ECameraProjectionMode::Orthographic);
            Camera->GetCameraComponent()->OrthoWidth=FMath::Max(L.ArenaSize.X,L.ArenaSize.Y*16/9)*1.08;
            PC->SetViewTarget(Camera);Capture(TEXT("DustTown-Plan-42.png"));
        }
    }
    if((Phase==6 || Phase==7) && PlacedPhase!=Phase && Elapsed-StageTime>3)
    {PlaceRemote(Phase==7,DeckStand(GetWorld(),L.TownCrossing));PlacedPhase=Phase;}
    FString LateReceipt;const bool PeerPending=Phase==6 && FParse::Value(FCommandLine::Get(),TEXT("ERDustLateReceipt="),LateReceipt) && !FPaths::FileExists(LateReceipt);
    if(Phase==6 && Elapsed-StageTime>30 && (!PeerPending || Elapsed-StageTime>120))
    {
        if(!LateReceipt.IsEmpty())Check(!PeerPending,TEXT("late_client_confirmed_damaged_snapshot_before_seed_rebuild"));
        Map->LayoutConfig.Seed=7919;Check(Map->Regenerate() && Map->GetCellWorld().GetRemovedCount()==0,TEXT("seed_rebuilds_city_and_restores_cover"));Publish(7,Map);StageTime=Elapsed;
        Capture(TEXT("DustTown-Plan-7919.png"));
    }
    else if(Phase==7 && Elapsed-StageTime>16)
    {
        UE_LOG(LogTemp,Display,TEXT("ER_DUST_COMPLETE result=%s"),Failed ? TEXT("FAIL"):TEXT("PASS"));
        FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);Phase=8;
    }
#endif
}
