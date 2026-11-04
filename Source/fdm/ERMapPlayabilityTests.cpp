#include "ERMapPlayability.h"
#include "ERProceduralMap.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
// A straight test graph: nodes are placed on a line, edges are listed by index.
FERTacticalGraph MakeGraph(int32 Count,const TArray<FIntPoint>& Links)
{
    FERTacticalGraph G;G.Coverage=EERGraphCoverage::Exact;G.bExitsBounded=true;
    for(int32 I=0;I<Count;++I)
    {
        auto& V=G.Nodes.AddDefaulted_GetRef();V.Position={I*1000.f,0,0};V.Source=FString::Printf(TEXT("n%d"),I);V.Kind=EERTacticalNodeKind::Room;
    }
    for(const auto& Link:Links)G.AddEdge(Link.X,Link.Y,EERTacticalMove::Walk,TEXT("test"),true,300);
    return G;
}
void SetBases(FERTacticalGraph& G,int32 Alpha,int32 Omega)
{
    G.Nodes[Alpha].bBase=true;G.Nodes[Alpha].Team=0;G.Nodes[Omega].bBase=true;G.Nodes[Omega].Team=1;
}
FERPlayabilityReport Evaluate(const FERTacticalGraph& G)
{
    FERPlayabilityReport R;R.Graph=G;
    for(const TCHAR* Code:{TEXT("CONNECTED_ZONES"),TEXT("BASE_EXIT_PATHS"),TEXT("ROUTE_DIVERSITY"),TEXT("INITIAL_SPAWN_LOS")})R.Metrics.AddDefaulted_GetRef().Code=Code;
    ERPlayability::EvaluateGraph(R,FERPlayabilityPolicy());
    return R;
}
TArray<int32> IntegerList(const TCHAR* Key,const TCHAR* Default)
{
    FString Text=Default;FParse::Value(FCommandLine::Get(),Key,Text,false);
    TArray<FString> Words;Text.ParseIntoArray(Words,TEXT(","));
    TArray<int32> Out;for(const auto& W:Words)Out.Add(FCString::Atoi(*W));
    return Out;
}
const TCHAR* const PlayabilityTemplates[]={TEXT("CentralBuilding"),TEXT("MiddleEasternAlleys"),TEXT("FireTraining"),TEXT("Rooms"),TEXT("Village"),TEXT("Town")};
UERMapTemplate* LoadTemplate(const TCHAR* Id)
{
    return LoadObject<UERMapTemplate>(nullptr,*(FString(TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_"))+Id));
}
// Geometry fingerprint of a layout before the cell world consumes it.
uint32 LayoutFingerprint(const FERProceduralLayout& L)
{
    uint32 H=GetTypeHash(L.Panels.Num());H=HashCombine(H,GetTypeHash(L.FixedPieces.Num()));H=HashCombine(H,GetTypeHash(L.RoomLinks.Num()));
    auto Mix=[&](FVector P){for(int32 I=0;I<3;++I)H=HashCombine(H,GetTypeHash(FMath::RoundToInt32(P[I]*10)));};
    Mix(L.ArenaSize);for(const auto& S:L.AlphaSpawns)Mix(S.GetLocation());for(const auto& P:L.Panels)Mix(P.Transform.GetLocation());for(const auto& P:L.FixedPieces)Mix(P.Transform.GetLocation());
    return H;
}
FString CsvRow(const FERPlayabilityReport& R)
{
    auto Value=[&](const TCHAR* Code,const TCHAR* Name,double Default=0){const auto* M=R.Find(Code);return M ? M->Value(Name,Default):Default;};
    auto Status=[&](const TCHAR* Code){const auto* M=R.Find(Code);return M ? ERPlayability::StatusName(M->Status):TEXT("missing");};
    double Ms=0;for(const auto& T:R.TimingsMs)Ms+=T.Value;
    return FString::Printf(TEXT("%s,%d,%d,%s,%s,%s,%s,%s,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%.2f\n"),ERPlayability::ArchetypeName(R.Archetype),R.ExpectedTeamSize,R.Seed,
        ERPlayability::StatusName(R.Overall()),R.Graph.Coverage==EERGraphCoverage::Exact ? TEXT("exact"):TEXT("routes"),Status(TEXT("CONNECTED_ZONES")),Status(TEXT("BASE_EXIT_PATHS")),Status(TEXT("INITIAL_SPAWN_LOS")),
        Value(TEXT("CONNECTED_ZONES"),TEXT("nodes")),Value(TEXT("CONNECTED_ZONES"),TEXT("edges")),Value(TEXT("CONNECTED_ZONES"),TEXT("components")),Value(TEXT("CONNECTED_ZONES"),TEXT("edges_unconfirmed")),
        Value(TEXT("CONNECTED_ZONES"),TEXT("base_to_base_m")),Value(TEXT("BASE_EXIT_PATHS"),TEXT("alpha_exits")),Value(TEXT("BASE_EXIT_PATHS"),TEXT("alpha_independent_paths")),Value(TEXT("BASE_EXIT_PATHS"),TEXT("alpha_chokes")),
        Value(TEXT("INITIAL_SPAWN_LOS"),TEXT("visible_pairs")),Value(TEXT("INITIAL_SPAWN_LOS"),TEXT("open_shot_pairs"),-1),Ms);
}
const TCHAR* const CsvHeader=TEXT("archetype,team,seed,overall,coverage,connected,exits,spawn_los,nodes,edges,components,edges_unconfirmed,base_to_base_m,alpha_exits,alpha_paths,alpha_chokes,visible_pairs,open_shot_pairs,total_ms\n");
FString OutputDirectory(){return FPaths::ProjectSavedDir()/TEXT("Testing/MapPlayability");}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERPlayabilityGraphTest,"EclipseRecoil.Maps.Playability.Graphs",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERPlayabilityGraphTest::RunTest(const FString&)
{
    using namespace ERPlayability;
    {
        // Isolated zone: bases meet, node 3 hangs alone.
        auto G=MakeGraph(4,{{0,1},{1,2}});SetBases(G,0,2);
        const auto Ids=Components(G);
        TestEqual(TEXT("isolated zone has its own component"),Ids[3]==Ids[0],false);
        auto R=Evaluate(G);const auto* C=R.Find(TEXT("CONNECTED_ZONES"));
        TestEqual(TEXT("exact graph fails an isolated zone"),C->Status,EERPlayabilityStatus::Fail);
        TestEqual(TEXT("one isolated node is reported"),C->Value(TEXT("isolated_nodes")),1.);
        TestEqual(TEXT("violation names the node"),C->Violations[0].Nodes[0],3);
        G.Coverage=EERGraphCoverage::Routes;
        TestEqual(TEXT("route graph treats a gap as a warning"),Evaluate(G).Find(TEXT("CONNECTED_ZONES"))->Status,EERPlayabilityStatus::Warn);
    }
    {
        // Disconnected bases.
        auto G=MakeGraph(4,{{0,1},{2,3}});SetBases(G,0,3);
        auto R=Evaluate(G);
        TestEqual(TEXT("disconnected bases fail"),R.Find(TEXT("CONNECTED_ZONES"))->Status,EERPlayabilityStatus::Fail);
        TestEqual(TEXT("no independent path"),R.Find(TEXT("BASE_EXIT_PATHS"))->Value(TEXT("alpha_independent_paths")),0.);
        TestEqual(TEXT("unreachable distance is explicit"),R.Find(TEXT("CONNECTED_ZONES"))->Value(TEXT("base_to_base_m")),-1.);
    }
    {
        // Single forced exit, a bridge in the middle of the map.
        auto G=MakeGraph(3,{{0,1},{1,2}});SetBases(G,0,2);
        TestEqual(TEXT("one path"),DisjointPaths(G,{0},{2},8),1);
        TestEqual(TEXT("bridge node is the choke"),CommonChokes(G,{0},{2}),TArray<int32>{1});
        auto R=Evaluate(G);const auto* X=R.Find(TEXT("BASE_EXIT_PATHS"));
        TestEqual(TEXT("single exit warns while report-only"),X->Status,EERPlayabilityStatus::Warn);
        TestEqual(TEXT("one exit counted"),X->Value(TEXT("alpha_exits")),1.);
        TestEqual(TEXT("choke positions are reported"),X->Violations[0].Positions[0],FVector(1000,0,0));
    }
    {
        // Two exits that join at the same choke before reaching the enemy.
        auto G=MakeGraph(6,{{0,1},{0,2},{1,3},{2,3},{3,4},{4,5}});SetBases(G,0,5);
        auto R=Evaluate(G);const auto* X=R.Find(TEXT("BASE_EXIT_PATHS"));
        TestEqual(TEXT("two exits seen"),X->Value(TEXT("alpha_exits")),2.);
        TestEqual(TEXT("merged exits are one independent path"),X->Value(TEXT("alpha_independent_paths")),1.);
        TestEqual(TEXT("merged exits warn"),X->Status,EERPlayabilityStatus::Warn);
        TestEqual(TEXT("both common nodes found, nearest first"),CommonChokes(G,{0},{5}),TArray<int32>({3,4}));
        TestEqual(TEXT("touching chokes group as one"),X->Value(TEXT("alpha_chokes")),1.);
    }
    {
        // Two genuinely separate routes.
        auto G=MakeGraph(6,{{0,1},{1,2},{2,5},{0,3},{3,4},{4,5}});SetBases(G,0,5);
        TestEqual(TEXT("two independent paths"),DisjointPaths(G,{0},{5},8),2);
        TestTrue(TEXT("no common choke"),CommonChokes(G,{0},{5}).IsEmpty());
        auto R=Evaluate(G);
        TestEqual(TEXT("independent exits pass"),R.Find(TEXT("BASE_EXIT_PATHS"))->Status,EERPlayabilityStatus::Pass);
        TestEqual(TEXT("connected graph passes"),R.Find(TEXT("CONNECTED_ZONES"))->Status,EERPlayabilityStatus::Pass);
        G.Edges[0].bConfirmed=false;
        TestEqual(TEXT("an unconfirmed edge is never a pass"),Evaluate(G).Find(TEXT("BASE_EXIT_PATHS"))->Status,EERPlayabilityStatus::Warn);
        G.Edges[0].bConfirmed=true;G.Edges[0].bBlocked=true;
        auto Cut=Evaluate(G);
        TestEqual(TEXT("a blocked edge no longer counts"),Cut.Find(TEXT("BASE_EXIT_PATHS"))->Value(TEXT("alpha_independent_paths")),1.);
        TestEqual(TEXT("blocked edges are reported"),Cut.Find(TEXT("CONNECTED_ZONES"))->Value(TEXT("edges_blocked")),1.);
    }
    {
        // Circular graph: the ring gives two ways round, a rung stays one vertex wide.
        auto G=MakeGraph(6,{{0,1},{1,2},{2,3},{3,4},{4,5},{5,0}});SetBases(G,0,3);
        TestEqual(TEXT("ring has two ways round"),DisjointPaths(G,{0},{3},8),2);
        TestEqual(TEXT("shortest arc is measured"),PathLength(G,{0},{3}),3000.f);
        TestEqual(TEXT("the limit bounds the flow"),DisjointPaths(G,{0},{3},1),1);
    }
    {
        // Spawn inside a room with one door, two gates beyond it.
        auto G=MakeGraph(6,{{0,1},{1,2},{1,3},{2,5},{3,4},{4,5}});
        G.Nodes[0].Kind=EERTacticalNodeKind::Spawn;G.Nodes[0].Team=0;G.Nodes[0].bBase=true;
        G.Nodes[1].bBase=true;G.Nodes[1].Team=0;G.Nodes[5].bBase=true;G.Nodes[5].Team=1;
        auto R=Evaluate(G);const auto* X=R.Find(TEXT("BASE_EXIT_PATHS"));
        TestEqual(TEXT("court has two exits"),X->Value(TEXT("alpha_exits")),2.);
        TestEqual(TEXT("court has two paths"),X->Value(TEXT("alpha_independent_paths")),2.);
        TestEqual(TEXT("the door still chokes every spawn"),X->Value(TEXT("alpha_chokes")),1.);
        TestEqual(TEXT("door inside the base warns"),X->Status,EERPlayabilityStatus::Warn);
    }
    {
        // Missing metadata must never read as a pass.
        FERPlayabilityReport R=Evaluate(FERTacticalGraph());
        TestEqual(TEXT("no graph stays not evaluated"),R.Find(TEXT("CONNECTED_ZONES"))->Status,EERPlayabilityStatus::NotEvaluated);
        TestEqual(TEXT("no metric means no overall pass"),R.Overall(),EERPlayabilityStatus::NotEvaluated);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERPlayabilitySightTest,"EclipseRecoil.Maps.Playability.LayoutSight",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERPlayabilitySightTest::RunTest(const FString&)
{
    FERProceduralLayout L;L.AlphaSpawns.Emplace(FVector(0,-1000,100));L.OmegaSpawns.Emplace(FVector(0,1000,100));
    FERProceduralConfig C;const FERPlayabilityPolicy P;
    auto Sight=[&]{return ERPlayability::Analyze(L,C,P).Find(TEXT("INITIAL_SPAWN_LOS"));};
    auto Report=ERPlayability::Analyze(L,C,P);
    TestEqual(TEXT("open arena warns"),Report.Find(TEXT("INITIAL_SPAWN_LOS"))->Status,EERPlayabilityStatus::Warn);
    TestEqual(TEXT("one visible pair"),Report.Find(TEXT("INITIAL_SPAWN_LOS"))->Value(TEXT("visible_pairs")),1.);
    TestEqual(TEXT("offending starts are located"),Report.Find(TEXT("INITIAL_SPAWN_LOS"))->Violations[0].Positions[1],FVector(0,1000,100));
    auto Wall=[&](bool Barrier,bool Visual)
    {
        L.FixedPieces.Reset();FERLayoutFixedPiece Piece;Piece.Transform=FTransform(FQuat::Identity,FVector(0,0,150),FVector(40,.2,3));Piece.Material=EERBlockMaterial::Concrete;
        Piece.PawnBarrier=Barrier;Piece.VisualOnly=Visual;L.FixedPieces.Add(Piece);
        return ERPlayability::Analyze(L,C,P);
    };
    TestEqual(TEXT("a wall hides the pair"),Wall(false,false).Find(TEXT("INITIAL_SPAWN_LOS"))->Status,EERPlayabilityStatus::Pass);
    TestEqual(TEXT("a perforated barrier does not"),Wall(true,false).Find(TEXT("INITIAL_SPAWN_LOS"))->Status,EERPlayabilityStatus::Warn);
    TestEqual(TEXT("visual pieces have no collision"),Wall(false,true).Find(TEXT("INITIAL_SPAWN_LOS"))->Status,EERPlayabilityStatus::Warn);
    L.FixedPieces.Reset();
    auto Panel=[&](EERBlockMaterial Material,TArray<FBox2D> Holes)
    {
        L.Panels.Reset();auto& D=L.Panels.AddDefaulted_GetRef();D.Material=Material;D.Size={4000,20,300};D.Transform=FTransform(FVector(-2000,-10,0));D.Holes=MoveTemp(Holes);
        return ERPlayability::Analyze(L,C,P);
    };
    TestEqual(TEXT("a cell panel hides the pair"),Panel(EERBlockMaterial::Brick,{}).Find(TEXT("INITIAL_SPAWN_LOS"))->Status,EERPlayabilityStatus::Pass);
    TestEqual(TEXT("glass does not"),Panel(EERBlockMaterial::Glass,{}).Find(TEXT("INITIAL_SPAWN_LOS"))->Status,EERPlayabilityStatus::Warn);
    TestEqual(TEXT("a door hole opens the line"),Panel(EERBlockMaterial::Brick,{FBox2D(FVector2D(1900,0),FVector2D(2100,230))}).Find(TEXT("INITIAL_SPAWN_LOS"))->Status,EERPlayabilityStatus::Warn);
    L.Panels.Reset();
    L.RoomShell.Add({FVector(-2000,0,0),FVector(2000,0,0),FVector(2000,0,400),FVector(-2000,0,400)});
    TestEqual(TEXT("a shell face hides the pair"),Sight()->Status,EERPlayabilityStatus::Pass);
    L.RoomShell.Reset();
    // A hollow skin is unknown: a clear line through it cannot be called a defect or a pass.
    auto& Hollow=L.Panels.AddDefaulted_GetRef();Hollow.Skin.AddDefaulted(3);Hollow.Size={100,100,100};Hollow.Cell={5,5,5};
    TestEqual(TEXT("unknown solids leave the line undecided"),Sight()->Status,EERPlayabilityStatus::NotEvaluated);
    L.Panels.Reset();
    L.OmegaSpawns.Reset();
    TestEqual(TEXT("missing starts stay not evaluated"),Sight()->Status,EERPlayabilityStatus::NotEvaluated);
    return !HasAnyErrors();
}

// Layout stage on real recipes. -ERPlayabilityTeams= and -ERPlayabilitySeeds= size the batch;
// -ERPlayabilityReports keeps one JSON per plan beside the summary CSV.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERPlayabilityReportTest,"EclipseRecoil.Maps.Playability.Reports",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERPlayabilityReportTest::RunTest(const FString&)
{
    const auto Teams=IntegerList(TEXT("ERPlayabilityTeams="),TEXT("2,4,8"));
    const auto Seeds=IntegerList(TEXT("ERPlayabilitySeeds="),TEXT("42,73,7919"));
    const bool Keep=FParse::Param(FCommandLine::Get(),TEXT("ERPlayabilityReports"));
    FString Csv=CsvHeader;
    for(const TCHAR* Id:PlayabilityTemplates)
    {
        auto* T=LoadTemplate(Id);if(!TestNotNull(Id,T))return false;
        const bool Concrete=T->Archetype==EERMapArchetype::Rooms || T->Archetype==EERMapArchetype::FireTraining;
        for(int32 Team:Teams)for(int32 Seed:Seeds)
        {
            const FString Case=FString::Printf(TEXT("%s %dv%d seed%d"),Id,Team,Team,Seed);
            FERProceduralConfig C;C.TemplateAsset=T;C.ExpectedTeamSize=Team;C.Seed=Seed;FERBlockRoomConfig B;
            FERProceduralLayout L;
            if(!ERProcedural::Build(C,B,L)){AddError(Case+TEXT(" did not build"));continue;}
            const auto P=ERPlayability::PolicyFor(T->Archetype,Team);
            const uint32 Before=LayoutFingerprint(L);
            const auto Report=ERPlayability::Analyze(L,C,P);
            TestEqual(*(Case+TEXT(": analysis leaves the layout untouched")),LayoutFingerprint(L),Before);
            FERProceduralLayout Again;ERProcedural::Build(C,B,Again);
            TestEqual(*(Case+TEXT(": analysis leaves generation untouched")),LayoutFingerprint(Again),Before);
            const auto Repeat=ERPlayability::Analyze(Again,C,P);
            const FString Json=ERPlayability::ToJson(Report,false);
            TestEqual(*(Case+TEXT(": identical report bytes")),ERPlayability::ToJson(Repeat,false),Json);
            TSharedPtr<FJsonObject> Parsed;const auto Reader=TJsonReaderFactory<>::Create(Json);
            TestTrue(*(Case+TEXT(": report is valid JSON")),FJsonSerializer::Deserialize(Reader,Parsed) && Parsed.IsValid());
            TestEqual(*(Case+TEXT(": seven metric codes")),Report.Metrics.Num(),7);
            TestTrue(*(Case+TEXT(": graph exists")),Report.Graph.Nodes.Num()>0 && Report.Graph.Edges.Num()>0);
            for(const auto& E:Report.Graph.Edges)TestTrue(*(Case+TEXT(": edge ends exist")),Report.Graph.Nodes.IsValidIndex(E.A) && Report.Graph.Nodes.IsValidIndex(E.B));
            for(const auto& M:Report.Metrics)if(M.Status==EERPlayabilityStatus::NotEvaluated)TestFalse(*(Case+TEXT(": unevaluated metric says why: ")+M.Code),M.Reason.IsEmpty());
            const auto* X=Report.Find(TEXT("BASE_EXIT_PATHS"));
            if(Concrete)
            {
                // Ladders onto cargo tops are dead ends, far from any recorded warehouse route.
                const auto* Reach=Report.Find(TEXT("CONNECTED_ZONES"));
                if(T->Archetype==EERMapArchetype::Rooms)TestEqual(*(Case+TEXT(": every room connects")),Reach->Value(TEXT("isolated_nodes")),0.);
                for(const auto& V:Reach->Violations)for(int32 Node:V.Nodes)TestTrue(*(Case+TEXT(": only cargo ladders stay apart")),Report.Graph.Nodes[Node].Source.StartsWith(TEXT("ladder")));
                TestNotEqual(*(Case+TEXT(": exits evaluated")),X->Status,EERPlayabilityStatus::NotEvaluated);
                TestTrue(*(Case+TEXT(": crossing distance measured")),Report.Find(TEXT("CONNECTED_ZONES"))->Value(TEXT("base_to_base_m"))>0);
                TestEqual(*(Case+TEXT(": half-turn exits match")),X->Value(TEXT("alpha_exits")),X->Value(TEXT("omega_exits")));
                TestEqual(*(Case+TEXT(": half-turn paths match")),X->Value(TEXT("alpha_independent_paths")),X->Value(TEXT("omega_independent_paths")));
            }
            Csv+=CsvRow(Report);
            UE_LOG(LogTemp,Display,TEXT("ER_MAP_PLAYABILITY %s"),*ERPlayability::Summary(Report));
            if(Keep)ERPlayability::Save(Report,OutputDirectory());
        }
    }
    return TestTrue(TEXT("batch summary saved"),FFileHelper::SaveStringToFile(Csv,*(OutputDirectory()/TEXT("layout-summary.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

// Physical stage: real capsules and real traces on generated collision.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERPlayabilityPhysicsTest,"EclipseRecoil.Maps.Playability.Physics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERPlayabilityPhysicsTest::RunTest(const FString&)
{
    const auto Teams=IntegerList(TEXT("ERPlayabilityTeams="),TEXT("2"));
    const auto Seeds=IntegerList(TEXT("ERPlayabilitySeeds="),TEXT("42,73,7919"));
    const bool Keep=FParse::Param(FCommandLine::Get(),TEXT("ERPlayabilityReports"));
    FString Csv=CsvHeader;
    for(const TCHAR* Id:PlayabilityTemplates)
    {
        auto* T=LoadTemplate(Id);if(!TestNotNull(Id,T))return false;
        for(int32 Team:Teams)
        {
            UWorld::InitializationValues V;V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
            auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);if(!World)return false;
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;Map->LayoutConfig.ExpectedTeamSize=Team;Map->bCeilings=false;
            const auto P=ERPlayability::PolicyFor(T->Archetype,Team);
            for(int32 Seed:Seeds)
            {
                const FString Case=FString::Printf(TEXT("%s %dv%d seed%d"),Id,Team,Team,Seed);
                Map->LayoutConfig.Seed=Seed;
                FERProceduralLayout Plan;FERBlockRoomConfig B=Map->Config;
                if(!ERProcedural::Build(Map->LayoutConfig,B,Plan,Map->CellSettings) || !Map->Regenerate()){AddError(Case+TEXT(" did not generate"));continue;}
                // Rebuilt from the same seed: the map itself dropped its panels.
                auto Report=ERPlayability::Analyze(Plan,Map->LayoutConfig,P);
                const double LayoutVisible=Report.Find(TEXT("INITIAL_SPAWN_LOS"))->Value(TEXT("visible_pairs"));
                ERPlayability::ConfirmInWorld(Report,World,Map->GetActorTransform(),Map->GetLayout(),P);
                const auto* Sight=Report.Find(TEXT("INITIAL_SPAWN_LOS"));
                TestEqual(*(Case+TEXT(": physical stage replaced the sight metric")),Sight->Stage,EERPlayabilityStage::Physical);
                // Layout solids are real collision: they can miss cover, never invent it.
                TestTrue(*(Case+TEXT(": layout sight is not stricter than collision")),Sight->Value(TEXT("visible_pairs"))<=LayoutVisible);
                TestEqual(*(Case+TEXT(": no walking edge is left unconfirmed")),Report.Find(TEXT("CONNECTED_ZONES"))->Value(TEXT("edges_unconfirmed")),0.);
                Csv+=CsvRow(Report);
                UE_LOG(LogTemp,Display,TEXT("ER_MAP_PLAYABILITY %s"),*ERPlayability::Summary(Report));
                UE_LOG(LogTemp,Display,TEXT("ER_MAP_PLAYABILITY_DIGEST template=%s team=%d seed=%d digest=%u"),Id,Team,Seed,Map->GetLayoutDigest());
                if(Keep)ERPlayability::Save(Report,OutputDirectory());
            }
            World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
        }
    }
    return TestTrue(TEXT("physical summary saved"),FFileHelper::SaveStringToFile(Csv,*(OutputDirectory()/TEXT("physical-summary.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}
#endif
