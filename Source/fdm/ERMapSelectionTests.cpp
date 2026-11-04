#include "ERMapSelection.h"
#include "ERAlleyDestruction.h"
#include "ERBlockDebris.h"
#include "ERIndustrialMapTestUtils.h"
#include "ERProceduralMap.h"
#include "ERSpawnRisk.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
constexpr int32 MaxStressThreats=8;
const TCHAR* const SelectionTemplates[]={TEXT("CentralBuilding"),TEXT("MiddleEasternAlleys"),TEXT("FireTraining"),TEXT("Rooms"),TEXT("Village"),TEXT("Town")};
UERMapTemplate* SelectionRecipe(const TCHAR* Id){return LoadObject<UERMapTemplate>(nullptr,*(FString(TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_"))+Id));}
TArray<int32> SelectionList(const TCHAR* Flag,const TCHAR* Default)
{
    FString Text=Default;FParse::Value(FCommandLine::Get(),Flag,Text,false);
    TArray<FString> Words;Text.ParseIntoArray(Words,TEXT(","));
    TArray<int32> Out;for(const auto& W:Words)Out.Add(FCString::Atoi(*W));
    return Out;
}
FString SelectionDirectory(){return FPaths::ProjectSavedDir()/TEXT("Testing/MapPlayability");}
uint32 PlanPrint(const FERProceduralLayout& L)
{
    uint32 H=GetTypeHash(L.Panels.Num());H=HashCombine(H,GetTypeHash(L.FixedPieces.Num()));H=HashCombine(H,GetTypeHash(L.RoomLinks.Num()));
    auto Mix=[&](FVector P){for(int32 I=0;I<3;++I)H=HashCombine(H,GetTypeHash(FMath::RoundToInt32(P[I]*10)));};
    Mix(L.ArenaSize);for(const auto& S:L.AlphaSpawns)Mix(S.GetLocation());for(const auto& P:L.Panels)Mix(P.Transform.GetLocation());for(const auto& P:L.FixedPieces)Mix(P.Transform.GetLocation());
    return H;
}
UWorld* SelectionWorld()
{
    UWorld::InitializationValues V;V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);
    if(World)GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    return World;
}
void CloseSelectionWorld(UWorld* World){World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}
FERPlayabilityMetric Metric(const TCHAR* Code,EERPlayabilityStatus Status,TArray<TPair<FString,double>> Values)
{
    FERPlayabilityMetric M;M.Code=Code;M.Status=Status;M.Values=MoveTemp(Values);return M;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERMapSelectionDecideTest,"EclipseRecoil.Maps.Selection.Decide",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERMapSelectionDecideTest::RunTest(const FString&)
{
    auto Candidate=[](int32 Index,bool Built,bool Clean,int32 Preference)
    {
        FERMapCandidate C;C.Index=Index;C.bBuilt=Built;C.Preference=Preference;
        if(!Clean)C.Rejected.Add(TEXT("paths_1"));
        return C;
    };
    bool Fallback=false;
    TestEqual(TEXT("a clean candidate beats a preferred broken one"),ERMapSelection::Decide({Candidate(0,true,false,3),Candidate(1,true,true,0)},1,Fallback),1);
    TestFalse(TEXT("no fallback with a clean candidate"),Fallback);
    TestEqual(TEXT("higher preference wins among clean ones"),ERMapSelection::Decide({Candidate(0,true,true,0),Candidate(1,true,true,2),Candidate(2,true,true,1)},1,Fallback),1);
    TestEqual(TEXT("nothing clean falls back to the plain seed"),ERMapSelection::Decide({Candidate(0,true,false,0),Candidate(1,true,false,3)},1,Fallback),0);
    TestTrue(TEXT("fallback is explicit"),Fallback);
    TestEqual(TEXT("an unbuilt plain seed falls back to the first layout"),ERMapSelection::Decide({Candidate(0,false,false,0),Candidate(1,true,false,0)},1,Fallback),1);
    TestEqual(TEXT("nothing built fails closed"),ERMapSelection::Decide({Candidate(0,false,false,0)},1,Fallback),INDEX_NONE);
    TestEqual(TEXT("the plain seed keeps a tie"),ERMapSelection::Decide({Candidate(0,true,true,1),Candidate(1,true,true,1)},1,Fallback),0);
    TSet<int32> Picked;
    for(int32 Seed=0;Seed<32;++Seed)Picked.Add(ERMapSelection::Decide({Candidate(0,true,false,1),Candidate(1,true,true,1),Candidate(2,true,true,1),Candidate(3,true,true,1)},Seed,Fallback));
    TestEqual(TEXT("other ties vary with the seed"),Picked.Num(),3);

    {
        TArray<FERMapCandidate> Tied{Candidate(0,true,true,1),Candidate(1,true,true,1),Candidate(2,true,true,0),Candidate(3,true,false,1)};
        ERMapSelection::PreferBreachSafe(Tied,[](int32 Index){return Index==0 ? 4:0;});
        TestEqual(TEXT("a breach that opens a line loses the tie"),Tied[0].Preference,1);
        TestEqual(TEXT("a breach that opens nothing wins it"),Tied[1].Preference,2);
        TestEqual(TEXT("only the best candidates pay for the reading"),Tied[2].BreachPairs,int32(INDEX_NONE));
        TestEqual(TEXT("a broken candidate is never read"),Tied[3].BreachPairs,int32(INDEX_NONE));
        TestEqual(TEXT("the safer plan moves the map"),ERMapSelection::Decide(Tied,1,Fallback),1);
    }
    FERPlayabilityReport R;R.Graph.Coverage=EERGraphCoverage::Exact;
    R.Metrics.Add(Metric(TEXT("CONNECTED_ZONES"),EERPlayabilityStatus::Fail,{}));
    R.Metrics.Add(Metric(TEXT("BASE_EXIT_PATHS"),EERPlayabilityStatus::Warn,{{TEXT("alpha_independent_paths"),2},{TEXT("omega_independent_paths"),1}}));
    R.Metrics.Add(Metric(TEXT("INITIAL_SPAWN_LOS"),EERPlayabilityStatus::Warn,{{TEXT("visible_pairs"),3}}));
    R.Metrics.Add(Metric(TEXT("ROUTE_DIVERSITY"),EERPlayabilityStatus::Pass,{{TEXT("lateral_ratio"),1.5}}));
    FERMapSelectionProfile P;P.bRequireWholeGraph=true;P.MinPaths=2;P.bForbidSpawnSight=true;P.LateralRatio=FFloatInterval(1,2);
    FERMapCandidate Unset;
    { FERMapSelectionProfile Empty;ERMapSelection::Judge(Unset,R,Empty);TestEqual(TEXT("unset ranges score nothing"),Unset.Preference,0); }
    FERMapCandidate C;ERMapSelection::Judge(C,R,P);
    TestEqual(TEXT("every broken rule is named"),C.Rejected.Num(),3);
    TestEqual(TEXT("the weaker team sets the paths"),C.Paths,1);
    TestEqual(TEXT("ranges still count, without rescuing the candidate"),C.Preference,1);
    R.Graph.Coverage=EERGraphCoverage::Routes;R.Find(TEXT("INITIAL_SPAWN_LOS"))->Status=EERPlayabilityStatus::NotEvaluated;
    ERMapSelection::Judge(C,R,P);
    TestEqual(TEXT("undecided sight with exposed pairs still rejects the candidate"),C.Rejected.Num(),2);
    FERMapCandidate Exposed;Exposed.bBuilt=true;Exposed.Rejected.Add(TEXT("spawn_sight_1"));
    TestEqual(TEXT("fallback cannot accept exposed spawns"),ERMapSelection::Decide({Exposed},42,Fallback),INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERMapRouteMetricsTest,"EclipseRecoil.Maps.Selection.Routes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERMapRouteMetricsTest::RunTest(const FString&)
{
    FERTacticalGraph G;
    for(const FVector& P:{FVector(0,0,0),FVector(1000,0,0),FVector(2000,0,0),FVector(0,1000,0),FVector(2000,1000,0)})
    {auto& N=G.Nodes.AddDefaulted_GetRef();N.Position=P;}
    G.AddEdge(0,1,EERTacticalMove::Walk,TEXT("direct"),true);G.AddEdge(1,2,EERTacticalMove::Walk,TEXT("direct"),true);
    TestEqual(TEXT("a single route has no lateral"),ERPlayability::LateralPathLength(G,{0},{2}),-1.f);
    G.AddEdge(0,3,EERTacticalMove::Walk,TEXT("lateral"),true);G.AddEdge(3,4,EERTacticalMove::Walk,TEXT("lateral"),true);G.AddEdge(4,2,EERTacticalMove::Walk,TEXT("lateral"),true);
    TestEqual(TEXT("direct stays the cheapest"),ERPlayability::PathLength(G,{0},{2}),2000.f);
    TestEqual(TEXT("lateral avoids every direct node"),ERPlayability::LateralPathLength(G,{0},{2}),4000.f);
    G.Edges[2].bBlocked=true;
    TestEqual(TEXT("blocked edges do not count"),ERPlayability::LateralPathLength(G,{0},{2}),-1.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERMapSelectionBatchTest,"EclipseRecoil.Maps.Selection.Batch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERMapSelectionBatchTest::RunTest(const FString&)
{
    const auto Teams=SelectionList(TEXT("ERPlayabilityTeams="),TEXT("2,4,8"));
    const auto Seeds=SelectionList(TEXT("ERPlayabilitySeeds="),TEXT("42,73,7919"));
    int32 Forced=0;FParse::Value(FCommandLine::Get(),TEXT("ERSelectionCandidates="),Forced);
    FString Rows=TEXT("archetype,team,seed,index,built,chosen,fallback,rejected,preference,paths,visible_pairs,unknown_solids,lateral_ratio,open_share,breach_pairs,build_ms,analysis_ms\n");
    FString Stress=TEXT("archetype,team,seed,scenario,applicable,openings,visible_pairs,visible_pairs_damaged,paths,paths_damaged,exit_share,exit_share_damaged,open_share,open_share_damaged,reason\n");
    FERBlockRoomConfig B;FERCellSettings Cells;
    for(const TCHAR* Id:SelectionTemplates)
    {
        auto* T=SelectionRecipe(Id);if(!TestNotNull(Id,T))return false;
        for(int32 Team:Teams)for(int32 Seed:Seeds)
        {
            const FString Case=FString::Printf(TEXT("%s %dv%d seed%d"),Id,Team,Team,Seed);
            FERProceduralConfig C;C.TemplateAsset=T;C.ExpectedTeamSize=Team;C.Seed=Seed;
            FERProceduralLayout Plain;
            if(!TestTrue(*(Case+TEXT(": plain seed builds")),ERProcedural::Build(C,B,Plain,Cells)))continue;
            FERProceduralConfig Zero=C;Zero.MapCandidate=0;FERProceduralLayout Same;ERProcedural::Build(Zero,B,Same,Cells);
            TestEqual(*(Case+TEXT(": candidate zero is the plain seed")),PlanPrint(Same),PlanPrint(Plain));
            auto Profile=ERMapSelection::ProfileFor(T->Archetype,Team);
            if(Forced>0)Profile.Candidates=Forced;
            const auto First=ERMapSelection::Select(C,B,Cells,Profile),Again=ERMapSelection::Select(C,B,Cells,Profile);
            TestEqual(*(Case+TEXT(": selection repeats")),First.Chosen,Again.Chosen);
            TestFalse(*(Case+TEXT(": budget not reached")),First.bBudgetHit);
            if(Profile.Candidates>1)TestEqual(*(Case+TEXT(": plain seed examined first")),First.Candidates[0].Index,0);
            for(const auto& A:First.Candidates)
                Rows+=FString::Printf(TEXT("%s,%d,%d,%d,%d,%d,%d,%s,%d,%d,%d,%d,%g,%g,%d,%.2f,%.2f\n"),ERPlayability::ArchetypeName(T->Archetype),Team,Seed,A.Index,A.bBuilt,A.Index==First.Chosen,First.bFallback,*FString::Join(A.Rejected,TEXT("|")),A.Preference,A.Paths,A.VisiblePairs,A.UnknownSolids,A.LateralRatio,A.OpenShare,A.BreachPairs,A.BuildMs,A.AnalysisMs);
            FERProceduralConfig Chosen=C;Chosen.MapCandidate=First.Chosen;FERProceduralLayout Plan,Twin;
            if(!TestTrue(*(Case+TEXT(": chosen plan builds")),ERProcedural::Build(Chosen,B,Plan,Cells)))continue;
            ERProcedural::Build(Chosen,B,Twin,Cells);
            TestEqual(*(Case+TEXT(": chosen plan repeats")),PlanPrint(Twin),PlanPrint(Plan));
            const auto Policy=ERPlayability::PolicyFor(T->Archetype,Team);
            const auto Report=ERPlayability::Analyze(Plan,Chosen,Policy);
            for(const auto& S:ERPlayability::Stress(Plan,Report,Policy))
                Stress+=FString::Printf(TEXT("%s,%d,%d,%s,%d,%d,%d,%d,%d,%d,%g,%g,%g,%g,%s\n"),ERPlayability::ArchetypeName(T->Archetype),Team,Seed,*S.Scenario,S.bApplicable,S.Openings.Num(),S.VisiblePairs,S.VisiblePairsDamaged,S.Paths,S.PathsDamaged,S.ExitShare,S.ExitShareDamaged,S.OpenShare,S.OpenShareDamaged,*S.Reason);
        }
    }
    const auto Flags=FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM;
    TestTrue(TEXT("candidates saved"),FFileHelper::SaveStringToFile(Rows,*(SelectionDirectory()/TEXT("selection-candidates.csv")),Flags));
    return TestTrue(TEXT("stress saved"),FFileHelper::SaveStringToFile(Stress,*(SelectionDirectory()/TEXT("stress-summary.csv")),Flags));
}

// Same seeds with and without selection, in real collision.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERMapSelectionPhysicsTest,"EclipseRecoil.Maps.Selection.Physics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERMapSelectionPhysicsTest::RunTest(const FString&)
{
    const auto Teams=SelectionList(TEXT("ERPlayabilityTeams="),TEXT("2,4,8"));
    const auto Seeds=SelectionList(TEXT("ERPlayabilitySeeds="),TEXT("42,73,7919"));
    FString Rows=TEXT("archetype,team,seed,selected,candidate,visible_pairs,open_shot_pairs,paths,base_to_base_m,edges_blocked,exit_share,open_share,regenerate_ms\n");
    for(const TCHAR* Id:SelectionTemplates)
    {
        auto* T=SelectionRecipe(Id);if(!TestNotNull(Id,T))return false;
        for(int32 Team:Teams)
        {
            auto* World=SelectionWorld();if(!World)return false;
            auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;Map->LayoutConfig.ExpectedTeamSize=Team;Map->bCeilings=false;
            const auto P=ERPlayability::PolicyFor(T->Archetype,Team);
            for(int32 Seed:Seeds)for(const bool Select:{false,true})
            {
                const FString Case=FString::Printf(TEXT("%s %dv%d seed%d select%d"),Id,Team,Team,Seed,Select);
                Map->LayoutConfig.Seed=Seed;Map->bSelectMapCandidates=Select;
                const double Clock=FPlatformTime::Seconds();
                if(!Map->Regenerate()){AddError(Case+TEXT(" did not generate"));continue;}
                const double RegenerateMs=(FPlatformTime::Seconds()-Clock)*1000;
                FERProceduralLayout Plan;FERBlockRoomConfig B=Map->Config;
                if(!ERProcedural::Build(Map->LayoutConfig,B,Plan,Map->CellSettings)){AddError(Case+TEXT(" did not rebuild"));continue;}
                auto Report=ERPlayability::Analyze(Plan,Map->LayoutConfig,P);
                ERPlayability::ConfirmInWorld(Report,World,Map->GetActorTransform(),Map->GetLayout(),P);
                auto Value=[&](const TCHAR* Code,const TCHAR* Name){const auto* M=Report.Find(Code);return M ? M->Value(Name):0.;};
                Rows+=FString::Printf(TEXT("%s,%d,%d,%d,%d,%g,%g,%g,%g,%g,%g,%g,%.1f\n"),ERPlayability::ArchetypeName(T->Archetype),Team,Seed,Select,Map->LayoutConfig.MapCandidate,Value(TEXT("INITIAL_SPAWN_LOS"),TEXT("visible_pairs")),Value(TEXT("INITIAL_SPAWN_LOS"),TEXT("open_shot_pairs")),
                    FMath::Min(Value(TEXT("BASE_EXIT_PATHS"),TEXT("alpha_independent_paths")),Value(TEXT("BASE_EXIT_PATHS"),TEXT("omega_independent_paths"))),Value(TEXT("CONNECTED_ZONES"),TEXT("base_to_base_m")),Value(TEXT("CONNECTED_ZONES"),TEXT("edges_blocked")),Value(TEXT("DOMINANT_VANTAGE"),TEXT("worst_exit_share")),Value(TEXT("ROUTE_EXPOSURE"),TEXT("open_share")),RegenerateMs);
            }
            CloseSelectionWorld(World);
        }
    }
    return TestTrue(TEXT("physical comparison saved"),FFileHelper::SaveStringToFile(Rows,*(SelectionDirectory()/TEXT("selection-physical.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

// The rules that keep a scenario honest, on a wall the outcome can be read from.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERMapStressRulesTest,"EclipseRecoil.Maps.Stress.Rules",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERMapStressRulesTest::RunTest(const FString&)
{
    FERProceduralLayout L;L.AlphaSpawns.Emplace(FVector(0,-1000,100));L.OmegaSpawns.Emplace(FVector(0,1000,100));
    auto& Wall=L.Panels.AddDefaulted_GetRef();Wall.Material=EERBlockMaterial::Brick;Wall.Size={4000,20,300};Wall.Transform=FTransform(FVector(-2000,-10,0));
    FERProceduralConfig C;FERPlayabilityPolicy P;
    auto Report=ERPlayability::Analyze(L,C,P);
    FERTacticalGraph G;G.Coverage=EERGraphCoverage::Exact;
    for(const FVector& At:{FVector(0,-1000,100),FVector(0,1000,100)}){auto& N=G.Nodes.AddDefaulted_GetRef();N.Position=At;}
    G.Nodes[0].bBase=true;G.Nodes[0].Team=0;G.Nodes[1].bBase=true;G.Nodes[1].Team=1;
    G.AddEdge(0,1,EERTacticalMove::Walk,TEXT("test"),true);
    Report.Graph=G;
    auto Breach=[&]()->TOptional<FERStressResult>{for(const auto& S:ERPlayability::Stress(L,Report,P))if(S.Scenario==TEXT("central_breach"))return S;return {};};
    {
        const auto First=ERPlayability::Stress(L,Report,P);
        const auto* S=First.FindByPredicate([](const FERStressResult& R){return R.Scenario==TEXT("central_breach");});
        if(!TestNotNull(TEXT("the full policy runs the breach"),S))return false;
        TestEqual(TEXT("every scenario reports"),First.Num(),4);
        TestTrue(TEXT("a brick wall can be breached"),S->bApplicable);
        TestEqual(TEXT("the wall hid the pair"),S->VisiblePairs,0);
        TestEqual(TEXT("the breach opens the line"),S->VisiblePairsDamaged,1);
        TestEqual(TEXT("nothing is damaged in the layout itself"),L.Panels[0].Holes.Num(),0);
        Report.Stress=First;const FString Text=ERPlayability::ToJson(Report,false);
        Report.Stress=ERPlayability::Stress(L,Report,P);
        TestEqual(TEXT("the same plan gives the same bytes"),ERPlayability::ToJson(Report,false),Text);
        TestTrue(TEXT("the report carries the scenario"),Text.Contains(TEXT("central_breach")));
    }
    L.Panels[0].bIndestructible=true;
    TestFalse(TEXT("an indestructible wall is never opened"),Breach()->bApplicable);
    L.Panels[0].bIndestructible=false;
    P.Stress=EERStressScenario::CoverRemoved;
    TestFalse(TEXT("a policy leaves out what cannot break there"),Breach().IsSet());
    auto Cover=[&](FVector Size){L.Panels[0].Size=Size;L.Panels[0].Transform=FTransform(FVector(-Size.X*.5f,-10,0));return ERPlayability::Stress(L,Report,P)[0].bApplicable;};
    TestTrue(TEXT("chest-high cover can be removed"),Cover({400,20,110}));
    TestFalse(TEXT("a joist is not cover"),Cover({400,10,20}));
    for(const auto& Item:{TPair<EERMapArchetype,int32>(EERMapArchetype::Rooms,2),TPair<EERMapArchetype,int32>(EERMapArchetype::CentralBuilding,4),TPair<EERMapArchetype,int32>(EERMapArchetype::Village,3)})
    {
        P=ERPlayability::PolicyFor(Item.Key,4);
        TestEqual(TEXT("each archetype has its own set"),ERPlayability::Stress(L,Report,P).Num(),Item.Value);
    }
    return true;
}

// Support is only read after damage, so anything that floats on a fresh map falls at the first shot.
// Village must have none. The others report what they have.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERMapFreshSupportTest,"EclipseRecoil.Maps.Stress.FreshSupport",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERMapFreshSupportTest::RunTest(const FString&)
{
    const auto Teams=SelectionList(TEXT("ERPlayabilityTeams="),TEXT("2,4,8"));
    const auto Seeds=SelectionList(TEXT("ERPlayabilitySeeds="),TEXT("42,73,7919"));
    FString Rows=TEXT("archetype,team,seed,panel,cells,material,skin,anchor_bottom,anchored,solid_skin,indestructible,size_x,size_y,size_z,z\n");
    for(const TCHAR* Id:SelectionTemplates)
    {
        auto* T=SelectionRecipe(Id);if(!TestNotNull(Id,T))return false;
        auto* World=SelectionWorld();if(!World)return false;
        auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;
        for(int32 Team:Teams)for(int32 Seed:Seeds)
        {
            Map->LayoutConfig.ExpectedTeamSize=Team;Map->LayoutConfig.Seed=Seed;
            if(!Map->Regenerate()){AddError(FString::Printf(TEXT("%s %dv%d seed%d did not generate"),Id,Team,Team,Seed));continue;}
            FERCellSupport Support=Map->GetSupport();TArray<TArray<FERCellId>> Loose;
            Support.Evaluate(Map->GetCellWorld(),Loose);
            TMap<int32,int32> Cells;for(const auto& Component:Loose)for(const auto& Cell:Component)Cells.FindOrAdd(Cell.Panel)+=1;
            int32 Total=0;for(const auto& Item:Cells)Total+=Item.Value;
            UE_LOG(LogTemp,Display,TEXT("ER_FRESH_SUPPORT %s team=%d seed=%d loose=%d components=%d panels=%d of=%d"),Id,Team,Seed,Total,Loose.Num(),Cells.Num(),Map->GetCellWorld().GetPanels().Num());
            if(T->Archetype==EERMapArchetype::Village)TestEqual(*FString::Printf(TEXT("Village %dv%d seed%d stands on a fresh map"),Team,Team,Seed),Total,0);
            for(const auto& Item:Cells)
            {
                const auto& P=Map->GetCellWorld().GetPanels()[Item.Key];
                Rows+=FString::Printf(TEXT("%s,%d,%d,%d,%d,%d,%s,%d,%d,%d,%d,%g,%g,%g,%g\n"),Id,Team,Seed,Item.Key,Item.Value,int32(P.Material),*P.SkinName.ToString(),P.bAnchorBottom,P.bAnchored,P.bSolidSkin,P.bIndestructible,P.Size.X,P.Size.Y,P.Size.Z,P.Transform.GetLocation().Z);
            }
        }
        CloseSelectionWorld(World);
    }
    return TestTrue(TEXT("fresh support saved"),FFileHelper::SaveStringToFile(Rows,*(SelectionDirectory()/TEXT("fresh-support.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

// Every scenario on the real cell world: same openings, collapse settled, then real collision.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERMapStressPhysicsTest,"EclipseRecoil.Maps.Stress.Physics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERMapStressPhysicsTest::RunTest(const FString&)
{
    const auto Teams=SelectionList(TEXT("ERStressTeams="),TEXT("4"));
    const auto Seeds=SelectionList(TEXT("ERPlayabilitySeeds="),TEXT("42,73,7919"));
    FString Rows=TEXT("archetype,team,seed,selected,candidate,scenario,applicable,openings,cells_removed,cells_detached,abstract_pairs,abstract_pairs_damaged,real_pairs,real_pairs_damaged,real_shots,real_shots_damaged,real_paths,real_paths_damaged,real_exit_share,real_exit_share_damaged,real_open_share,real_open_share_damaged,spawn_seen,spawn_seen_damaged\n");
    int32 Checked=0;
    for(const TCHAR* Id:SelectionTemplates)
    {
        auto* T=SelectionRecipe(Id);if(!TestNotNull(Id,T))return false;
        for(int32 Team:Teams)
        {
            auto* World=SelectionWorld();if(!World)return false;
            auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;Map->LayoutConfig.ExpectedTeamSize=Team;Map->bCeilings=false;
            const auto P=ERPlayability::PolicyFor(T->Archetype,Team);
            FERSpawnRiskTuning Tuning;Tuning.MaxTraces=Tuning.MaxCandidates*MaxStressThreats*2;
            // What the respawn scoring reads right now, both ways across the map.
            auto SpawnSeen=[&]
            {
                const FTransform Frame=Map->GetActorTransform();int32 Seen=0;
                auto Side=[&](const TArray<FTransform>& Starts,const TArray<FTransform>& Enemies)
                {
                    TArray<FERSpawnCandidate> Candidates;TArray<FERSpawnThreat> Threats;
                    for(int32 I=0;I<FMath::Min(Starts.Num(),Tuning.MaxCandidates);++I)Candidates.AddDefaulted_GetRef().Location=Frame.TransformPosition(Starts[I].GetLocation());
                    for(int32 I=0;I<FMath::Min(Enemies.Num(),MaxStressThreats);++I)Threats.Add({Frame.TransformPosition(Enemies[I].GetLocation())+FVector(0,0,Tuning.EyeOffset)});
                    ERSpawnRisk::Score(World,Candidates,Threats,{},{},{},0,Tuning);
                    for(const auto& C:Candidates)Seen+=C.Seen;
                };
                Side(Map->GetLayout().AlphaSpawns,Map->GetLayout().OmegaSpawns);Side(Map->GetLayout().OmegaSpawns,Map->GetLayout().AlphaSpawns);
                return Seen;
            };
            for(int32 Seed:Seeds)for(const bool Select:{false,true})
            {
                const FString Case=FString::Printf(TEXT("%s %dv%d seed%d select%d"),Id,Team,Team,Seed,Select);
                Map->LayoutConfig.Seed=Seed;Map->bSelectMapCandidates=Select;
                if(!Map->Regenerate()){AddError(Case+TEXT(" did not generate"));continue;}
                // Selection that stays on the plain seed repeats the row above it.
                if(Select && Map->LayoutConfig.MapCandidate==0)continue;
                FERProceduralLayout Plan;FERBlockRoomConfig B=Map->Config;
                if(!ERProcedural::Build(Map->LayoutConfig,B,Plan,Map->CellSettings)){AddError(Case+TEXT(" did not rebuild"));continue;}
                const auto Report=ERPlayability::Analyze(Plan,Map->LayoutConfig,P);
                // A fresh map may still be settling: that is not damage.
                ERIndustrialTest::Settle(*Map);
                auto Base=Report;ERPlayability::ConfirmInWorld(Base,World,Map->GetActorTransform(),Map->GetLayout(),P);
                const int32 SeenBefore=SpawnSeen();
                auto Number=[](const FERPlayabilityReport& R,const TCHAR* Code,const TCHAR* Name){const auto* M=R.Find(Code);return M ? M->Value(Name):0.;};
                auto Paths=[&](const FERPlayabilityReport& R){return FMath::Min(Number(R,TEXT("BASE_EXIT_PATHS"),TEXT("alpha_independent_paths")),Number(R,TEXT("BASE_EXIT_PATHS"),TEXT("omega_independent_paths")));};
                bool Dirty=false;
                for(const auto& S:ERPlayability::Stress(Plan,Report,P))
                {
                    const FString Name=Case+TEXT(" ")+S.Scenario;
                    if(!S.bApplicable)
                    {
                        Rows+=FString::Printf(TEXT("%s,%d,%d,%d,%d,%s,0,0,0,0,%d,%d,0,0,0,0,0,0,0,0,0,0,0,0\n"),ERPlayability::ArchetypeName(T->Archetype),Team,Seed,Select,Map->LayoutConfig.MapCandidate,*S.Scenario,S.VisiblePairs,S.VisiblePairsDamaged);
                        continue;
                    }
                    if(Dirty && !Map->Regenerate()){AddError(Name+TEXT(" did not restore"));break;}
                    Dirty=true;ERIndustrialTest::Settle(*Map);
                    const auto DetachedBefore=Map->GetCellWorld().GetDetachedCount();
                    int32 Removed=0;
                    if(S.Scenario==TEXT("walkway_lost"))
                    {
                        const auto Bridges=Map->GetLayout().WallBridges;
                        FCollisionQueryParams Q(SCENE_QUERY_STAT(ERStressBridge),false);
                        TestTrue(*(Name+TEXT(": the crossing carries a player first")),ERIndustrialTest::BridgeSupported(*World,Bridges[0],Q));
                        ERIndustrialTest::CutBridgeSupports(*Map,Bridges[0]);ERIndustrialTest::Settle(*Map);
                        TestFalse(*(Name+TEXT(": the crossing really falls")),ERIndustrialTest::BridgeSupported(*World,Bridges[0],Q));
                        TestTrue(*(Name+TEXT(": the opposite crossing stands")),ERIndustrialTest::BridgeSupported(*World,Bridges[1],Q));
                    }
                    else
                    {
                        for(const auto& Opening:S.Openings)
                        {
                            const auto& Panel=Plan.Panels[Opening.Key];
                            const FBox Local(FVector(Opening.Value.Min.X,-1,Opening.Value.Min.Y),FVector(Opening.Value.Max.X,Panel.Size.Y+1,Opening.Value.Max.Y));
                            Removed+=ERAlleyDestruction::Cut(Map,Opening.Key,Local.TransformBy(Panel.Transform));
                        }
                        TestTrue(*(Name+TEXT(": the same opening removes real cells")),Removed>0);
                        ERIndustrialTest::Settle(*Map);
                    }
                    // Debris falls in play; this world never ticks, so it would hang where it broke.
                    for(TActorIterator<AERBlockDebris> Piece(World);Piece;++Piece)Piece->Destroy();
                    const int64 Detached=Map->GetCellWorld().GetDetachedCount()-DetachedBefore;
                    auto Damaged=Report;ERPlayability::ConfirmInWorld(Damaged,World,Map->GetActorTransform(),Map->GetLayout(),P);
                    const int32 SeenAfter=SpawnSeen();
                    const double RealPairs=Number(Base,TEXT("INITIAL_SPAWN_LOS"),TEXT("visible_pairs")),RealDamaged=Number(Damaged,TEXT("INITIAL_SPAWN_LOS"),TEXT("visible_pairs"));
                    TestTrue(*(Name+TEXT(": cover that is gone never hides a start")),SeenAfter>=SeenBefore);
                    // A collapse can take more than the opening, which the layout reading cannot know.
                    if(Detached==0)TestTrue(*(Name+TEXT(": real sight stays within the abstract state")),RealDamaged<=S.VisiblePairsDamaged);
                    Rows+=FString::Printf(TEXT("%s,%d,%d,%d,%d,%s,1,%d,%d,%d,%d,%d,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%d,%d\n"),ERPlayability::ArchetypeName(T->Archetype),Team,Seed,Select,Map->LayoutConfig.MapCandidate,*S.Scenario,S.Openings.Num(),Removed,int32(Detached),S.VisiblePairs,S.VisiblePairsDamaged,
                        RealPairs,RealDamaged,Number(Base,TEXT("INITIAL_SPAWN_LOS"),TEXT("open_shot_pairs")),Number(Damaged,TEXT("INITIAL_SPAWN_LOS"),TEXT("open_shot_pairs")),Paths(Base),Paths(Damaged),
                        Number(Base,TEXT("DOMINANT_VANTAGE"),TEXT("worst_exit_share")),Number(Damaged,TEXT("DOMINANT_VANTAGE"),TEXT("worst_exit_share")),Number(Base,TEXT("ROUTE_EXPOSURE"),TEXT("open_share")),Number(Damaged,TEXT("ROUTE_EXPOSURE"),TEXT("open_share")),SeenBefore,SeenAfter);
                    ++Checked;
                }
            }
            CloseSelectionWorld(World);
        }
    }
    TestTrue(TEXT("stress comparison saved"),FFileHelper::SaveStringToFile(Rows,*(SelectionDirectory()/TEXT("stress-physical.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return TestTrue(TEXT("at least one real scenario was compared"),Checked>0);
}
#endif
