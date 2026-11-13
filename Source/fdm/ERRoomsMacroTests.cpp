#include "ERRoomsMacro.h"
#include "ERMapPlayability.h"
#include "ERProceduralMap.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
UERMapTemplate* RoomsRecipe(){return LoadObject<UERMapTemplate>(nullptr,TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_Rooms"));}
TArray<int32> MacroSeeds()
{
    FString Text=TEXT("42,73,7919,3,6,8,14");FParse::Value(FCommandLine::Get(),TEXT("ERPlayabilitySeeds="),Text,false);
    TArray<FString> Words;Text.ParseIntoArray(Words,TEXT(","));
    TArray<int32> Out;for(const auto& W:Words)Out.Add(FCString::Atoi(*W));
    return Out;
}
FString MacroRow(const FERPlayabilityReport& R,const TCHAR* Graph,int32 Variant,float BuildMs)
{
    auto Value=[&](const TCHAR* Code,const TCHAR* Name){const auto* M=R.Find(Code);return M ? M->Value(Name):0.;};
    return FString::Printf(TEXT("%s,%d,%d,%d,%g,%g,%g,%g,%g,%g,%.2f\n"),Graph,R.ExpectedTeamSize,R.Seed,Variant,Value(TEXT("CONNECTED_ZONES"),TEXT("edges")),Value(TEXT("CONNECTED_ZONES"),TEXT("edges_stopped_by_collision")),Value(TEXT("CONNECTED_ZONES"),TEXT("base_to_base_m")),
        Value(TEXT("BASE_EXIT_PATHS"),TEXT("alpha_independent_paths")),Value(TEXT("INITIAL_SPAWN_LOS"),TEXT("visible_pairs")),Value(TEXT("INITIAL_SPAWN_LOS"),TEXT("open_shot_pairs")),BuildMs);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERRoomsMacroLayoutTest,"EclipseRecoil.Destruction.RoomsMacroLayout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERRoomsMacroLayoutTest::RunTest(const FString&)
{
    auto* T=RoomsRecipe();if(!TestNotNull(TEXT("Rooms recipe"),T))return false;
    if(!TestNotNull(TEXT("baked table on the recipe"),T->Rooms.MacroTable.Get()))return false;
    TArray<int32> Seeds;for(int32 I=0;I<512;++I)Seeds.Add(I);Seeds.Append({7919,-17,INT32_MIN,INT32_MAX});
    FERBlockRoomConfig B;TSet<int32> Used;int32 Applied=0,Fallback=0;
    for(int32 Team:{0,2,3,4,8})for(int32 Seed:Seeds)
    {
        FERProceduralConfig Off;Off.TemplateAsset=T;Off.Seed=Seed;Off.ExpectedTeamSize=Team;FERProceduralConfig On=Off;On.bEnablePCGExMacroGraph=true;
        const FString Case=FString::Printf(TEXT("team%d seed%d"),Team,Seed);
        FERProceduralLayout Legacy,L,Again;
        if(!ERProcedural::Build(Off,B,Legacy) || !ERProcedural::Build(On,B,L) || !ERProcedural::Build(On,B,Again)){AddError(Case+TEXT(" did not build"));continue;}
        TestEqual(*(Case+TEXT(": flag off stays legacy")),Legacy.RoomsMacroVariant,int32(INDEX_NONE));
        TestEqual(*(Case+TEXT(": same seed picks the same plan")),L.RoomsMacroVariant,Again.RoomsMacroVariant);
        TestEqual(*(Case+TEXT(": repeatable links")),L.RoomLinks.Num(),Again.RoomLinks.Num());
        if(Team==0)
        {
            // Without team sizing nothing refits the plan: the macro only swaps the spine.
            TestEqual(*(Case+TEXT(": rooms and starts follow the same sampling")),L.Rooms.Num(),Legacy.Rooms.Num());
            TestEqual(*(Case+TEXT(": arena budget is untouched")),L.ArenaSize,Legacy.ArenaSize);
        }
        else
        {
            // Team sizing fits room dimensions to the real floor, corridors included, so a
            // macro plan may settle on other rooms: it must still meet the same area budget.
            const double Target=ERProcedural::TargetAreaM2(Team,EERMapArchetype::Rooms);
            TestTrue(*(Case+TEXT(": macro plan keeps the team area budget")),FMath::Abs(ERProcedural::PlayableAreaM2(L)-Target)<=Target*.1+.01);
            TestTrue(*(Case+TEXT(": complete room graph")),L.Rooms.Num()>=4 && L.Rooms.Num()<=10 && L.Rooms.Num()%2==0);
        }
        if(L.RoomsMacroVariant==INDEX_NONE)
        {
            ++Fallback;
            TestEqual(*(Case+TEXT(": a fallback is exactly legacy")),L.RoomLinks.Num(),Legacy.RoomLinks.Num());
            continue;
        }
        ++Applied;Used.Add(L.RoomsMacroVariant);
        const int32 AlphaNode=0,OmegaNode=1;
        TestFalse(*(Case+TEXT(": the straight spine is gone")),L.RoomLinks.ContainsByPredicate([&](const FERRoomsLink& E){return (E.A==AlphaNode && E.B==OmegaNode) || (E.A==OmegaNode && E.B==AlphaNode);}));
        TestTrue(*(Case+TEXT(": plan is marked")),L.FeatureNames.Contains(TEXT("PCGExMacroGraph")));
        for(const auto& V:L.RoomNodes)TestTrue(*(Case+TEXT(": half-turn paired nodes")),L.RoomNodes.ContainsByPredicate([&](const FERRoomsNode& D){return D.Center.Equals(FVector(-V.Center.X,-V.Center.Y,V.Center.Z),.01) && D.Size.Equals(V.Size);}));
        TArray<int32> Degree;Degree.Init(0,L.RoomNodes.Num());
        for(const auto& E:L.RoomLinks)
        {
            ++Degree[E.A];++Degree[E.B];
            TestTrue(*(Case+TEXT(": walkable slope")),FMath::Abs(E.End.Z-E.Start.Z)<=FVector::Dist2D(E.Start,E.End)*.5f+.01f);
            TestTrue(*(Case+TEXT(": paired corridor")),L.RoomLinks.ContainsByPredicate([&](const FERRoomsLink& D){return D.Start.Equals(FVector(-E.Start.X,-E.Start.Y,E.Start.Z),.01) || D.End.Equals(FVector(-E.Start.X,-E.Start.Y,E.Start.Z),.01);}));
        }
        for(int32 D:Degree)TestTrue(*(Case+TEXT(": no dead end")),D>=2);
        const auto Report=ERPlayability::Analyze(L,On,ERPlayability::PolicyFor(T->Archetype,Team));
        // A secondary spawn pocket joined inside a room stays unconfirmed until the physical stage:
        // that is a warning, not a break in the graph.
        const auto* Reach=Report.Find(TEXT("CONNECTED_ZONES"));
        TestTrue(*(Case+TEXT(": every room connects")),Reach->Status!=EERPlayabilityStatus::Fail && Reach->Violations.IsEmpty() && Reach->Value(TEXT("isolated_nodes"))==0);
        TestTrue(*(Case+TEXT(": at least two independent paths")),Report.Find(TEXT("BASE_EXIT_PATHS"))->Value(TEXT("alpha_independent_paths"))>=2);
        TestEqual(*(Case+TEXT(": no common choke")),Report.Find(TEXT("BASE_EXIT_PATHS"))->Value(TEXT("alpha_chokes")),0.);
    }
    AddInfo(FString::Printf(TEXT("applied=%d fallback=%d plans=%d"),Applied,Fallback,Used.Num()));
    UE_LOG(LogTemp,Display,TEXT("ER_ROOMS_MACRO_LAYOUT applied=%d fallback=%d plans=%d"),Applied,Fallback,Used.Num());
    TestTrue(TEXT("macro plans are really used"),Applied>0);
    TestTrue(TEXT("seeds reach several baked plans"),Used.Num()>=2);
    // Broken tables can never break a match.
    TStrongObjectPtr<UERMapTemplate> Copy(DuplicateObject<UERMapTemplate>(T,GetTransientPackage()));
    auto* Bad=NewObject<UERRoomsMacroTable>();Copy->Rooms.MacroTable=Bad;
    for(int32 N:{2,3,4})
    {
        auto& Unknown=Bad->Variants.AddDefaulted_GetRef();Unknown.LaneRooms=N;Unknown.Links={{ERRoomsMacro::AlphaBase,ERRoomsMacro::OmegaBase}};
        auto& Empty=Bad->Variants.AddDefaulted_GetRef();Empty.LaneRooms=N;
        auto& Stub=Bad->Variants.AddDefaulted_GetRef();Stub.LaneRooms=N;Stub.Links={{ERRoomsMacro::AlphaBase,ERRoomsMacro::CentreLeft},{ERRoomsMacro::OmegaBase,ERRoomsMacro::CentreRight}};
    }
    for(int32 Seed=0;Seed<64;++Seed)
    {
        FERProceduralConfig On;On.TemplateAsset=Copy.Get();On.Seed=Seed;On.bEnablePCGExMacroGraph=true;FERProceduralConfig Off=On;Off.bEnablePCGExMacroGraph=false;
        FERProceduralLayout L,Legacy;
        if(TestTrue(TEXT("invalid plans still build"),ERProcedural::Build(On,B,L) && ERProcedural::Build(Off,B,Legacy)))
        {
            TestEqual(TEXT("invalid plans fall back"),L.RoomsMacroVariant,int32(INDEX_NONE));
            TestEqual(TEXT("fallback restores every link"),L.RoomLinks.Num(),Legacy.RoomLinks.Num());
            TestEqual(TEXT("fallback restores every node"),L.RoomNodes.Num(),Legacy.RoomNodes.Num());
        }
    }
    Copy->Rooms.MacroTable=nullptr;
    FERProceduralConfig Missing;Missing.TemplateAsset=Copy.Get();Missing.bEnablePCGExMacroGraph=true;FERProceduralLayout L;
    TestTrue(TEXT("a missing table falls back"),ERProcedural::Build(Missing,B,L) && L.RoomsMacroVariant==INDEX_NONE);
    return !HasAnyErrors();
}

// Same analysis on both generators: real collision, same seeds.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERRoomsMacroPhysicsTest,"EclipseRecoil.Destruction.RoomsMacroPhysics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERRoomsMacroPhysicsTest::RunTest(const FString&)
{
    auto* T=RoomsRecipe();if(!TestNotNull(TEXT("Rooms recipe"),T))return false;
    FString Csv=TEXT("graph,team,seed,variant,edges,edges_stopped,base_to_base_m,independent_paths,visible_pairs,open_shot_pairs,build_ms\n");
    FString TeamText=TEXT("2,4,8");FParse::Value(FCommandLine::Get(),TEXT("ERPlayabilityTeams="),TeamText,false);
    TArray<FString> TeamWords;TeamText.ParseIntoArray(TeamWords,TEXT(","));
    for(const auto& TeamWord:TeamWords)
    {
        const int32 Team=FCString::Atoi(*TeamWord);
        UWorld::InitializationValues V;V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
        auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);if(!World)return false;
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;Map->LayoutConfig.ExpectedTeamSize=Team;Map->bCeilings=false;
        const auto P=ERPlayability::PolicyFor(T->Archetype,Team);
        for(int32 Seed:MacroSeeds())for(int32 Mode=0;Mode<2;++Mode)
        {
            const FString Case=FString::Printf(TEXT("%s %dv%d seed%d"),Mode ? TEXT("macro"):TEXT("legacy"),Team,Team,Seed);
            Map->LayoutConfig.Seed=Seed;Map->LayoutConfig.bEnablePCGExMacroGraph=Mode!=0;
            FERProceduralLayout Plan;const double Start=FPlatformTime::Seconds();
            const bool Built=ERProcedural::Build(Map->LayoutConfig,Map->Config,Plan,Map->CellSettings);
            const float BuildMs=(FPlatformTime::Seconds()-Start)*1000;
            if(!Built || !Map->Regenerate()){AddError(Case+TEXT(" did not generate"));continue;}
            TestTrue(*(Case+TEXT(": calibrated cells")),Map->ValidateCells());
            const auto& Live=Map->GetLayout();
            FCollisionQueryParams Q(SCENE_QUERY_STAT(ERRoomsMacroPhysics),false);FHitResult Hit;const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);
            for(const auto* Starts:{&Live.AlphaSpawns,&Live.OmegaSpawns})for(const auto& S:*Starts)
                TestFalse(*(Case+TEXT(": clear standing spawn")),World->OverlapBlockingTestByChannel(S.GetLocation(),FQuat::Identity,ECC_Pawn,Capsule,Q));
            auto Report=ERPlayability::Analyze(Plan,Map->LayoutConfig,P);
            ERPlayability::ConfirmInWorld(Report,World,Map->GetActorTransform(),Live,P);
            const auto* Reach=Report.Find(TEXT("CONNECTED_ZONES"));
            TestEqual(*(Case+TEXT(": no impossible corridor")),Reach->Value(TEXT("edges_stopped_by_collision")),0.);
            TestEqual(*(Case+TEXT(": no broken map")),Reach->Value(TEXT("components")),1.);
            Csv+=MacroRow(Report,Mode ? TEXT("macro"):TEXT("legacy"),Live.RoomsMacroVariant,BuildMs);
            UE_LOG(LogTemp,Display,TEXT("ER_ROOMS_MACRO_DIGEST mode=%d team=%d seed=%d digest=%u"),Mode,Team,Seed,Map->GetTemplateGeometryDigest());
        }
        World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("Testing/MapPlayability");IFileManager::Get().MakeDirectory(*Directory,true);
    return TestTrue(TEXT("comparison saved"),FFileHelper::SaveStringToFile(Csv,*(Directory/TEXT("rooms-macro-comparison.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}
#endif
