#include "ERProceduralMap.h"
#include "ERMapPlayability.h"
#include "ERMapSelection.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERInitialSpawnRuleTest,"EclipseRecoil.Maps.InitialSpawnOcclusion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERInitialSpawnRuleTest::RunTest(const FString&)
{
    FERProceduralLayout L;
    L.AlphaSpawns.Add(FTransform(FVector(0,-1000,100)));L.OmegaSpawns.Add(FTransform(FVector(0,1000,100)));
    TestFalse(TEXT("open opposing starts are invalid"),ERPlayability::InitialSpawnsOccluded(L));
    FERLayoutFixedPiece Screen;Screen.Material=EERBlockMaterial::Concrete;
    Screen.Transform=FTransform(FQuat::Identity,FVector(0,0,150),FVector(20,.2,3));L.FixedPieces.Add(Screen);
    TestTrue(TEXT("visible opaque wall screens the starts"),ERPlayability::InitialSpawnsOccluded(L));
    for(float X:{-600.f,600.f})
    {L.AlphaSpawns.Emplace(FVector(X,-1000,100));L.OmegaSpawns.Emplace(FVector(-X,1000,100));}
    TestTrue(TEXT("three separated paired spawn zones are accepted when every opposing pair is screened"),ERPlayability::InitialSpawnsOccluded(L));
    L.AlphaSpawns[2].SetLocation({2000,-1000,100});
    TestFalse(TEXT("an exposed secondary spawn zone is rejected"),ERPlayability::InitialSpawnsOccluded(L));
    L.AlphaSpawns.SetNum(1);L.OmegaSpawns.SetNum(1);
    L.FixedPieces[0].VisualOnly=true;TestFalse(TEXT("decoration cannot prove occlusion"),ERPlayability::InitialSpawnsOccluded(L));L.FixedPieces[0].VisualOnly=false;
    L.FixedPieces[0].CollisionOnly=true;TestFalse(TEXT("invisible collision cannot prove visual occlusion"),ERPlayability::InitialSpawnsOccluded(L));L.FixedPieces[0].CollisionOnly=false;
    L.FixedPieces[0].PawnBarrier=true;TestFalse(TEXT("perforated pawn barrier cannot prove sight cover"),ERPlayability::InitialSpawnsOccluded(L));L.FixedPieces[0].PawnBarrier=false;
    L.FixedPieces[0].Material=EERBlockMaterial::Glass;TestFalse(TEXT("glass cannot prove sight cover"),ERPlayability::InitialSpawnsOccluded(L));L.FixedPieces[0].Material=EERBlockMaterial::Concrete;
    L.FixedPieces[0].MeshCollision=true;TestFalse(TEXT("unknown imported collision cannot prove occlusion"),ERPlayability::InitialSpawnsOccluded(L));L.FixedPieces[0].MeshCollision=false;
    L.FixedPieces[0].Transform.SetScale3D({.1,.2,3});TestFalse(TEXT("a blocked centre ray cannot hide the capsule edges"),ERPlayability::InitialSpawnsOccluded(L));
    L.FixedPieces[0]=Screen;
    for(int32 I=1;I<100;++I){L.AlphaSpawns.Add(FTransform(FVector(0,-1000,100)));L.OmegaSpawns.Add(FTransform(FVector(0,1000,100)));}
    L.AlphaSpawns[17].SetLocation({2000,-1000,100});L.OmegaSpawns[17].SetLocation({2000,1000,100});
    TestFalse(TEXT("an exposed pair outside the old sampling stride is still rejected"),ERPlayability::InitialSpawnsOccluded(L));
    for(const auto Archetype:{EERMapArchetype::CentralBuilding,EERMapArchetype::MiddleEasternAlleys,EERMapArchetype::FireTraining,EERMapArchetype::Rooms,EERMapArchetype::Village})for(int32 Team=1;Team<=16;++Team)
        TestTrue(TEXT("all playable archetypes and sizes forbid initial spawn sight"),ERMapSelection::ProfileFor(Archetype,Team).bForbidSpawnSight);
    auto* T=LoadObject<UERMapTemplate>(nullptr,TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_CentralBuilding.DA_CentralBuilding"));
    if(!TestNotNull(TEXT("Central Building recipe"),T))return false;
    UWorld::InitializationValues Values;Values.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);if(!World)return false;
    auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;Map->LayoutConfig.ExpectedTeamSize=2;Map->bCeilings=false;
    for(int32 Seed:{42,73})
    {
        Map->LayoutConfig.Seed=Seed;
        if(TestTrue(TEXT("screened circular map generated"),Map->Regenerate()))
        {
            const auto& Layout=Map->GetLayout();
            TestTrue(TEXT("all real sight and projectile lines are blocked before readiness"),ERPlayability::InitialSpawnsOccludedInWorld(World,Map->GetActorTransform(),Layout));
            TestTrue(TEXT("validated map is ready"),Map->IsMapReady());
            const auto Policy=ERPlayability::PolicyFor(EERMapArchetype::CentralBuilding,2);
            FERProceduralLayout Full;
            if(!TestTrue(TEXT("rebuild complete report descriptor"),ERProcedural::Build(Map->LayoutConfig,Map->Config,Full,Map->CellSettings)))continue;
            auto Report=ERPlayability::Analyze(Full,Map->LayoutConfig,Policy);
            ERPlayability::ConfirmInWorld(Report,World,Map->GetActorTransform(),Layout,Policy);
            const auto* Sight=Report.Find(TEXT("INITIAL_SPAWN_LOS"));
            if(TestNotNull(TEXT("physical spawn metric"),Sight))
            {
                TestEqual(TEXT("zero visible opposing pairs"),Sight->Value(TEXT("visible_pairs")),0.);
                TestEqual(TEXT("zero open shot pairs"),Sight->Value(TEXT("open_shot_pairs")),0.);
                TestEqual(TEXT("all pairs measured"),Sight->Value(TEXT("pairs_sampled")),double(Layout.AlphaSpawns.Num()*Layout.OmegaSpawns.Num()));
            }
        }
    }
    Map->LayoutConfig.ExpectedTeamSize=17;
    TestFalse(TEXT("invalid gameplay request cannot regenerate"),Map->Regenerate());
    TestFalse(TEXT("failed generation is never ready"),Map->IsMapReady());
    Map->LayoutConfig.ExpectedTeamSize=2;
    TestTrue(TEXT("valid regeneration recovers readiness"),Map->Regenerate());
    TestTrue(TEXT("recovered map is ready"),Map->IsMapReady());
    World->DestroyWorld(false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERSpawnZonesTest,"EclipseRecoil.Maps.SpawnZones",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERSpawnZonesTest::RunTest(const FString&)
{
    for(const TCHAR* Id:{TEXT("CentralBuilding"),TEXT("MiddleEasternAlleys"),TEXT("FireTraining"),TEXT("Rooms"),TEXT("Village")})
    {
        auto* Template=LoadObject<UERMapTemplate>(nullptr,*(FString(TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_"))+Id));
        if(!TestNotNull(Id,Template))return false;
        TSet<int32> Counts;
        for(int32 Seed=0;Seed<18;++Seed)
        {
            FERProceduralConfig Config;Config.TemplateAsset=Template;Config.ExpectedTeamSize=3;Config.Seed=Seed;
            FERBlockRoomConfig Blocks;FERProceduralLayout Layout;
            if(!TestTrue(*FString::Printf(TEXT("%s seed%d spawn zones build"),Id,Seed),ERProcedural::Build(Config,Blocks,Layout)))return false;
            const int32 Zones=Layout.SpawnZones.Num()/2;Counts.Add(Zones);
            TestTrue(TEXT("one to three paired zones"),Zones>=1 && Zones<=3 && Layout.SpawnZones.Num()==Zones*2);
            TestTrue(TEXT("every opposing start pair remains screened"),ERPlayability::InitialSpawnsOccluded(Layout));
            for(int32 Team=0;Team<2;++Team)
            {
                int32 Covered=0;const auto& Bank=Team ? Layout.OmegaSpawns:Layout.AlphaSpawns;
                for(int32 I=0;I<Zones;++I)
                {
                    const auto& Zone=Layout.SpawnZones[Team*Zones+I];
                    TestEqual(TEXT("zone team"),Zone.Team,Team);TestEqual(TEXT("start ranges partition the complete bank"),Zone.FirstStart,Covered);
                    TestTrue(TEXT("each zone has at least two player starts"),Zone.StartCount>=2);
                    Covered+=Zone.StartCount;
                    for(int32 J=0;J<I;++J)TestTrue(TEXT("zone centers are at least10m apart"),FVector::Dist2D(Zone.Center,Layout.SpawnZones[Team*Zones+J].Center)>=999.9);
                    const auto& Mate=Layout.SpawnZones[(1-Team)*Zones+I];FVector Expected=Zone.Center;
                    if(Layout.LayoutSymmetry!=EERTemplateSymmetry::Mirror)Expected.X=-Expected.X;Expected.Y=-Expected.Y;
                    TestTrue(TEXT("zone centers use the template symmetry"),Expected.Equals(Mate.Center,.01));
                    if(I>0)TestTrue(TEXT("secondary zone has a route to existing circulation"),Zone.ExitRoute.Num()>=3);
                }
                TestEqual(TEXT("all original player slots survive distribution"),Covered,Bank.Num());
            }
            if(Template->Archetype==EERMapArchetype::Rooms && Zones>1)
            {
                const auto Graph=ERPlayability::BuildGraph(Layout,Template->Archetype,ERPlayability::PolicyFor(Template->Archetype,3));
                int32 GraphStarts[2]={0,0};
                for(const auto& Node:Graph.Nodes)if(Node.Kind==EERTacticalNodeKind::Spawn && Node.Team>=0 && Node.Team<2)++GraphStarts[Node.Team];
                TestTrue(TEXT("room graph retains primary and secondary spawn zones for both teams"),GraphStarts[0]>=Zones && GraphStarts[1]>=Zones);
            }
            AddInfo(FString::Printf(TEXT("ER_SPAWN_ZONES template=%s seed=%d zones=%d starts=%d"),Id,Seed,Zones,Layout.AlphaSpawns.Num()));
        }
        TestTrue(*(FString(Id)+TEXT(" samples one, two AND three actual spawn zones")),Counts.Contains(1) && Counts.Contains(2) && Counts.Contains(3));
    }
    return true;
}
#endif
