#include "ERLobbyRules.h"
#include "ERMapCatalog.h"
#include "ERMatch.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERLobbyQuorum, "EclipseRecoil.Menu.LobbyQuorumAndVotes", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FERLobbyQuorum::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("initial server capacity"), ERServer::DefaultMaxPlayers, 16);
    TestEqual(TEXT("zero capacity becomes one"), ERServer::ValidMaxPlayers(0), 1);
    TestEqual(TEXT("negative capacity becomes one"), ERServer::ValidMaxPlayers(-7), 1);
    TestEqual(TEXT("capacity above former cap retained"), ERServer::ValidMaxPlayers(33), 33);
    TestEqual(TEXT("typed capacity is not limited by slider range"), ERServer::ValidMaxPlayers(128), 128);
    TestFalse(TEXT("half of sixteen insufficient"), ERLobby::HasQuorum(16, 8));
    TestTrue(TEXT("nine of sixteen starts"), ERLobby::HasQuorum(16, 9));
    TestFalse(TEXT("empty lobby cannot start"), ERLobby::HasQuorum(0, 0));
    TestTrue(TEXT("solo player ready can start"), ERLobby::HasQuorum(1, 1));
    TestFalse(TEXT("one of two insufficient"), ERLobby::HasQuorum(2, 1));
    TestTrue(TEXT("two of three is majority"), ERLobby::HasQuorum(3, 2));
    TestFalse(TEXT("half of four insufficient"), ERLobby::HasQuorum(4, 2));
    TestTrue(TEXT("three of four sufficient"), ERLobby::HasQuorum(4, 3));
    TestEqual(TEXT("zero votes admits all maps"), ERLobby::LeadingMaps(TArray<int32>{-1,-1}, 2), TArray<int32>({0,1}));
    TestEqual(TEXT("tied leaders only"), ERLobby::LeadingMaps(TArray<int32>{0,1}, 3), TArray<int32>({0,1}));
    TestEqual(TEXT("plurality without majority wins"), ERLobby::LeadingMaps(TArray<int32>{2,2,1,0,-1}, 3), TArray<int32>({2}));
    TestEqual(TEXT("out of range votes ignored"), ERLobby::LeadingMaps(TArray<int32>{-99,99,1}, 2), TArray<int32>({1}));
    TestEqual(TEXT("unvoted lobby only draws production maps"),ERMaps::LobbyLeaders(TArray<int32>{-1,-1}),TArray<int32>({0,2,3,5,6}));
    TestEqual(TEXT("stale Depot votes cannot win or enter random pool"),ERMaps::LobbyLeaders(TArray<int32>{1,1,1}),TArray<int32>({0,2,3,5,6}));
    TestEqual(TEXT("valid vote beats excluded Depot votes"),ERMaps::LobbyLeaders(TArray<int32>{1,1,2}),TArray<int32>({2}));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERRoundRotationCycle,"EclipseRecoil.Menu.RoundRotationCycle",EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::EngineFilter)
bool FERRoundRotationCycle::RunTest(const FString& Parameters)
{
    const auto Pool=ERMaps::Rotation();
    if(!TestTrue(TEXT("at least two production templates"),Pool.Num()>1))return false;
    TestTrue(TEXT("a reference map outside the pool restarts in place"),ERMaps::NextRoundMaps(TEXT("/Game/EclipseRecoil/Maps/L_Depot"),{}).IsEmpty());
    TSet<FString> Orders;
    for(int32 Seed=0;Seed<128;++Seed)
    {
        FRandomStream Random(Seed);TArray<FName> Played;
        FName Current=Pool[Seed%Pool.Num()];ERMaps::RecordRoundMap(Current,Played);
        TestEqual(TEXT("initial selected map consumes the first slot"),Played.Num(),1);
        TSet<FName> Cycle;Cycle.Add(Current);FString Order=Current.ToString();
        for(int32 Round=1;Round<Pool.Num()*4;++Round)
        {
            const auto Before=Played;const auto Choices=ERMaps::NextRoundMaps(Current,Played);
            TestEqual(TEXT("selection / failed travel cannot consume history"),Played,Before);
            TestEqual(TEXT("candidate count follows the remaining bag"),Choices.Num(),Played.Num()==Pool.Num() ? Pool.Num()-1:Pool.Num()-Played.Num());
            TestFalse(TEXT("no consecutive repeat, including cycle boundary"),Choices.Contains(Current));
            for(const FName Choice:Choices)
            {
                TestTrue(TEXT("production entries only"),Pool.Contains(Choice));
                if(Played.Num()<Pool.Num())TestFalse(TEXT("played templates unavailable until exhaustion"),Played.Contains(Choice));
            }
            if(Choices.IsEmpty())return false;
            Current=Choices[Random.RandHelper(Choices.Num())];
            if(Cycle.Num()==Pool.Num())Cycle.Reset();
            TestFalse(TEXT("template appears once in each complete cycle"),Cycle.Contains(Current));Cycle.Add(Current);
            ERMaps::RecordRoundMap(Current,Played);
            TestEqual(TEXT("arrival history survives a full cycle then restarts"),Played.Num(),Round%Pool.Num()+1);
            const auto Arrived=Played;ERMaps::RecordRoundMap(Current,Played);
            TestEqual(TEXT("duplicate arrival is idempotent even at cycle end"),Played,Arrived);
            Order+=TEXT("|")+Current.ToString();
        }
        TestEqual(TEXT("last cycle is complete"),Cycle.Num(),Pool.Num());Orders.Add(Order);
        const auto Before=Played;ERMaps::RecordRoundMap(FName(ERMaps::At(1).Package),Played);
        TestEqual(TEXT("reference Depot never consumes a rotation slot"),Played,Before);
        Played.Reset();
        ERMaps::RecordRoundMap(Current,Played);TestEqual(TEXT("new session starts a fresh bag"),Played.Num(),1);
    }
    TestTrue(TEXT("orders vary with random draws"),Orders.Num()>16);
    AddInfo(FString::Printf(TEXT("ER_ROTATION_CYCLES seeds=128 cycles=512 templates=%d orders=%d"),Pool.Num(),Orders.Num()));
    return true;
}
#endif
