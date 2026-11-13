#include "ERProceduralMap.h"
#include "ERLayoutSolids.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
// The furniture kit may only add solids: every recorded route and start must stay standing-clear of it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERInteriorsTest,"EclipseRecoil.Maps.Interiors",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERInteriorsTest::RunTest(const FString&)
{
    FERBlockRoomConfig B;
    for(const TCHAR* Id:{TEXT("Town"),TEXT("Rooms"),TEXT("CentralBuilding")})
    {
        auto* T=LoadObject<UERMapTemplate>(nullptr,*(FString(TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_"))+Id));
        if(!TestNotNull(Id,T))return false;
        int32 Layouts=0,Furnished=0,Pieces=0;TSet<FName> Programs;
        for(int32 Team:{1,2,3,4,8,16})for(int32 I=0;I<(Team==2 ? 128:8);++I)
        {
            FERProceduralConfig C;C.TemplateAsset=T;C.ExpectedTeamSize=Team;C.Seed=I<126 ? I:I==126 ? 7919:-17;
            FERProceduralLayout L,Again;
            const FString Case=FString::Printf(TEXT("%s %dv%d seed %d"),Id,Team,Team,C.Seed);
            if(!TestTrue(*(Case+TEXT(": builds")),ERProcedural::Build(C,B,L)))return false;
            ++Layouts;Furnished+=L.InteriorPieces>0 ? 1:0;Pieces+=L.InteriorPieces;
            for(const FName& Feature:L.FeatureNames)if(Feature.ToString().StartsWith(TEXT("Interior.")))Programs.Add(Feature);
            if(!TestTrue(*(Case+TEXT(": furniture is appended last")),L.InteriorPanelStart>=0 && L.InteriorPanelStart<=L.Panels.Num() && L.InteriorFixedStart>=0 && L.InteriorFixedStart<=L.FixedPieces.Num()))return false;
            TestTrue(*(Case+TEXT(": mates are declared together")),L.Interiors.FilterByPredicate([](const FERLayoutInterior& Space){return Space.Paired;}).Num()%2==0);
            FERProceduralLayout Kit;
            for(int32 P=L.InteriorPanelStart;P<L.Panels.Num();++P)Kit.Panels.Add(L.Panels[P]);
            for(int32 P=L.InteriorFixedStart;P<L.FixedPieces.Num();++P)Kit.FixedPieces.Add(L.FixedPieces[P]);
            const ERLayoutQueries::FLayoutSolids Solids(Kit);
            TArray<TArray<FVector>> Paths=L.Routes;
            Paths.Append(L.SpawnExitRoutes);Paths.Append(L.StairRoutes);Paths.Append(L.RoomAccessRoutes);Paths.Append(L.CrossingRoutes);Paths.Append(L.GroundRoutes);
            for(const auto& Zone:L.SpawnZones)Paths.Add(Zone.ExitRoute);
            bool Clear=true;
            for(const auto& Path:Paths)for(int32 J=1;J<Path.Num();++J)Clear&=Solids.PawnClear(Path[J-1],Path[J]);
            TestTrue(*(Case+TEXT(": every recorded route stays standing-clear of furniture")),Clear);
            bool Starts=true;
            for(const auto* Set:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& Start:*Set)
                Starts&=Solids.PawnClear(Start.GetLocation()-FVector(40,0,0),Start.GetLocation()+FVector(40,0,0)) && Solids.PawnClear(Start.GetLocation()-FVector(0,40,0),Start.GetLocation()+FVector(0,40,0));
            TestTrue(*(Case+TEXT(": every start stays clear of furniture")),Starts);
            TSet<int32> Ids;bool Unique=true;
            for(const auto& Panel:L.Panels){Unique&=!Ids.Contains(Panel.Id);Ids.Add(Panel.Id);}
            TestTrue(*(Case+TEXT(": panel ids stay unique")),Unique);
            if(I<2)
            {
                TestTrue(*(Case+TEXT(": repeat builds")),ERProcedural::Build(C,B,Again));
                bool Same=L.Panels.Num()==Again.Panels.Num() && L.FixedPieces.Num()==Again.FixedPieces.Num();
                for(int32 P=0;Same && P<L.Panels.Num();++P)Same=L.Panels[P].Transform.Equals(Again.Panels[P].Transform) && L.Panels[P].Size.Equals(Again.Panels[P].Size);
                TestTrue(*(Case+TEXT(": furnishing is deterministic")),Same);
            }
        }
        TestTrue(*FString::Printf(TEXT("%s furnishes nearly every layout (%d of %d)"),Id,Furnished,Layouts),Furnished*10>=Layouts*9);
        TestTrue(*FString::Printf(TEXT("%s draws several room programs (%d)"),Id,Programs.Num()),Programs.Num()>=3);
        AddInfo(FString::Printf(TEXT("ER_INTERIORS template=%s layouts=%d furnished=%d pieces=%d programs=%d"),Id,Layouts,Furnished,Pieces,Programs.Num()));
    }
    return true;
}
#endif
