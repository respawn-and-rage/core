#include "ERProceduralMap.h"
#include "ERMapPlayability.h"
#include "ERLayoutSolids.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERUrbanTownLayoutTest,"EclipseRecoil.Maps.Town.Layout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERUrbanTownLayoutTest::RunTest(const FString&)
{
    FERProceduralLayout AreaStudy;AreaStudy.ArenaSize={1000,1000,1400};
    AreaStudy.InaccessibleGround={FBox(FVector(-600,-500,0),FVector(0,500,1000)),FBox(FVector(-250,-500,0),FVector(250,500,1000))};
    TestEqual(TEXT("inaccessible area is clipped and overlaps count only once"),ERProcedural::PlayableAreaM2(AreaStudy),25.);
    AreaStudy.InaccessibleGround.Reset();
    AreaStudy.InaccessibleGroundPolygons={{{-500,-500,0},{500,-500,0},{500,500,0}},{{-500,-500,0},{500,-500,0},{-500,500,0}}};
    TestEqual(TEXT("crossing diagonal exclusions count their union exactly"),ERProcedural::PlayableAreaM2(AreaStudy),25.);
    AreaStudy.InaccessibleGroundPolygons.SetNum(1);
    TestEqual(TEXT("triangular exclusion uses actual area, not its bounding box"),ERProcedural::PlayableAreaM2(AreaStudy),50.);
    auto* T=NewObject<UERMapTemplate>();T->TemplateId=TEXT("town.urban");T->Archetype=EERMapArchetype::UrbanTown;T->Symmetry=EERTemplateSymmetry::HalfTurn;
    T->ArenaSize={5000,6000,1400};
    FERProceduralConfig C;C.TemplateAsset=T;C.ExpectedTeamSize=2;FERBlockRoomConfig B;
    for(int32 I=0;I<516;++I)
    {
        C.Seed=I<512 ? I:I==512 ? 7919:I==513 ? -17:I==514 ? INT32_MAX:INT32_MIN;
        FERProceduralLayout L,Again;
        if(!TestTrue(TEXT("Town seed builds atomically"),ERProcedural::Build(C,B,L)))return false;
        TestTrue(TEXT("every opposing initial ray is screened"),ERPlayability::InitialSpawnsOccluded(L));
        TestTrue(TEXT("urban footprint meets team area"),FMath::Abs(ERProcedural::PlayableAreaM2(L)-ERProcedural::TargetAreaM2(2,EERMapArchetype::UrbanTown))<=ERProcedural::TargetAreaM2(2,EERMapArchetype::UrbanTown)*.1);
        TestTrue(TEXT("closed lots are excluded from gross area"),!L.InaccessibleGround.IsEmpty() && ERProcedural::PlayableAreaM2(L)<L.ArenaSize.X*L.ArenaSize.Y/10000.);
        TestTrue(TEXT("oblique inaccessible corners"),L.InaccessibleGroundPolygons.Num()>=2);
        TestTrue(TEXT("central galleries join the room wings"),L.TownFootprints.Num()>L.TownBuildings.Num());
        for(const auto& Outline:L.TownFootprints)TestTrue(TEXT("room wings and galleries have polygon plans"),Outline.Num()>=4);
        TestTrue(TEXT("paired central through-buildings"),L.TownBuildings.Num()>=2 && L.TownBuildings.Num()%2==0);
        TestTrue(TEXT("all circulation remains on the ground"),L.Ladders.IsEmpty() && L.StairRoutes.IsEmpty() && L.RooftopRoutes.IsEmpty());
        TestEqual(TEXT("both base bypasses and their half-turn mates are reserved"),L.SpawnExitRoutes.Num(),4);
        ERLayoutQueries::FLayoutSolids SpawnSolids(L);
        const double HX=L.ArenaSize.X*.5,HY=L.ArenaSize.Y*.5;
        for(double Side:{-45.,45.})TestTrue(TEXT("two separated standing lanes fit the perimeter spawn bypass"),
            SpawnSolids.PawnClear({-HX+120+Side,-HY+550,100},{-HX+120+Side,-HY+800,100}));
        for(const auto& Path:L.RoomAccessRoutes)for(const auto& P:Path)TestTrue(TEXT("ground route height"),P.Z>=100 && P.Z<=112);
        for(int32 J=0;J<L.TownBuildings.Num();J+=2)
            TestTrue(TEXT("building footprints are half-turn pairs"),L.TownBuildings[J].TransformBy(FTransform(FRotator(0,180,0))).GetCenter().Equals(L.TownBuildings[J+1].GetCenter(),.01));
        if(I<3)
        {
            TestTrue(TEXT("deterministic repeat"),ERProcedural::Build(C,B,Again));
            TestEqual(TEXT("repeat panels"),L.Panels.Num(),Again.Panels.Num());
            for(int32 J=0;J<L.Panels.Num();++J)TestTrue(TEXT("repeat panel frames"),L.Panels[J].Transform.Equals(Again.Panels[J].Transform));
        }
    }
    // Include the smallest profile and the full sixteen-player area budget.
    for(int32 Team=1;Team<=16;++Team)for(int32 Seed:{0,1,42,73,7919,-17,INT32_MIN,INT32_MAX})
    {
        C.ExpectedTeamSize=Team;C.Seed=Seed;FERProceduralLayout L;
        if(!TestTrue(TEXT("Town supports every team size"),ERProcedural::Build(C,B,L)))return false;
        TestTrue(TEXT("sized area"),FMath::Abs(ERProcedural::PlayableAreaM2(L)-ERProcedural::TargetAreaM2(Team,EERMapArchetype::UrbanTown))<=ERProcedural::TargetAreaM2(Team,EERMapArchetype::UrbanTown)*.1);
        FERCellWorld World;FString Error;
        const bool Built=World.Build(L.Panels,FERCellSettings(),GetDefault<UERBlockMaterialCatalog>(),Error);
        if(!TestTrue(*FString::Printf(TEXT("Town %dv%d fits calibrated cell budget: %s"),Team,Team,*Error),Built))return false;
    }
    AddInfo(TEXT("ER_TOWN_LAYOUT seeds=516 sizes=1..16 PASS"));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERUrbanTownPhysicsTest,"EclipseRecoil.Maps.Town.Physics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERUrbanTownPhysicsTest::RunTest(const FString&)
{
    auto* T=LoadObject<UERMapTemplate>(nullptr,TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_Town"));if(!TestNotNull(TEXT("Town recipe"),T))return false;
    UWorld::InitializationValues V;V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);if(!World)return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;Map->bCeilings=false;
    for(int32 Team:{1,2,4})for(int32 Seed:{42,73,7919})
    {
        Map->LayoutConfig.ExpectedTeamSize=Team;Map->LayoutConfig.Seed=Seed;
        if(!TestTrue(TEXT("Town physical generation"),Map->Regenerate()))continue;
        const auto& L=Map->GetLayout();TestTrue(TEXT("valid calibrated cells"),Map->ValidateCells());
        TestTrue(TEXT("supported and clear spawn zones"),ERProcedural::SpawnZonesClearInWorld(World,Map->GetActorTransform(),L));
        TestTrue(TEXT("all real sight and projectile rays screened"),ERPlayability::InitialSpawnsOccludedInWorld(World,FTransform::Identity,L));
        FCollisionQueryParams Q(SCENE_QUERY_STAT(ERTownPhysics),false);FHitResult Hit;
        const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);
        const double HX=L.ArenaSize.X*.5,HY=L.ArenaSize.Y*.5;
        for(double Side:{-45.,45.})TestFalse(TEXT("two standing players fit the primary spawn bypass"),
            World->SweepSingleByChannel(Hit,{-HX+120+Side,-HY+550,100},{-HX+120+Side,-HY+800,100},FQuat::Identity,ECC_Pawn,Capsule,Q));
        TArray<TArray<FVector>> Paths=L.RoomAccessRoutes;Paths.Append(L.CrossingRoutes);
        for(const auto& Path:Paths)for(int32 I=1;I<Path.Num();++I)
            TestFalse(TEXT("standing capsule traverses urban route"),World->SweepSingleByChannel(Hit,Path[I-1],Path[I],FQuat::Identity,ECC_Pawn,Capsule,Q));
        const uint32 Digest=Map->GetStateDigest();
        const auto& Cells=Map->GetCellWorld();
        TMap<FERCellId,float> Damage;const FERCellId Cell{0,0,0,0};
        if(Cells.IsAlive(Cell))Damage.Add(Cell,Cells.GetHealth(Cell)+1);
        Map->ApplyCellBatch(Damage);TestTrue(TEXT("damage changes authoritative state"),Map->GetStateDigest()!=Digest);
        TestTrue(TEXT("regeneration restores map"),Map->Regenerate());TestEqual(TEXT("regeneration restores digest"),Map->GetStateDigest(),Digest);
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
