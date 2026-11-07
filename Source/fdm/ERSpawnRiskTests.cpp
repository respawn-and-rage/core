#include "ERSpawnRisk.h"
#include "ERAlleyDestruction.h"
#include "ERProceduralMap.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FERSpawnCandidate At(FVector Location){FERSpawnCandidate C;C.Location=Location;return C;}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERSpawnRiskChoiceTest,"EclipseRecoil.Maps.SpawnRisk.Choice",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERSpawnRiskChoiceTest::RunTest(const FString&)
{
    const FERSpawnRiskTuning T;using namespace ERSpawnRisk;
    auto First=[](int32){return 0;};
    TestEqual(TEXT("no valid start stays no start"),Pick({},T,First),int32(INDEX_NONE));
    {
        // Comparable scores stay a random choice, so spawns are not predictable.
        TArray<FERSpawnCandidate> Pair{At(FVector(0)),At(FVector(0))};Pair[0].Risk=10;Pair[1].Risk=10+T.Comparable-1;
        TSet<int32> Seen;for(int32 Roll=0;Roll<2;++Roll)Seen.Add(Pick(Pair,T,[&](int32 Options){return Roll%Options;}));
        TestEqual(TEXT("both comparable starts are reachable"),Seen.Num(),2);
        Pair[1].Risk=10+T.Comparable+1;
        for(int32 Roll=0;Roll<4;++Roll)TestEqual(TEXT("a clearly worse start is never drawn"),Pick(Pair,T,[&](int32 Options){return Roll%Options;}),0);
        Pair[0].Risk=T.SightPenalty*2;Pair[1].Risk=T.SightPenalty;
        TestEqual(TEXT("with every start exposed the least bad still spawns"),Pick(Pair,T,First),1);
        Pair[0].Risk=Pair[1].Risk;Pair[0].Unknown=1;
        TestEqual(TEXT("comparable risk prefers a fully checked start"),Pick(Pair,T,First),1);
    }
    {
        // Enemy position changes the answer on the next respawn.
        TArray<FERSpawnCandidate> C{At(FVector(0)),At(FVector(2500,0,0))};
        Score(nullptr,C,{{FVector(500,0,0)}},{},{},{},0,T);
        TestTrue(TEXT("nearer enemy costs more"),C[0].Risk>C[1].Risk);
        Score(nullptr,C,{{FVector(2200,0,0)}},{},{},{},0,T);
        TestTrue(TEXT("a repositioned enemy reverses the order"),C[0].Risk<C[1].Risk);
        Score(nullptr,C,{},{},{},{},0,T);
        TestEqual(TEXT("a dead enemy stops counting"),C[0].Risk+C[1].Risk,0.f);
        Score(nullptr,C,{{FVector(0,0,T.NearDistance+100)}},{},{},{},0,T);
        TestEqual(TEXT("missing collision never proves a distant threat covered"),C[0].Unknown,1);
        TestEqual(TEXT("beyond protection reach adds only unknown sight risk"),C[0].Risk,T.SightPenalty);
    }
    {
        // A live device: only its fan is a hazard.
        TArray<FERSpawnCandidate> C{At(FVector(300,0,0)),At(FVector(-300,0,0)),At(FVector(3000,0,0))};
        FERSpawnHazard H;H.Forward=FVector::ForwardVector;H.Radius=800;H.HalfAngle=55;
        Score(nullptr,C,{},{H},{},{},0,T);
        TestTrue(TEXT("inside the fan"),C[0].bInHazard);TestFalse(TEXT("behind the device"),C[1].bInHazard);TestFalse(TEXT("outside its radius"),C[2].bInHazard);
        TestEqual(TEXT("the hazard penalty applies once"),C[0].Risk,T.HazardPenalty);
    }
    {
        // Recent deaths and repeats fade instead of pinning a start forever.
        TArray<FERSpawnCandidate> C{At(FVector(0)),At(FVector(T.SectorRadius+100,0,0))};
        const TArray<FERSpawnEvent> Deaths{{FVector(0),100}};
        Score(nullptr,C,{},{},Deaths,{},100,T);
        TestEqual(TEXT("a fresh death marks its sector"),C[0].Risk,T.DeathPenalty);TestEqual(TEXT("the next sector is clean"),C[1].Risk,0.f);
        Score(nullptr,C,{},{},Deaths,{},100+T.MemorySeconds*.5f,T);
        TestEqual(TEXT("the mark fades"),C[0].Risk,T.DeathPenalty*.5f);
        Score(nullptr,C,{},{},Deaths,{},100+T.MemorySeconds*2,T);
        TestEqual(TEXT("the mark ends"),C[0].Risk,0.f);
        Score(nullptr,C,{},{},{},{{FVector(0),100}},100,T);
        TestEqual(TEXT("the last start is avoided"),C[0].Risk,T.RepeatPenalty);
        FERSpawnMemory Memory;for(int32 I=0;I<100;++I)Memory.Add(Memory.Deaths[0],FVector(0),I);
        TestEqual(TEXT("memory stays bounded"),Memory.Deaths[0].Num(),32);
        Memory.Reset();TestTrue(TEXT("a new arena forgets"),Memory.Deaths[0].IsEmpty());
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERSpawnRiskCrowdedTest,"EclipseRecoil.Maps.SpawnRisk.Crowded",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERSpawnRiskCrowdedTest::RunTest(const FString&)
{
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if(!TestNotNull(TEXT("collision world"),World))return false;
    auto* Screen=World->SpawnActor<AActor>();auto* Wall=NewObject<UBoxComponent>(Screen);
    Screen->SetRootComponent(Wall);Screen->AddInstanceComponent(Wall);
    Wall->SetBoxExtent(FVector(10,1000,250));Wall->SetCollisionProfileName(TEXT("BlockAll"));Wall->RegisterComponent();
    Wall->SetWorldLocation(FVector(0,0,150));
    FERSpawnRiskTuning Tuning;
    TArray<FERSpawnCandidate> Starts;
    for(int32 Index=0;Index<Tuning.MaxCandidates;++Index)Starts.Add(At(FVector(300+Index*4,0,100)));
    TArray<FERSpawnThreat> Enemies;
    for(int32 Index=0;Index<4;++Index)Enemies.Add({FVector(-200-Index*10,0,164)});
    // The fifth enemy is farther away, on the unprotected side of the screen.
    Enemies.Add({FVector(1000,0,164)});
    const int32 Traces=ERSpawnRisk::Score(World,Starts,Enemies,{},{},{},0,Tuning);
    TestTrue(TEXT("crowded evaluation respects its shared trace budget"),Traces<=Tuning.MaxTraces);
    const int32 Choice=ERSpawnRisk::Pick(Starts,Tuning,[](int32){return 0;});
    TestEqual(TEXT("final verification reaches the distant shooter"),Starts[Choice].Seen,1);
    TestEqual(TEXT("the chosen start has complete sight information"),Starts[Choice].Unknown,0);
    for(const auto& Start:Starts)
        TestTrue(TEXT("unfinished starts are never called covered"),Start.Seen>0 || Start.Unknown>0);
    Enemies.Pop();
    ERSpawnRisk::Score(World,Starts,Enemies,{},{},{},0,Tuning);
    for(const auto& Start:Starts)
    {
        TestEqual(TEXT("all remaining enemies are covered"),Start.Seen,0);
        TestEqual(TEXT("all covered samples fit exactly in the budget"),Start.Unknown,0);
    }
    Tuning.MaxTraces=1;
    ERSpawnRisk::Score(World,Starts,Enemies,{},{},{},0,Tuning);
    TestEqual(TEXT("one blocked sample does not finish a threat"),Starts[0].Unknown,4);
    TestTrue(TEXT("partially sampled threats retain conservative risk"),Starts[0].Risk>=4*Tuning.SightPenalty);
    World->DestroyWorld(false);
    return !HasAnyErrors();
}

// Real generated collision: the same query a respawn makes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERSpawnRiskPhysicsTest,"EclipseRecoil.Maps.SpawnRisk.Physics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERSpawnRiskPhysicsTest::RunTest(const FString&)
{
    auto* T=LoadObject<UERMapTemplate>(nullptr,TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_Rooms"));if(!TestNotNull(TEXT("Rooms recipe"),T))return false;
    UWorld::InitializationValues V;V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);if(!World)return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;Map->LayoutConfig.Seed=42;Map->bCeilings=false;
    if(TestTrue(TEXT("map generated"),Map->Regenerate()))
    {
        const auto& L=Map->GetLayout();
        const auto* Screen=L.RoomScreens.FindByPredicate([](const FERRoomsScreen& S){return S.Room!=INDEX_NONE;});
        if(TestNotNull(TEXT("a timber screen stands in a room"),Screen))
        {
            const float Floor=Screen->Frame.GetLocation().Z-120;
            auto Point=[&](float Y,float Z){FVector P=Screen->Frame.TransformPosition({0,Y,0});P.Z=Floor+Z;return P;};
            const FVector Eye=Point(-450,164);
            FERSpawnRiskTuning Tuning;
            TArray<FERSpawnCandidate> C{At(Point(-200,100)),At(Point(60,100))};
            ERSpawnRisk::Score(World,C,{{Eye}},{},{},{},0,Tuning);
            TestEqual(TEXT("free but visible start is seen"),C[0].Seen,1);
            TestEqual(TEXT("free and covered start is not"),C[1].Seen,0);
            TestEqual(TEXT("the covered start is chosen"),ERSpawnRisk::Pick(C,Tuning,[](int32){return 0;}),1);
            ERSpawnRisk::Score(World,C,{{Point(420,164)}},{},{},{},0,Tuning);
            TestEqual(TEXT("an enemy moving round the cover flips the answer"),ERSpawnRisk::Pick(C,Tuning,[](int32){return 0;}),0);
            // Breach the board on the same line: cover that is gone protects nobody.
            FCollisionQueryParams Query(SCENE_QUERY_STAT(ERSpawnRiskTest),false);
            FHitResult Hit;FERCellId Cell;
            if(TestTrue(TEXT("the board stops the line"),World->LineTraceSingleByChannel(Hit,Eye,C[1].Location+FVector(0,0,Tuning.EyeOffset),ECC_GameTraceChannel1,Query)) && TestTrue(TEXT("the line meets a real cell"),Map->ResolveCell(Hit,Cell)))
                TestTrue(TEXT("a real breach opens in the board"),ERAlleyDestruction::Cut(Map,Cell.Panel,FBox(Hit.ImpactPoint-FVector(60,60,120),Hit.ImpactPoint+FVector(60,60,60)))>0);
            ERSpawnRisk::Score(World,C,{{Eye}},{},{},{},0,Tuning);
            TestEqual(TEXT("destroyed cover exposes the start"),C[1].Seen,1);
            // The budget holds however crowded the match is.
            TArray<FERSpawnCandidate> Many;for(int32 I=0;I<Tuning.MaxCandidates;++I)Many.Add(At(Point(-100-I*20,100)));
            TArray<FERSpawnThreat> Crowd;for(int32 I=0;I<32;++I)Crowd.Add({Point(-300-I*5,164)});
            const int32 Traces=ERSpawnRisk::Score(World,Many,Crowd,{},{},{},0,Tuning);
            TestTrue(TEXT("traces stay inside the budget"),Traces<=Tuning.MaxTraces);
            UE_LOG(LogTemp,Display,TEXT("ER_SPAWN_RISK_BUDGET traces=%d limit=%d"),Traces,Tuning.MaxTraces);
        }
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return !HasAnyErrors();
}
#endif
