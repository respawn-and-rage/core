#include "ERProceduralMap.h"
#include "ERLayoutSolids.h"
#include "ERMapPlayability.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
// The open-ground pass and the furniture kit may only add solids: every recorded route and start
// must stay standing-clear of what they built, on every template.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERInteriorsTest,"EclipseRecoil.Maps.Interiors",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERInteriorsTest::RunTest(const FString&)
{
    FERBlockRoomConfig B;
    for(const TCHAR* Id:{TEXT("Town"),TEXT("Rooms"),TEXT("CentralBuilding"),TEXT("Village"),TEXT("FireTraining"),TEXT("MiddleEasternAlleys"),TEXT("Rocky")})
    {
        const bool Declares=FString(Id)==TEXT("Town") || FString(Id)==TEXT("Rooms") || FString(Id)==TEXT("CentralBuilding");
        const bool Slow=FString(Id)==TEXT("MiddleEasternAlleys");
        auto* T=LoadObject<UERMapTemplate>(nullptr,*(FString(TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_"))+Id));
        if(!TestNotNull(Id,T))return false;
        int32 Layouts=0,Furnished=0,Pieces=0,Structures=0;TSet<FName> Programs,Ground;
        for(int32 Team:{1,2,3,4,8,16})for(int32 I=0;I<(Slow ? 4:Team==2 ? 128:8);++I)
        {
            FERProceduralConfig C;C.TemplateAsset=T;C.ExpectedTeamSize=Team;C.Seed=I<126 ? I:I==126 ? 7919:-17;
            FERProceduralLayout L,Again;
            const FString Case=FString::Printf(TEXT("%s %dv%d seed %d"),Id,Team,Team,C.Seed);
            if(!TestTrue(*(Case+TEXT(": builds")),ERProcedural::Build(C,B,L)))return false;
            ++Layouts;Furnished+=L.InteriorPieces>0 ? 1:0;Pieces+=L.InteriorPieces;
            Structures+=L.GroundFeatures;
            for(const FName& Feature:L.FeatureNames)
            {
                if(Feature.ToString().StartsWith(TEXT("Interior.")))Programs.Add(Feature);
                if(Feature.ToString().StartsWith(TEXT("Ground.")))Ground.Add(Feature);
            }
            TestTrue(*(Case+TEXT(": structures precede furniture")),L.GroundPanelStart>=0 && L.GroundPanelStart<=L.InteriorPanelStart && L.GroundFixedStart>=0 && L.GroundFixedStart<=L.InteriorFixedStart);
            TestTrue(*(Case+TEXT(": a sealed shell has no open ground")),L.RoomShell.IsEmpty() || L.GroundFeatures==0);
            if(!TestTrue(*(Case+TEXT(": furniture is appended last")),L.InteriorPanelStart>=0 && L.InteriorPanelStart<=L.Panels.Num() && L.InteriorFixedStart>=0 && L.InteriorFixedStart<=L.FixedPieces.Num()))return false;
            TestTrue(*(Case+TEXT(": mates are declared together")),L.Interiors.FilterByPredicate([](const FERLayoutInterior& Space){return Space.Paired;}).Num()%2==0);
            FERProceduralLayout Kit;
            for(int32 P=FMath::Max(0,L.GroundPanelStart);P<L.Panels.Num();++P)Kit.Panels.Add(L.Panels[P]);
            for(int32 P=FMath::Max(0,L.GroundFixedStart);P<L.FixedPieces.Num();++P)Kit.FixedPieces.Add(L.FixedPieces[P]);
            Kit.Boulders=L.Boulders;
            const ERLayoutQueries::FLayoutSolids Solids(Kit);
            TArray<TArray<FVector>> Paths=L.Routes;
            Paths.Append(L.SpawnExitRoutes);Paths.Append(L.StairRoutes);Paths.Append(L.RoomAccessRoutes);Paths.Append(L.CrossingRoutes);Paths.Append(L.GroundRoutes);
            Paths.Append(L.DistrictRoutes);Paths.Append(L.VillagePassages);Paths.Append(L.VillageSideBypasses);
            for(const auto& Zone:L.SpawnZones)Paths.Add(Zone.ExitRoute);
            bool Clear=true;
            for(const auto& Path:Paths)for(int32 J=1;J<Path.Num();++J)Clear&=Solids.PawnClear(Path[J-1],Path[J]);
            TestTrue(*(Case+TEXT(": every recorded route stays standing-clear of added solids")),Clear);
            TestTrue(*(Case+TEXT(": opposing starts stay screened")),Team==0 || ERPlayability::InitialSpawnsOccluded(L));
            bool Starts=true;
            for(const auto* Set:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& Start:*Set)
                Starts&=Solids.PawnClear(Start.GetLocation()-FVector(40,0,0),Start.GetLocation()+FVector(40,0,0)) && Solids.PawnClear(Start.GetLocation()-FVector(0,40,0),Start.GetLocation()+FVector(0,40,0));
            TestTrue(*(Case+TEXT(": every start stays clear of furniture")),Starts);
            // Open-ground structures are built with their mate, and their ladders can be climbed.
            struct FBuilt{FVector Centre,Size;EERBlockMaterial Material;};
            TArray<FBuilt> Built;
            for(int32 P=L.GroundPanelStart;P<L.InteriorPanelStart;++P)
            {
                const FBox Box=FBox(FVector::ZeroVector,L.Panels[P].Size).TransformBy(L.Panels[P].Transform);
                Built.Add({Box.GetCenter(),Box.GetSize(),L.Panels[P].Material});
            }
            for(int32 P=L.GroundFixedStart;P<L.InteriorFixedStart;++P)
            {
                const auto& Piece=L.FixedPieces[P];const FVector Half=Piece.Transform.GetScale3D()*50;
                const FBox Box=FBox(-Half,Half).TransformBy(FTransform(Piece.Transform.GetRotation(),Piece.Transform.GetLocation()));
                Built.Add({Box.GetCenter(),Box.GetSize(),Piece.Material});
            }
            // A boulder is a closed solid facing outward. The box that layout queries see through it lies
            // inside the rock, and the box that stops a pawn holds all of it.
            bool Rocks=true;
            for(const auto& Rock:L.Boulders)
            {
                TArray<FVector> Points;ERProcedural::BoulderTriangles(Rock,Points);
                const FTransform Frame(FRotator(0,Rock.Yaw,0),Rock.Center);
                TMap<FIntVector,int32> Edges;
                auto Key=[](FVector Q){return FIntVector(FMath::RoundToInt32(Q.X*10),FMath::RoundToInt32(Q.Y*10),FMath::RoundToInt32(Q.Z*10));};
                TMap<FIntVector,int32> Ids;
                auto Vertex=[&](FVector Q){const FIntVector Slot=Key(Q);if(const int32* Known=Ids.Find(Slot))return *Known;const int32 New=Ids.Num();Ids.Add(Slot,New);return New;};
                Rocks&=Points.Num()==240;
                for(int32 Tri=0;Tri+2<Points.Num();Tri+=3)
                {
                    const FVector V0=Points[Tri],V1=Points[Tri+1],V2=Points[Tri+2],Normal=FVector::CrossProduct(V1-V0,V2-V0).GetSafeNormal();
                    Rocks&=FVector::DotProduct(Normal,(V0+V1+V2)/3-Rock.Center)>0;
                    for(int32 Corner=0;Corner<8;++Corner)
                    {
                        const FVector Inner=Frame.TransformPositionNoScale(Rock.Radii*.4*FVector(Corner&1 ? 1:-1,Corner&2 ? 1:-1,Corner&4 ? 1:-1));
                        Rocks&=FVector::DotProduct(Normal,Inner-V0)<0;
                    }
                    const int32 Ia=Vertex(V0),Ib=Vertex(V1),Ic=Vertex(V2);
                    for(const FIntPoint& Edge:{FIntPoint(Ia,Ib),FIntPoint(Ib,Ic),FIntPoint(Ic,Ia)})++Edges.FindOrAdd(FIntVector(Edge.X,Edge.Y,0));
                    for(const FVector& Point:{V0,V1,V2})
                    {
                        const FVector Local=Frame.InverseTransformPositionNoScale(Point);
                        Rocks&=FMath::Abs(Local.X)<=Rock.Radii.X*1.1+.1 && FMath::Abs(Local.Y)<=Rock.Radii.Y*1.1+.1 &&
                            Local.Z<=Rock.Radii.Z*ERProcedural::BoulderTop+.1 && Local.Z>=-Rock.Radii.Z*1.1-.1;
                    }
                }
                // Closed and consistently wound: every directed edge once, with its reverse.
                for(const auto& Edge:Edges)Rocks&=Edge.Value==1 && Edges.Contains(FIntVector(Edge.Key.Y,Edge.Key.X,0));
            }
            TestTrue(*(Case+TEXT(": boulders are closed rocks around their sight box and inside their pawn box")),Rocks);
            // A boulder's mate is the same rock under the layout's symmetry.
            for(const auto& Rock:L.Boulders)Built.Add({Rock.Center,Rock.Radii,EERBlockMaterial::Stone});
            bool Mated=true;
            for(const auto& A:Built)
                Mated&=Built.ContainsByPredicate([&](const FBuilt& O){return O.Material==A.Material && O.Size.Equals(A.Size,1) &&
                    (O.Centre.Equals(FVector(-A.Centre.X,-A.Centre.Y,A.Centre.Z),1) || O.Centre.Equals(FVector(A.Centre.X,-A.Centre.Y,A.Centre.Z),1));});
            TestTrue(*(Case+TEXT(": open-ground structures have their mate")),Mated);
            bool Climbs=L.GroundLadderStart>=0 && L.GroundLadderStart<=L.Ladders.Num();
            for(int32 J=FMath::Max(0,L.GroundLadderStart);J<L.Ladders.Num();++J)
            {
                const auto& Ladder=L.Ladders[J];
                Climbs&=Solids.PawnClear(Ladder.Frame.TransformPosition({55,0,94}),Ladder.Frame.TransformPosition({55,0,Ladder.DeckHeight+94}));
                const FVector Landing=Ladder.Frame.TransformPosition({-100,0,Ladder.DeckHeight+94});
                Climbs&=Solids.PawnClear(Landing,Landing);
            }
            TestTrue(*(Case+TEXT(": open-ground ladders have a clear climb and landing")),Climbs);
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
        if(Declares)
        {
            TestTrue(*FString::Printf(TEXT("%s furnishes nearly every layout (%d of %d)"),Id,Furnished,Layouts),Furnished*10>=Layouts*9);
            TestTrue(*FString::Printf(TEXT("%s draws several room programs (%d)"),Id,Programs.Num()),Programs.Num()>=3);
        }
        AddInfo(FString::Printf(TEXT("ER_INTERIORS template=%s layouts=%d furnished=%d pieces=%d programs=%d structures=%d kinds=%d"),Id,Layouts,Furnished,Pieces,Programs.Num(),Structures,Ground.Num()));
    }
    return true;
}
#endif
