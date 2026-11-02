#include "ERProceduralMap.h"
#include "ERMapCatalog.h"
#include "ERMapTheme.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "ERLadder.h"
#include "ERWeapon.h"
#include "EngineUtils.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERAlleysLayoutTest,"EclipseRecoil.Destruction.MiddleEasternAlleysLayout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERAlleysLayoutTest::RunTest(const FString& Parameters)
{
    auto* T=LoadObject<UERMapTemplate>(nullptr,TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_MiddleEasternAlleys"));
    if(!TestNotNull(TEXT("saved alley recipe"),T))return false;
    FERProceduralConfig C;C.TemplateAsset=T;FERBlockRoomConfig B;FERProceduralLayout L,Again;
    int32 RoofTransferSeeds=0;
    TSet<FString> Shapes;TSet<int32> AccessCounts,BendCounts,PlazaCounts,TowerCounts,WallCounts,WallAccessPairs,UnderpassAxes;
    for(int32 Seed=0;Seed<512;++Seed)
    {
        C.Seed=Seed;
        if(!TestTrue(FString::Printf(TEXT("seed %d has nonoverlapping solids and clear complete routes"),Seed),ERProcedural::Build(C,B,L)))return false;
        Shapes.Add(L.ArenaSize.ToString()+L.TownBuildings[0].ToString());
        TestEqual(TEXT("ten Alpha starts"),L.AlphaSpawns.Num(),10);TestEqual(TEXT("ten Omega starts"),L.OmegaSpawns.Num(),10);
        if(L.SeededDistrict)
        {
            AccessCounts.Add(L.DistrictAccesses);BendCounts.Add(L.DistrictBends);PlazaCounts.Add(L.TownCourts.Num());TowerCounts.Add(L.DistrictTowers.Num());
            WallCounts.Add(L.DistrictWallCount);WallAccessPairs.Add(L.DistrictWallCount*10+L.DistrictAccesses);
            TestTrue(TEXT("two to four approaches, with that many arches per existing wall"),L.DistrictAccesses>=2 && L.DistrictAccesses<=4 && L.TownGates.Num()==L.DistrictAccesses*L.DistrictWallCount && L.DistrictRoutes.Num()==L.DistrictAccesses);
            TestTrue(TEXT("zero, one or two central walls"),L.DistrictWallCount>=0 && L.DistrictWallCount<=2);
            if(L.DistrictWallCount==2)TestTrue(TEXT("twin walls surround at least 23m of open core"),L.DistrictOpenHalfDepth>=1180 && FMath::IsNearlyEqual(L.DistrictWallOffset-L.DistrictOpenHalfDepth,120));
            TestTrue(TEXT("paired climbable towers and variable squares"),L.DistrictTowers.Num()>=2 && L.DistrictTowers.Num()%2==0 && L.TownCourts.Num()>=4);
            for(int32 I=0;I<10;++I)
            {auto P=L.AlphaSpawns[I].GetLocation();P.X=-P.X;P.Y=-P.Y;TestTrue(TEXT("equal opposite team start and elevation"),P.Equals(L.OmegaSpawns[I].GetLocation(),.01));}
            for(int32 I=0;I<L.RooftopRoutes.Num();I+=2)for(int32 J=0;J<L.RooftopRoutes[I].Num();++J)
            {auto P=L.RooftopRoutes[I][J];P.X=-P.X;P.Y=-P.Y;TestTrue(TEXT("paired roof access and rise"),P.Equals(L.RooftopRoutes[I+1][J],.01));}
            for(const auto& Plot:L.TownBuildings)
            {
                if(L.DistrictWallCount!=1)TestTrue(TEXT("houses and towers leave the reserved center open"),Plot.Max.Y<=-L.DistrictOpenHalfDepth+.1 || Plot.Min.Y>=L.DistrictOpenHalfDepth-.1);
                const FBox Mate(FVector(-Plot.Max.X,-Plot.Max.Y,Plot.Min.Z),FVector(-Plot.Min.X,-Plot.Min.Y,Plot.Max.Z));
                TestTrue(TEXT("every occupied parcel has an opposite equal parcel"),L.TownBuildings.ContainsByPredicate([&](const FBox& Other){return Other.Min.Equals(Mate.Min,.1) && Other.Max.Equals(Mate.Max,.1);}));
            }
        }
        else
        {
            TestEqual(TEXT("six named courts including A/B"),L.TownCourts.Num(),6);TestEqual(TEXT("three narrow arched gateways"),L.TownGates.Num(),3);
            TestTrue(TEXT("B connects back to Mid"),L.TownStreetNames.Contains(TEXT("Lower_Mid")));
        }
        TestTrue(TEXT("full upper and lower routes"),L.UpperRoute.Num()>=(L.SeededDistrict ? 2:31) && L.LowerRoute.Num()==2);
        TestTrue(TEXT("graded terrain and multiple roof accesses"),L.GradedTerrain && L.RooftopRoutes.Num()>=2 && L.GroundRoutes.Num()==2);
        int32 Houses=L.Houses.Num();
        TestTrue(TEXT("requested house variants form actual parcels"),Houses>12);
        if(T->Town.ProceduralHouses)
        {
            if(!L.RoofTransfers.IsEmpty())++RoofTransferSeeds;
            int32 Entrances=0;
            for(const auto& H:L.Houses)
            {
                TestTrue(TEXT("every house is generated without a fitted imported mesh"),!H.Mesh && !H.StructureName.IsNone());
                Entrances+=H.Entrances.Num();
            }
            TestTrue(TEXT("street-facing module entrances are recorded"),Entrances>=4);
        }
        TestTrue(TEXT("three-sand ground material bound"),L.GroundSurface && (T->ThemeLibrary ? T->ThemeLibrary->Themes[L.ThemeIndex].Grounds.Contains(L.GroundSurface):L.GroundSurface==T->Town.GroundSurface));
        if(T->Town.LowCoverAndUnderpass)
        {
            UnderpassAxes.Add(L.UnderpassAxis);
            TestTrue(TEXT("paired ramps and breachable road above a real lower route"),L.UnderpassRamps.Num()==2 && L.UnderpassBreaches.Num()==2 && L.UnderpassRoute.Num()>80);
            TestTrue(TEXT("low cover in the plaza and both districts"),L.CentralLowCovers>=8 && L.LowCovers.Num()-L.CentralLowCovers>=6);
            for(int32 I=0;I<L.LowCovers.Num();I+=2)
            {
                const auto& A=L.LowCovers[I];const auto& M=L.LowCovers[I+1];
                TestTrue(TEXT("low obstacles have equal opposite size and elevation"),FVector(-A.Max.X,-A.Max.Y,A.Min.Z).Equals(M.Min,.01) && FVector(-A.Min.X,-A.Min.Y,A.Max.Z).Equals(M.Max,.01));
                TestTrue(TEXT("cover stays low"),A.GetSize().Z<=120);
            }
        }
    }
    if(T->Town.ProceduralHouses)TestTrue(TEXT("district variants include clear roof crossings"),RoofTransferSeeds>0);
    TestTrue(TEXT("seed changes whole parcels/arena"),Shapes.Num()>450);
    if(T->Town.SeededDistrict)
    {
        TestEqual(TEXT("all three access counts sampled"),AccessCounts.Num(),3);TestEqual(TEXT("all four winding settings sampled"),BendCounts.Num(),4);
        TestTrue(TEXT("square and tower counts actually change"),PlazaCounts.Num()>1 && TowerCounts.Num()>1);
        TestEqual(TEXT("all central wall variants sampled"),WallCounts.Num(),3);TestEqual(TEXT("wall count varies independently of approach count"),WallAccessPairs.Num(),9);
        if(T->Town.LowCoverAndUnderpass)TestEqual(TEXT("both winding underpass orientations sampled"),UnderpassAxes.Num(),2);
    }
    C.Seed=42;ERProcedural::Build(C,B,L);ERProcedural::Build(C,B,Again);
    TestEqual(TEXT("repeat fixed geometry"),L.FixedPieces.Num(),Again.FixedPieces.Num());
    for(int32 I=0;I<L.FixedPieces.Num();++I)
        TestTrue(TEXT("repeat mesh, surface and transform"),L.FixedPieces[I].Transform.Equals(Again.FixedPieces[I].Transform) && L.FixedPieces[I].Mesh==Again.FixedPieces[I].Mesh && L.FixedPieces[I].Surface==Again.FixedPieces[I].Surface && L.FixedPieces[I].MeshRotation==Again.FixedPieces[I].MeshRotation);
    auto* Invalid=DuplicateObject<UERMapTemplate>(T,GetTransientPackage());C.TemplateAsset=Invalid;Invalid->Town.AxisWarp=4;
    const int32 Before=L.FixedPieces.Num();TestFalse(TEXT("invalid recipe rejected"),ERProcedural::Build(C,B,L));
    TestEqual(TEXT("rejection leaves previous layout intact"),L.FixedPieces.Num(),Before);
    TestTrue(TEXT("new map belongs to match rotation"),ERMaps::Rotation().Contains(FName(TEXT("/Game/EclipseRecoil/Maps/L_MiddleEasternAlleys"))));
    AddInfo(TEXT("ER_ALLEYS_LAYOUT seeds=512"));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERAlleysCollisionTest,"EclipseRecoil.Destruction.MiddleEasternAlleysCollision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERAlleysCollisionTest::RunTest(const FString& Parameters)
{
    auto* T=LoadObject<UERMapTemplate>(nullptr,TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_MiddleEasternAlleys"));
    if(!TestNotNull(TEXT("saved alley recipe"),T))return false;
    UWorld::InitializationValues V;V.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);if(!World)return false;
    auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;Map->bCeilings=false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ERAlleys),false);FHitResult Hit;const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);
    for(int32 Seed:{0,42,73,511,7919,INT32_MAX})
    {
        Map->LayoutConfig.Seed=Seed;if(!TestTrue(TEXT("physical seeded map"),Map->Regenerate()))break;
        if(T->Town.DestructibleArchitecture)
        {
            Map->RequestSupportCheck();for(int32 I=0;I<500 && Map->IsSupportPending();++I)Map->StepSupport();
            TestEqual(TEXT("all intact town structures start supported"),Map->GetCellWorld().GetRemovedCount(),int64(0));
        }
        const auto& L=Map->GetLayout();const auto P=L.TownCrossing;
        TestTrue(TEXT("market surface matches layout"),World->LineTraceSingleByChannel(Hit,P+FVector(0,0,500),P-FVector(0,0,50),ECC_WorldStatic,Q) && FMath::IsNearlyEqual(Hit.ImpactPoint.Z,L.SeededDistrict ? 0.f:320.f,1.f));
        TestFalse(L.SeededDistrict ? TEXT("standing capsule crosses open market"):TEXT("standing capsule crosses under bridge"),World->SweepSingleByChannel(Hit,L.LowerRoute[0],L.LowerRoute.Last(),FQuat::Identity,ECC_Pawn,Capsule,Q));
        if(L.SeededDistrict)
        {
            for(int32 Wall=0;Wall<L.DistrictWallCount;++Wall)
            {
                const float Y=L.DistrictWallCount==1 ? 0:(Wall ? 1.f:-1.f)*L.DistrictWallOffset;
                int32 Climbs=0;
                for(const auto& Ladder:L.Ladders)if(FMath::IsNearlyEqual(Ladder.DeckHeight+Ladder.Frame.GetLocation().Z,1300.f) && FMath::Abs(Ladder.Frame.GetLocation().Y-Y)<200)
                {
                    ++Climbs;
                    const FVector Exit=Ladder.Frame.TransformPosition({-100,0,Ladder.DeckHeight+100});
                    TestFalse(TEXT("wall roof ladder exit admits standing pawn"),World->OverlapBlockingTestByChannel(Exit,FQuat::Identity,ECC_Pawn,Capsule,Q));
                    TestTrue(TEXT("wall roof ladder lands on a physical deck"),World->LineTraceSingleByChannel(Hit,Exit,Exit-FVector(0,0,150),ECC_WorldStatic,Q) && FMath::IsNearlyEqual(Hit.ImpactPoint.Z,1300.f,1.f));
                    const FVector Across(Exit.X,Y*2-Exit.Y,Exit.Z);
                    TestFalse(TEXT("pawn crosses wall over the roof"),World->SweepTestByChannel(Exit,Across,FQuat::Identity,ECC_Pawn,Capsule,Q));
                }
                TestEqual(TEXT("each wall has paired climbs on both sides"),Climbs,4);
                int32 Passages=0;bool WasOpen=false;
                for(float X=-L.ArenaSize.X*.5f+60;X<L.ArenaSize.X*.5f-60;X+=25)
                {
                    // Sample inside the wall thickness; cover beyond its
                    // faces must not split one arch into several fake arches.
                    const bool Open=!World->SweepSingleByChannel(Hit,{X,Y-80,100},{X,Y+80,100},FQuat::Identity,ECC_Pawn,Capsule,Q);
                    if(Open && !WasOpen)++Passages;WasOpen=Open;
                }
                TestEqual(TEXT("each real city wall has exactly the requested capsule passages"),Passages,L.DistrictAccesses);
            }
            if(L.DistrictWallCount!=1)
            {
                int32 Open=0,Total=0;
                for(float X=-L.ArenaSize.X*.5f+100;X<L.ArenaSize.X*.5f-100;X+=50)
                {++Total;Open+=!World->SweepSingleByChannel(Hit,{X,-160,100},{X,160,100},FQuat::Identity,ECC_Pawn,Capsule,Q);}
                TestTrue(TEXT("actual middle is broadly open without a hidden third wall"),Open>Total*.75f);
            }
            for(const auto& Route:L.DistrictRoutes)for(int32 I=1;I<Route.Num();++I)
                if(World->SweepSingleByChannel(Hit,Route[I-1],Route[I],FQuat::Identity,ECC_Pawn,Capsule,Q))
                {
                    const bool Floor=Hit.GetComponent() && Hit.GetComponent()->GetName()==TEXT("AlleyTerrain") && Hit.ImpactNormal.Z>=.72;
                    if(!Floor){AddError(FString::Printf(TEXT("district complete route blocked seed=%d at=%s component=%s normal=%s"),Seed,*Route[I].ToString(),Hit.GetComponent() ? *Hit.GetComponent()->GetName():TEXT("none"),*Hit.ImpactNormal.ToString()));break;}
                }
            for(TActorIterator<AERLadder> It(World);It;++It)
            {
                for(float Z=0;Z<It->Height;Z+=50)TestTrue(TEXT("visible tower ladder rungs/handles have real collision"),It->HasRung(Z));
                TestFalse(TEXT("tower climb line has standing body clearance"),World->SweepSingleByChannel(Hit,It->ClimbPosition(0,94),It->ClimbPosition(It->Height,94),FQuat::Identity,ECC_Pawn,Capsule,Q));
                const auto Landing=It->Landing(94);TestFalse(TEXT("tower deck landing has headroom"),World->OverlapBlockingTestByChannel(Landing,FQuat::Identity,ECC_Pawn,Capsule,Q));
                TestTrue(TEXT("tower landing rests on a visible deck"),World->LineTraceSingleByChannel(Hit,Landing,Landing-FVector(0,0,150),ECC_WorldStatic,Q) && FMath::IsNearlyEqual(Hit.ImpactPoint.Z,It->GetActorLocation().Z+It->DeckHeight,1));
            }
        }
        for(const auto& Gate:L.TownGates)for(int32 I=1;I<Gate.Passage.Num();++I)
            TestFalse(TEXT("angled gate passage has actual capsule clearance"),World->SweepSingleByChannel(Hit,Gate.Passage[I-1],Gate.Passage[I],FQuat::Identity,ECC_Pawn,Capsule,Q));
        for(const auto* Starts:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& S:*Starts)
            TestFalse(TEXT("actual team spawn capsule is clear"),World->OverlapBlockingTestByChannel(S.GetLocation(),FQuat::Identity,ECC_Pawn,Capsule,Q));
        for(const auto& Route:L.StairRoutes)for(const auto& S:Route)
            TestFalse(TEXT("standing stair headroom"),World->OverlapBlockingTestByChannel(S+FVector(0,0,20),FQuat::Identity,ECC_Pawn,Capsule,Q));
        for(const auto& Route:L.RooftopRoutes)for(const auto& S:Route)
            if(World->OverlapBlockingTestByChannel(S+FVector(0,0,20),FQuat::Identity,ECC_Pawn,Capsule,Q))
                AddError(FString::Printf(TEXT("roof route capsule blocked seed=%d point=%s"),Seed,*S.ToString()));
        for(const auto& Route:L.RooftopRoutes)
        {
            const auto Landing=Route.Last();const bool HitRoof=World->LineTraceSingleByChannel(Hit,Landing+FVector(0,0,150),Landing-FVector(0,0,200),ECC_WorldStatic,Q);
            const auto* Base=HitRoof ? Cast<UInstancedStaticMeshComponent>(Hit.GetComponent()):nullptr;
            TestTrue(TEXT("roof landing is supported by the actual house mesh"),(Base && Base->GetStaticMesh() && Base->GetStaticMesh()->GetName().Contains(TEXT("House_"))) || (HitRoof && Hit.GetComponent()->ComponentHasTag(TEXT("ERDestructibleShell"))));
        }
        TArray<FVector> Interior;TestTrue(TEXT("actual source opening admits a standing player into a house"),ERProcedural::FindAlleyInteriorRoute(World,L,Interior));
        if(T->Town.ProceduralHouses)
        {
            TArray<FVector> RimRoute;TestTrue(TEXT("remaining low rim has physical standing support"),ERProcedural::FindAlleyRoofRimRoute(World,L,RimRoute));
            int32 Doors=0;
            for(const auto& House:L.Houses)for(const auto& Entry:House.Entrances)
            {
                ++Doors;
                for(int32 I=1;I<Entry.Num();++I)
                    if(World->SweepSingleByChannel(Hit,Entry[I-1]+FVector(0,0,20),Entry[I]+FVector(0,0,20),FQuat::Identity,ECC_Pawn,Capsule,Q))
                        AddError(FString::Printf(TEXT("module entry blocked seed=%d house=%s from=%s to=%s component=%s"),Seed,*House.StructureName.ToString(),*Entry[I-1].ToString(),*Entry[I].ToString(),Hit.GetComponent() ? *Hit.GetComponent()->GetName():TEXT("none")));
                for(const auto& Point:Entry)
                    TestTrue(TEXT("door and flat interior have physical footing"),World->LineTraceSingleByChannel(Hit,Point,Point-FVector(0,0,150),ECC_WorldStatic,Q) && Hit.ImpactNormal.Z>.7);
            }
            TestTrue(TEXT("all recorded street doors were physically checked"),Doors>=4);
            for(const auto& Transfer:L.RoofTransfers)
            {
                for(int32 I=0;I<Transfer.Num();++I)
                {
                    const auto Point=Transfer[I];
                    if(World->OverlapBlockingTestByChannel(Point,FQuat::Identity,ECC_Pawn,Capsule,Q))
                        AddError(FString::Printf(TEXT("roof transfer standing clearance seed=%d point=%s route=%s -> %s"),Seed,*Point.ToString(),*Transfer[1].ToString(),*Transfer[2].ToString()));
                    TestTrue(TEXT("roof crossing has actual deck support through the rim opening"),World->LineTraceSingleByChannel(Hit,Point,Point-FVector(0,0,120),ECC_WorldStatic,Q) && FMath::Abs(Hit.ImpactPoint.Z-(Point.Z-100))<1);
                }
                const float Clearance=FMath::Max(Transfer[1].Z,Transfer[2].Z)+10;
                auto A=Transfer[1],B=Transfer[2];A.Z=B.Z=Clearance;
                TestFalse(TEXT("no hidden roof lip obstructs the short jump"),World->SweepTestByChannel(A,B,FQuat::Identity,ECC_Pawn,Capsule,Q));
            }
        }
        for(const auto& Route:L.GroundRoutes)
        {
            float Low=10000,High=-10000;
            for(const auto& S:Route)
            {
                TestTrue(TEXT("visible sloping ground has actual collision"),World->LineTraceSingleByChannel(Hit,S+FVector(0,0,100),S-FVector(0,0,200),ECC_WorldStatic,Q));
                TestTrue(TEXT("ground height follows the terrain mesh"),FMath::Abs(Hit.ImpactPoint.Z-ERProcedural::AlleyGroundHeight(L,FVector2D(S)))<3);
                Low=FMath::Min(Low,float(Hit.ImpactPoint.Z));High=FMath::Max(High,float(Hit.ImpactPoint.Z));
            }
            TestTrue(TEXT("ground route changes elevation by over one metre"),High-Low>100);
        }
        if(L.UnderpassAxis)
        {
            for(int32 I=1;I<L.UnderpassRoute.Num();++I)
            {
                const auto A=L.UnderpassRoute[I-1],Next=L.UnderpassRoute[I];
                if(World->SweepSingleByChannel(Hit,A,Next,FQuat::Identity,ECC_Pawn,Capsule,Q))
                {
                    const bool Floor=Hit.GetComponent() && Hit.GetComponent()->GetName()==TEXT("AlleyTerrain") && Hit.ImpactNormal.Z>=.72;
                    if(!Floor){AddError(FString::Printf(TEXT("underpass blocked seed=%d point=%s component=%s normal=%s"),Seed,*Next.ToString(),Hit.GetComponent() ? *Hit.GetComponent()->GetName():TEXT("none"),*Hit.ImpactNormal.ToString()));break;}
                }
                if(!(World->LineTraceSingleByChannel(Hit,Next+FVector(0,0,40),Next-FVector(0,0,120),ECC_WorldStatic,Q) && FMath::Abs(Hit.ImpactPoint.Z-(Next.Z-100))<4))
                {AddError(FString::Printf(TEXT("underpass footing seed=%d point=%s hit=%s component=%s"),Seed,*Next.ToString(),*Hit.ImpactPoint.ToString(),Hit.GetComponent() ? *Hit.GetComponent()->GetName():TEXT("none")));break;}
            }
            for(const auto& CoverBounds:L.LowCovers)
            {const auto CoverPoint=CoverBounds.GetCenter();TestTrue(TEXT("low obstacle has real collision"),World->LineTraceTestByChannel(CoverPoint-FVector(CoverBounds.GetSize().X+30,0,0),CoverPoint+FVector(CoverBounds.GetSize().X+30,0,0),ECC_Pawn,Q));}
            const FVector RoadPoint=L.UnderpassBreaches[0].GetCenter();FERCellId Id;
            TestTrue(TEXT("intact road is a real damageable cell surface"),World->LineTraceSingleByChannel(Hit,RoadPoint+FVector(0,0,150),RoadPoint-FVector(0,0,50),ECC_GameTraceChannel1,Q) && Map->ResolveCell(Hit,Id));
            const auto* HE=LoadObject<UERWeaponData>(nullptr,TEXT("/Game/EclipseRecoil/Weapons/LowPoly/DA_Grenade_RGD5"));
            if(!TestNotNull(TEXT("saved HE structural profile"),HE))break;
            const auto Blast=AERBlockRoom::ApplyWorldExplosion(World,RoadPoint+FVector(0,0,10),HE->BlockExplosion,nullptr);
            const bool Blocked=World->SweepSingleByChannel(Hit,RoadPoint+FVector(0,0,100),RoadPoint-FVector(0,0,260),FQuat::Identity,ECC_Pawn,Capsule,Q);
            if(Blocked)AddInfo(FString::Printf(TEXT("ER_ALLEY_BREACH seed=%d damaged=%d removed=%d hit=%s component=%s"),Seed,Blast.Damaged,Blast.Destroyed,*Hit.ImpactPoint.ToString(),Hit.GetComponent() ? *Hit.GetComponent()->GetName():TEXT("none")));
            TestFalse(TEXT("blast opens a standing capsule hole through the road and terrain"),Blocked);
            TestTrue(TEXT("breach reveals the lower floor instead of an invisible ground plane"),World->LineTraceSingleByChannel(Hit,RoadPoint+FVector(0,0,150),RoadPoint-FVector(0,0,400),ECC_WorldStatic,Q) && FMath::IsNearlyEqual(Hit.ImpactPoint.Z,-360.f,1.f));
        }
    }
    World->DestroyWorld(false);AddInfo(TEXT("ER_ALLEYS_COLLISION seeds=6"));return true;
}
#endif
