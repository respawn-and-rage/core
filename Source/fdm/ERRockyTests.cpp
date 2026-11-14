#include "ERProceduralMap.h"
#include "ERMapPlayability.h"
#include "ERLayoutSolids.h"
#include "ERCharacter.h"
#include "ERMovement.h"
#include "ERWeapon.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Collision.h"
#include "Components/PrimitiveComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FVector RockyMate(FVector P,bool Mirror){return Mirror ? FVector(P.X,-P.Y,P.Z):FVector(-P.X,-P.Y,P.Z);}
// The surface stays outside the sight core and inside the pawn-clearance box.
bool SoundCrag(const FERLayoutCrag& Crag)
{
    TArray<FVector> Points;ERProcedural::CragTriangles(Crag,Points);
    if(Points.IsEmpty() || Points.Num()%3!=0)return false;
    const FTransform Frame(FRotator(0,Crag.Yaw,0),Crag.Center);
    const double Reach=ERProcedural::CragReach(Crag),Peak=ERProcedural::CragPeak,Drop=ERProcedural::CragArchDrop(Crag);
    const FVector Core=ERProcedural::CragSightHalf(Crag)*.998;
    auto Local=[&](FVector P){FVector Q=Frame.InverseTransformPositionNoScale(P);if(Crag.Reflected)Q.Y=-Q.Y;return Q;};
    TMap<FIntVector,int32> Ids;TSet<FIntPoint> Edges;bool Sound=true;
    auto Vertex=[&](FVector Q){const FIntVector Slot(FMath::RoundToInt32(Q.X*20),FMath::RoundToInt32(Q.Y*20),FMath::RoundToInt32(Q.Z*20));if(const int32* Known=Ids.Find(Slot))return *Known;const int32 New=Ids.Num();Ids.Add(Slot,New);return New;};
    TArray<FVector> Triangle;
    Triangle.SetNum(3);
    double SignedVolume=0;
    for(int32 I=0;I+2<Points.Num();I+=3)
    {
        for(int32 J=0;J<3;++J)Triangle[J]=Local(Points[I+J]);
        // The tapered shell can straddle two faces of the sight box without
        // entering it. An overlap test handles those rounded corners.
        const FSeparatingAxisPointCheck CoreOverlap(Triangle,FVector::ZeroVector,Core,false);
        Sound&=!CoreOverlap.bHit;
        for(const FVector& Q:Triangle)
        {
            Sound&=FMath::Abs(Q.X)<=Crag.Half.X+Reach+.1 && FMath::Abs(Q.Y)<=Crag.Half.Y+Reach+.1 && Q.Z>=-Crag.Half.Z-Drop-.1 && Q.Z<=Crag.Half.Z+Peak+.1;
        }
        // A closed shell has positive volume when its triangles face outward in map space.
        SignedVolume+=FVector::DotProduct(Points[I]-Crag.Center,FVector::CrossProduct(Points[I+1]-Crag.Center,Points[I+2]-Crag.Center))/6;
        const int32 A=Vertex(Triangle[0]),B=Vertex(Triangle[1]),C=Vertex(Triangle[2]);
        for(const FIntPoint& Edge:{FIntPoint(A,B),FIntPoint(B,C),FIntPoint(C,A)}){Sound&=!Edges.Contains(Edge);Edges.Add(Edge);}
    }
    for(const FIntPoint& Edge:Edges)Sound&=Edges.Contains(FIntPoint(Edge.Y,Edge.X));
    return Sound && SignedVolume>0;
}
}

// Layout rules of the Rocky template over both symmetries, team sizes 1 to 16 and many seeds:
// pairing, area budget, screened starts, rock that is solid to sight, ground that is either an easy
// ramp or under rock, and every ledge reachable on foot with a way back.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERRockyLayoutTest,"EclipseRecoil.Maps.Rocky.Layout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERRockyLayoutTest::RunTest(const FString&)
{
    FERBlockRoomConfig B;int32 Layouts=0,Huts=0,Ledges=0;TSet<FName> Features;
    for(const EERTemplateSymmetry Symmetry:{EERTemplateSymmetry::HalfTurn,EERTemplateSymmetry::Mirror})
    {
        const bool Mirror=Symmetry==EERTemplateSymmetry::Mirror;
        auto* T=NewObject<UERMapTemplate>();T->TemplateId=TEXT("rocky.canyon");T->Archetype=EERMapArchetype::Rocky;T->Symmetry=Symmetry;T->ArenaSize={6000,6000,1500};
        FERProceduralConfig C;C.TemplateAsset=T;
        for(int32 Team=1;Team<=16;++Team)
        {
            TArray<int32> Seeds{42,7919};
            if(Team<=8){for(int32 I=0;I<12;++I)Seeds.AddUnique(I);Seeds.Append({73,-17,INT32_MIN,INT32_MAX});}
            TSet<FString> Plans;
            for(int32 Seed:Seeds)
            {
                C.ExpectedTeamSize=Team;C.Seed=Seed;FERProceduralLayout L;
                const FString Case=FString::Printf(TEXT("Rocky %s %dv%d seed %d"),Mirror ? TEXT("mirror"):TEXT("half-turn"),Team,Team,Seed);
                if(!TestTrue(*(Case+TEXT(": builds")),ERProcedural::Build(C,B,L)))return false;
                if(!Mirror && Team==3 && Seed==1)
                {
                    const double CourtHeight=L.AlphaSpawns[0].GetLocation().Z-100.;
                    TestTrue(TEXT("Rocky 3v3 seed 1 retains the high central ridge"),
                        L.FeatureNames.Contains(TEXT("RockHighSaddle")) && L.PoolWater.IsEmpty() &&
                        ERProcedural::GroundHeight(L,FVector2D::ZeroVector)>CourtHeight+900.);
                }
                const bool Basin=L.FeatureNames.Contains(TEXT("RockTerracedBasin"));
                const bool OffsetHills=L.FeatureNames.Contains(TEXT("RockOffsetHills"));
                const bool Diagonal=L.FeatureNames.Contains(TEXT("RockDiagonalRidge"));
                const bool Massif=L.FeatureNames.Contains(TEXT("RockCentralMassif"));
                const bool Plateaus=L.FeatureNames.Contains(TEXT("RockBrokenPlateaus"));
                const bool SeparateLandform=Basin || OffsetHills || Diagonal || Massif || Plateaus;
                if(!Mirror && Team==3 && (Seed==3 || Seed==5 || Seed==7919))
                    TestTrue(*(Case+TEXT(": stable terrain family")),Seed==3 ? Diagonal:Seed==5 ? Massif:Plateaus);
                if(!Mirror && Team==3 && (Seed==4 || Seed==42))
                    TestTrue(*(Case+TEXT(": stable terrain family")),Seed==4 ? Basin:OffsetHills);
                if(Basin || OffsetHills)
                {
                    const double CourtHeight=L.AlphaSpawns[0].GetLocation().Z-100.;
                    const double Centre=ERProcedural::GroundHeight(L,FVector2D::ZeroVector)-CourtHeight;
                    TestTrue(*(Case+TEXT(": low centre between accessible flanks")),
                        Centre<(Basin ? -540.:230.) && L.PoolWater.IsEmpty());
                }
                ++Layouts;
                const double Target=ERProcedural::TargetAreaM2(Team,EERMapArchetype::Rocky);
                TestTrue(*(Case+TEXT(": net ground within ten percent of the budget")),FMath::Abs(ERProcedural::PlayableAreaM2(L)-Target)<=Target*.1+.01);
                TestTrue(*(Case+TEXT(": closed rock is excluded from the area")),!L.InaccessibleGroundPolygons.IsEmpty() && ERProcedural::PlayableAreaM2(L)<PI*FMath::Square(L.ArenaSize.X*.5)/10000.);
                TestTrue(*(Case+TEXT(": every opposing start is screened")),ERPlayability::InitialSpawnsOccluded(L));
                // Pairing: starts, rock, ground and the builder's own constructions.
                bool Paired=L.AlphaSpawns.Num()>=8 && L.AlphaSpawns.Num()==L.OmegaSpawns.Num() && L.LayoutSymmetry==Symmetry && L.Crags.Num()%2==0;
                for(int32 I=0;Paired && I<L.AlphaSpawns.Num();++I)Paired=L.OmegaSpawns[I].GetLocation().Equals(RockyMate(L.AlphaSpawns[I].GetLocation(),Mirror),.02);
                for(int32 I=0;Paired && I+1<L.Crags.Num();I+=2)
                {
                    const auto& A=L.Crags[I];const auto& O=L.Crags[I+1];
                    const double Turn=FMath::Abs(FRotator::NormalizeAxis(O.Yaw-(Mirror ? -A.Yaw:A.Yaw+180)));
                    Paired=O.Center.Equals(RockyMate(A.Center,Mirror),.02) && O.Half.Equals(A.Half,.01) && O.Shape==A.Shape && O.Arch==A.Arch && O.Skin==A.Skin && !A.Reflected && O.Reflected==Mirror && Turn<.02;
                }
                TestTrue(*(Case+TEXT(": starts and rock are paired")),Paired);
                const int32 Columns=L.TerrainColumns,Rows=Columns>0 ? L.TerrainHeights.Num()/Columns:0;
                bool Ground=L.GradedTerrain && Columns>2 && Rows>2;
                for(int32 Y=0;Ground && Y<Rows;++Y)for(int32 X=0;X<Columns;++X)
                    Ground&=L.TerrainHeights[Y*Columns+X]==L.TerrainHeights[(Rows-1-Y)*Columns+(Mirror ? X:Columns-1-X)];
                TestTrue(*(Case+TEXT(": the ground is paired")),Ground);
                struct FBuilt{FVector Centre,Size;EERBlockMaterial Material;};TArray<FBuilt> Built;
                for(int32 P=0;P<L.GroundPanelStart;++P)
                {
                    const FBox Box=FBox(FVector::ZeroVector,L.Panels[P].Size).TransformBy(L.Panels[P].Transform);
                    Built.Add({Box.GetCenter(),Box.GetSize(),L.Panels[P].Material});
                }
                for(int32 P=0;P<L.GroundFixedStart;++P)
                {
                    const auto& Piece=L.FixedPieces[P];const FVector Half=Piece.Transform.GetScale3D()*50;
                    const FBox Box=FBox(-Half,Half).TransformBy(FTransform(Piece.Transform.GetRotation(),Piece.Transform.GetLocation()));
                    Built.Add({Box.GetCenter(),Box.GetSize(),Piece.Material});
                }
                bool Mated=true;
                for(const auto& A:Built)Mated&=Built.ContainsByPredicate([&](const FBuilt& O){return O.Material==A.Material && O.Size.Equals(A.Size,1) && O.Centre.Equals(RockyMate(A.Centre,Mirror),1);});
                for(int32 I=0;I+1<L.GroundLadderStart;I+=2)
                    Mated&=L.Ladders[I+1].Frame.GetLocation().Equals(RockyMate(L.Ladders[I].Frame.GetLocation(),Mirror),.02) && L.Ladders[I+1].DeckHeight==L.Ladders[I].DeckHeight;
                TestTrue(*(Case+TEXT(": buildings and ladders are paired")),Mated && L.GroundLadderStart%2==0);
                bool PairedRoutes=true;
                for(int32 I=0;I<L.CrossingRoutes.Num();++I)
                {
                    const auto& Tunnel=L.CrossingRoutes[I];
                    const int32 MateIndex=Mirror ? I:1-I;
                    if(!L.CrossingRoutes.IsValidIndex(MateIndex)){PairedRoutes=false;continue;}
                    const auto& Other=L.CrossingRoutes[MateIndex];
                    if(Other.Num()!=Tunnel.Num()){PairedRoutes=false;continue;}
                    for(int32 Point=0;Point<Tunnel.Num();++Point)
                        PairedRoutes&=RockyMate(Tunnel[Point],Mirror).Equals(Other[Other.Num()-1-Point],.1);
                }
                TestTrue(*(Case+TEXT(": tunnel routes are paired")),PairedRoutes);
                TestTrue(*(Case+TEXT(": circular arena")),L.CircularArena && L.ArenaSize.X==L.ArenaSize.Y);
                TestTrue(*(Case+TEXT(": two tunnels and one approach to the crest per half")),
                    L.CrossingRoutes.Num()==2 && L.RooftopRoutes.Num()==2 && L.FeatureNames.Contains(TEXT("LunarOutpost")));
                TestTrue(*(Case+TEXT(": two lookout ladders and rock cover in both halves")),
                    L.Ladders.Num()==2 && L.RockyCoverOnly && L.Crags.Num()>58);
                if(L.CrossingRoutes.Num()==2 && L.RooftopRoutes.Num()==2)
                {
                    const double Lookout=L.RooftopRoutes[0].Last().Z;
                    const auto& Summit=L.RockScrambles[0];
                    const double Court=L.AlphaSpawns[0].GetLocation().Z-100;
                    // The lookout is a lower shelf alongside the summit crags.
                    double Crown=Lookout-100;
                    for(const auto& Crag:L.Crags)
                        if(FVector::DistSquared2D(Crag.Center,Summit.Last())<FMath::Square(800.))
                            Crown=FMath::Max(Crown,Crag.Center.Z+Crag.Half.Z);
                    TestTrue(*(Case+TEXT(": the stone summit matches its terrain family")),SeparateLandform ? Crown-Court>350 && Crown-Court<840:Crown-Court>=930 && Crown-Court<=1150);
                    const double MinLookoutRise=SeparateLandform ? (Basin ? 170.:390.):Team==1 ? 755.:680.;
                    for(const auto& Trail:L.RooftopRoutes)
                    {
                        const double Rise=Trail.Last().Z-100-Court;
                        double MinX=Trail[0].X,MaxX=Trail[0].X,Length=0;
                        for(int32 I=1;I<Trail.Num();++I)
                        {
                            MinX=FMath::Min(MinX,Trail[I].X);
                            MaxX=FMath::Max(MaxX,Trail[I].X);
                            Length+=FVector::Dist2D(Trail[I-1],Trail[I]);
                        }
                        const bool Gully=L.FeatureNames.Contains(TEXT("RockGully"));
                        const bool Winding=Gully || SeparateLandform ? MaxX-MinX>310. : MinX<-350 && MaxX>350;
                        TestTrue(*FString::Printf(TEXT("%s: winding ramps reach the upper lookout (rise %.0f, sweep %.0f to %.0f, length %.0f)"),*Case,Rise,MinX,MaxX,Length),
                            FMath::Abs(Trail.Last().Y)<=(SeparateLandform ? 1250.:700.) && FMath::Abs(Trail.Last().Z-Lookout)<.1 && Rise>=MinLookoutRise && Rise<=1150 &&
                            Winding && Length>FVector::Dist2D(Trail[0],Trail.Last())*(Gully || SeparateLandform ? 1.025:1.15));
                    }
                    const ERLayoutQueries::FLayoutSolids Rock(L);
                    for(const auto& Tunnel:L.CrossingRoutes)
                    {
                        const auto* Center=Tunnel.FindByPredicate([](const FVector& P){return FMath::IsNearlyZero(P.Y);});
                        if(!TestNotNull(*(Case+TEXT(": tunnel center")),Center))continue;
                        const auto* Entry=Tunnel.FindByPredicate([](const FVector& P){return FMath::IsNearlyEqual(P.Y,-ERProcedural::RockyTunnelMouth,.1);});
                        const auto* Exit=Tunnel.FindByPredicate([](const FVector& P){return FMath::IsNearlyEqual(P.Y,ERProcedural::RockyTunnelMouth,.1);});
                        if(!TestNotNull(*(Case+TEXT(": tunnel entrance")),Entry) || !TestNotNull(*(Case+TEXT(": tunnel exit")),Exit))continue;
                        TestTrue(*(Case+TEXT(": tunnel bends inside the ridge")),FMath::PointDistToSegment(*Center,*Entry,*Exit)>430);
                        for(const FVector* Portal:{Entry,Exit})
                        {
                            TestTrue(*(Case+TEXT(": tunnel has a standing corridor")),Rock.PawnClearWorld(*Center,*Portal));
                            const FVector UnderArch=FMath::Lerp(*Center,*Portal,.45);
                            TestFalse(*(Case+TEXT(": tunnel arch has a solid core")),Rock.SightClear(UnderArch,UnderArch+FVector(0,0,900)));
                        }
                    }
                    Plans.Add(FString::Printf(TEXT("%.0f|%.0f|%.0f"),Lookout,L.CrossingRoutes[0][1].X,L.RooftopRoutes[0][0].X));
                }
                // On foot: no trap, every ledge reached and left, every ladder standing on something.
                const auto Audit=ERProcedural::AuditTraversal(L);
                if(!Audit.Passed())AddError(FString::Printf(TEXT("%s: reached=%d trapped=%d ledges_missed=%d/%d ladders=%d trap=%s ledge=%s"),*Case,Audit.Reached,Audit.Trapped,Audit.LedgesMissed,Audit.Ledges,Audit.BrokenLadders,
                    *(Audit.TrapSamples.IsEmpty() ? FVector::ZeroVector:Audit.TrapSamples[0]).ToString(),*(Audit.MissedSamples.IsEmpty() ? FVector::ZeroVector:Audit.MissedSamples[0]).ToString()));
                Ledges+=Audit.Ledges;
                const double Court=L.AlphaSpawns[0].GetLocation().Z-100;
                const double Radius=L.ArenaSize.X*.5;
                const ERLayoutQueries::FLayoutSolids Solids(L);
                // A rounded shell extends beyond its conservative sight box.
                // Trace the actual mesh to measure the ridge across both portals.
                TArray<FVector> Formation;
                for(const auto& Crag:L.Crags)
                    if(FMath::Abs(Crag.Center.Y)<850)ERProcedural::CragTriangles(Crag,Formation);
                bool RimToRim=true;
                TArray<FString> Gaps;
                for(double X=-Radius+100;X<=Radius-100;X+=100)
                {
                    if(L.FeatureNames.Contains(TEXT("RockGully")) && FMath::Abs(X)<=420)continue;
                    bool Covered=false;
                    for(double Y:{-600.,-500.,-400.,-300.,-200.,-100.,0.,100.,200.,300.,400.,500.,600.})
                    {
                        const FVector Low(X,Y,Court+420),High(X,Y,Court+1350);
                        Covered|=ERProcedural::GroundHeight(L,{X,Y})>=Low.Z;
                        for(int32 I=0;!Covered && I<Formation.Num();I+=3)
                        {
                            const FVector A=Formation[I],Mid=Formation[I+1],D=Formation[I+2];
                            if(X<FMath::Min3(A.X,Mid.X,D.X) || X>FMath::Max3(A.X,Mid.X,D.X) ||
                               Y<FMath::Min3(A.Y,Mid.Y,D.Y) || Y>FMath::Max3(A.Y,Mid.Y,D.Y))continue;
                            FVector Hit,Normal;
                            Covered=FMath::SegmentTriangleIntersection(High,Low,A,Mid,D,Hit,Normal);
                        }
                    }
                    if(!Covered)Gaps.Add(FString::Printf(TEXT("%.0f"),X));
                    RimToRim&=Covered;
                }
                if(!SeparateLandform)TestTrue(*FString::Printf(TEXT("%s: the formation spans the arena at ridge height (gaps at %s)"),*Case,*FString::Join(Gaps,TEXT(","))),RimToRim);
                TestTrue(*(Case+TEXT(": the summit and base have paired rock ledges")),L.RockScrambles.Num()>=4);
                if(L.RockScrambles.Num()>=4)
                {
                    const auto& Summit=L.RockScrambles[0];const auto& BaseRock=L.RockScrambles[2];
                    TestTrue(*FString::Printf(TEXT("%s: raised summit and base shelves (summit %.0f, base rise %.0f)"),*Case,Summit.Last().Z-Court-100,BaseRock.Last().Z-BaseRock[0].Z),
                        Summit.Last().Z>Court+(SeparateLandform ? (Basin ? 220:420):L.FeatureNames.Contains(TEXT("RockGully")) ? 720:775) && BaseRock.Last().Z>BaseRock[0].Z+90);
                }
                bool Hops=true;
                for(const auto& Route:L.StairRoutes)for(int32 I=1;I<Route.Num();++I)Hops&=FMath::Abs(Route[I].Z-Route[I-1].Z)<=ERProcedural::JumpRise+.01;
                TestTrue(*(Case+TEXT(": no rock step rises more than a jump")),Hops);
                // Rock taller than a player stops a line through its core, at chest and at head height.
                bool Opaque=true;
                for(int32 I=0;I<L.Crags.Num();I+=2)
                {
                    const auto& Crag=L.Crags[I];const FQuat Turn(FRotator(0,Crag.Yaw,0));
                    const double Floor=ERProcedural::GroundHeight(L,FVector2D(Crag.Center)),Top=Crag.Center.Z+Crag.Half.Z;
                    if(Top<Floor+200 || Crag.Arch)continue;
                    for(double Z:{Floor+110,Floor+175})for(const FVector& Axis:{FVector(1,0,0),FVector(0,1,0)})
                    {
                        const FVector Reach=Turn.RotateVector(Axis*(FVector::DotProduct(Axis,Crag.Half)+200));
                        const FVector Through(Crag.Center.X,Crag.Center.Y,FMath::Clamp(Z,Crag.Center.Z-Crag.Half.Z+5,Top-5));
                        Opaque&=!Solids.SightClear(Through-Reach,Through+Reach);
                    }
                }
                TestTrue(*(Case+TEXT(": tall rock is opaque to sight")),Opaque);
                auto CheckPaths=[&](const TCHAR* Kind,const TArray<TArray<FVector>>& Paths)
                {
                    for(const auto& Path:Paths)for(int32 J=1;J<Path.Num();++J)
                    {
                        if(Solids.PawnClearWorld(Path[J-1],Path[J]))continue;
                        AddError(FString::Printf(TEXT("%s: %s path blocked %s -> %s"),*Case,Kind,*Path[J-1].ToString(),*Path[J].ToString()));
                        return;
                    }
                };
                CheckPaths(TEXT("patrol"),L.Routes);
                CheckPaths(TEXT("spawn"),L.SpawnExitRoutes);
                CheckPaths(TEXT("room access"),L.RoomAccessRoutes);
                CheckPaths(TEXT("crossing"),L.CrossingRoutes);
                CheckPaths(TEXT("ground"),L.GroundRoutes);
                CheckPaths(TEXT("crest"),L.RooftopRoutes);
                CheckPaths(TEXT("rock shelves"),L.RockScrambles);
                for(const auto& Zone:L.SpawnZones)CheckPaths(TEXT("spawn zone"),{Zone.ExitRoute});
                if(Seed==42 || Seed==7919)
                {
                    bool Rock=true;for(const auto& Crag:L.Crags)Rock&=SoundCrag(Crag);
                    TestTrue(*(Case+TEXT(": every crag is a closed skin around its core")),Rock);
                    FERProceduralLayout Again;
                    TestTrue(*(Case+TEXT(": repeat builds")),ERProcedural::Build(C,B,Again));
                    bool Same=L.Crags.Num()==Again.Crags.Num() && L.Panels.Num()==Again.Panels.Num() && L.TerrainHeights==Again.TerrainHeights && L.FixedPieces.Num()==Again.FixedPieces.Num();
                    for(int32 I=0;Same && I<L.Crags.Num();++I)Same=L.Crags[I].Center==Again.Crags[I].Center && L.Crags[I].Half==Again.Crags[I].Half && L.Crags[I].Shape==Again.Crags[I].Shape;
                    for(int32 I=0;Same && I<L.Panels.Num();++I)Same=L.Panels[I].Transform.Equals(Again.Panels[I].Transform);
                    TestTrue(*(Case+TEXT(": deterministic from the seed")),Same);
                    FERCellWorld World;FString Error;
                    TestTrue(*(Case+TEXT(": fits the calibrated cell budget ")+Error),World.Build(L.Panels,FERCellSettings(),GetDefault<UERBlockMaterialCatalog>(),Error));
                }
                Huts+=L.FeatureNames.Contains(TEXT("LunarOutpost")) ? 1:0;
                for(const FName& Feature:L.FeatureNames)Features.Add(Feature);
            }
            if(Team>=2 && Team<=8)
                TestTrue(*(FString::Printf(TEXT("Rocky %dv%d: seeds vary the crest and its approaches"),Team,Team)),Plans.Num()>=Seeds.Num()/2);
        }
    }
    for(const TCHAR* Feature:{TEXT("RockRidge"),TEXT("RockTrail"),TEXT("RockTunnel"),TEXT("RockOutcrops"),TEXT("LunarOutpost")})
        TestTrue(*FString::Printf(TEXT("some layout has %s"),Feature),Features.Contains(Feature));
    AddInfo(FString::Printf(TEXT("ER_ROCKY_LAYOUT layouts=%d ledges=%d huts=%d features=%d"),Layouts,Ledges,Huts,Features.Num()));
    return true;
}

// The same rules against real collision: what the layout stage counts on must be there.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERRockyPhysicsTest,"EclipseRecoil.Maps.Rocky.Physics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERRockyPhysicsTest::RunTest(const FString&)
{
    auto* T=LoadObject<UERMapTemplate>(nullptr,TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_Rocky"));if(!TestNotNull(TEXT("Rocky recipe"),T))return false;
    UWorld::InitializationValues V;V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);if(!World)return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;Map->bCeilings=false;
    for(int32 Team=1;Team<=8;++Team)for(int32 Seed:{3,4,5,42,73,7919})
    {
        Map->LayoutConfig.ExpectedTeamSize=Team;Map->LayoutConfig.Seed=Seed;
        const FString Case=FString::Printf(TEXT("Rocky %dv%d seed %d"),Team,Team,Seed);
        if(!TestTrue(*(Case+TEXT(": physical generation")),Map->Regenerate()))continue;
        const auto& L=Map->GetLayout();TestTrue(*(Case+TEXT(": valid calibrated cells")),Map->ValidateCells());
        TestTrue(*(Case+TEXT(": supported and clear spawn zones")),ERProcedural::SpawnZonesClearInWorld(World,Map->GetActorTransform(),L));
        TestTrue(*(Case+TEXT(": all real sight and projectile rays screened")),ERPlayability::InitialSpawnsOccludedInWorld(World,FTransform::Identity,L));
        FCollisionQueryParams Q(SCENE_QUERY_STAT(ERRockyPhysics),false);FHitResult Hit;
        const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);
        // Cross the ring at the elevation of the highest accessible ridge.
        const double RimRadius=L.ArenaSize.X*.5;
        const double RimTestZ=L.AlphaSpawns[0].GetLocation().Z+1180;
        for(int32 Sample=0;Sample<96;++Sample)
        {
            const double Angle=Sample*2*PI/96;
            const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
            const FVector Height(0,0,RimTestZ);
            if(World->SweepSingleByChannel(Hit,Direction*(RimRadius-500)+Height,
                Direction*(RimRadius+850)+Height,FQuat::Identity,ECC_Pawn,Capsule,Q))continue;
            AddError(FString::Printf(TEXT("%s: open perimeter at %.1f degrees"),*Case,FMath::RadiansToDegrees(Angle)));
            break;
        }
        if(L.FeatureNames.Contains(TEXT("RockCentralMassif")))
        {
            const auto& Cave=L.GroundRoutes.Last();
            TestTrue(*(Case+TEXT(": cavern has two side openings")),
                Cave[0].X<-1100 && Cave.Last().X>1100);
            const FVector Inside(0,0,ERProcedural::GroundHeight(L,FVector2D::ZeroVector)+100.);
            TestTrue(*(Case+TEXT(": the walkable cavern is roofed by solid rock")),
                World->LineTraceSingleByChannel(Hit,Inside,Inside+FVector(0,0,800),ECC_Visibility,Q) &&
                Hit.GetComponent() && Hit.GetComponent()->GetFName()==TEXT("RockField"));
        }
        if(L.FeatureNames.Contains(TEXT("RockGully")))
        {
            const auto& Ford=L.GroundRoutes.Last();
            const FVector EyeOffset(0,0,52);
            TestTrue(*(Case+TEXT(": bedrock screens a direct line through the gorge")),
                World->LineTraceSingleByChannel(Hit,Ford[0]+EyeOffset,Ford.Last()+EyeOffset,ECC_Visibility,Q));
            const FVector Bed(0,0,ERProcedural::GroundHeight(L,{0,0}));
            FVector Surface;
            TestTrue(*(Case+TEXT(": the centre crossing has wading depth")),Map->WaterDepthAt(Bed)>=25 && Map->WaterSurfaceAt(Bed,Surface));
            TestTrue(*(Case+TEXT(": the banks remain dry")),Map->WaterDepthAt(Bed+FVector(350,0,0))==0);
        }
        for(const auto& Tunnel:L.CrossingRoutes)
        {
            const auto* Center=Tunnel.FindByPredicate([](const FVector& Point){return FMath::IsNearlyZero(Point.Y);});
            if(!TestNotNull(*(Case+TEXT(": tunnel center")),Center))continue;
            const auto* Entry=Tunnel.FindByPredicate([](const FVector& P){return FMath::IsNearlyEqual(P.Y,-ERProcedural::RockyTunnelMouth,.1);});
            const auto* Exit=Tunnel.FindByPredicate([](const FVector& P){return FMath::IsNearlyEqual(P.Y,ERProcedural::RockyTunnelMouth,.1);});
            if(!TestNotNull(*(Case+TEXT(": tunnel entrance")),Entry) || !TestNotNull(*(Case+TEXT(": tunnel exit")),Exit))continue;
            for(const FVector* Portal:{Entry,Exit})
            {
                const FVector UnderArch=FMath::Lerp(*Center,*Portal,.45);
                TestTrue(*(Case+TEXT(": tunnel ceiling is solid rock")),
                    World->LineTraceSingleByChannel(Hit,UnderArch,UnderArch+FVector(0,0,900),ECC_Visibility,Q) &&
                    Hit.GetComponent() && Hit.GetComponent()->GetFName()==TEXT("RockField"));
            }
            for(const ECollisionChannel Channel:{ECC_Visibility,ECC_GameTraceChannel1})
                TestTrue(*(Case+TEXT(": rock screens the two tunnel mouths")),
                    World->LineTraceSingleByChannel(Hit,*Entry+FVector(0,0,60),*Exit+FVector(0,0,60),Channel,Q) &&
                    Hit.GetComponent() && Hit.GetComponent()->GetFName()==TEXT("RockField"));
        }
        // The ground mesh is the height grid, and a walking route only ever touches walkable ground.
        int32 Ground=0,Routes=0,Rocks=0,Climbs=0,MeasuredTops=0;
        for(double Y=-L.ArenaSize.Y*.5+150;Y<L.ArenaSize.Y*.5-150;Y+=333)for(double X=-L.ArenaSize.X*.5+150;X<L.ArenaSize.X*.5-150;X+=333)
        {
            if(FVector2D(X,Y).Size()>L.ArenaSize.X*.5-170)continue;
            const double Floor=ERProcedural::GroundHeight(L,FVector2D(X,Y));
            if(!World->LineTraceSingleByChannel(Hit,FVector(X,Y,Floor+2000),FVector(X,Y,Floor-50),ECC_Visibility,Q))
            {AddError(FString::Printf(TEXT("%s: no ground under %.0f,%.0f"),*Case,X,Y));continue;}
            if(Hit.GetComponent() && (Hit.GetComponent()->GetFName()==TEXT("AlleyTerrain") || Hit.GetComponent()->GetFName()==TEXT("RidgeBedrock"))){++Ground;TestTrue(*(Case+TEXT(": the ground mesh follows the height grid")),FMath::Abs(Hit.ImpactPoint.Z-Floor)<1);}
        }
        for(const auto& Trail:L.RooftopRoutes)
        {
            const FVector Lookout=Trail.Last();
            TestTrue(*(Case+TEXT(": the climb ends on solid bedrock")),
                World->LineTraceSingleByChannel(Hit,Lookout,Lookout-FVector(0,0,155),ECC_Visibility,Q) &&
                Hit.GetComponent() && Hit.GetComponent()->GetFName()==TEXT("RidgeBedrock"));
        }
        TArray<TArray<FVector>> Paths=L.RoomAccessRoutes;Paths.Append(L.GroundRoutes);Paths.Append(L.SpawnExitRoutes);Paths.Append(L.RooftopRoutes);Paths.Append(L.RockScrambles);
        for(const auto& Path:Paths)for(int32 I=1;I<Path.Num();++I)
        {
            ++Routes;
            const bool Blocked=World->SweepSingleByChannel(Hit,Path[I-1],Path[I],FQuat::Identity,ECC_Pawn,Capsule,Q);
            const bool Walkable=Blocked && Hit.GetComponent() && (Hit.GetComponent()->GetFName()==TEXT("AlleyTerrain") || Hit.GetComponent()->GetFName()==TEXT("RidgeBedrock")) && Hit.ImpactNormal.Z>=.72;
            if(Blocked && !Walkable)AddError(FString::Printf(TEXT("%s: route %s -> %s blocked by %s normal=%s at %s"),*Case,*Path[I-1].ToString(),*Path[I].ToString(),*GetNameSafe(Hit.GetComponent()),*Hit.ImpactNormal.ToString(),*Hit.ImpactPoint.ToString()));
        }
        for(const auto& Crag:L.Crags)
        {
            const FQuat Turn(FRotator(0,Crag.Yaw,0));const double Top=Crag.Center.Z+Crag.Half.Z,Floor=ERProcedural::GroundHeight(L,FVector2D(Crag.Center));
            // Ridges overlap by design. Measure surfaces exposed at the centre
            // and let the traversal audit find standing space on each ledge.
            const FVector Above(Crag.Center.X,Crag.Center.Y,Top+400);
            const bool Struck=World->LineTraceSingleByChannel(Hit,Above,Above-FVector(0,0,500),ECC_Visibility,Q);
            if(Struck && Hit.GetComponent() && Hit.GetComponent()->GetName()==TEXT("RockField") &&
                Hit.ImpactPoint.Z<=Top+ERProcedural::CragPeak+.5)
            {
                ++MeasuredTops;
                TestTrue(*(Case+TEXT(": rock over its core")),Hit.ImpactPoint.Z>=Top-.5);
            }
            if(Top<Floor+200 || Crag.Arch)continue;
            ++Rocks;
            for(const ECollisionChannel Channel:{ECC_Visibility,ECC_GameTraceChannel1})for(const FVector& Axis:{FVector(1,0,0),FVector(0,1,0)})
            {
                const FVector Reach=Turn.RotateVector(Axis*(FVector::DotProduct(Axis,Crag.Half)+ERProcedural::CragReach(Crag)+200));
                const FVector Through(Crag.Center.X,Crag.Center.Y,FMath::Clamp(Floor+175,Crag.Center.Z-Crag.Half.Z+5,Top-5));
                TestTrue(*(Case+TEXT(": tall rock stops sight and rounds")),World->LineTraceSingleByChannel(Hit,Through-Reach,Through+Reach,Channel,Q));
            }
        }
        for(int32 I=0;I<L.GroundLadderStart;++I)
        {
            ++Climbs;const auto& Ladder=L.Ladders[I];
            const FVector Foot=Ladder.Frame.TransformPositionNoScale(FVector(55,0,96)),Head=Ladder.Frame.TransformPositionNoScale(FVector(-100,0,Ladder.DeckHeight+96));
            TestFalse(*(Case+TEXT(": a climber fits at the foot of a ladder")),World->OverlapBlockingTestByChannel(Foot,FQuat::Identity,ECC_Pawn,Capsule,Q));
            TestFalse(*(Case+TEXT(": a climber fits on the landing of a ladder")),World->OverlapBlockingTestByChannel(Head,FQuat::Identity,ECC_Pawn,Capsule,Q));
            TestTrue(*(Case+TEXT(": the landing of a ladder is solid")),World->LineTraceSingleByChannel(Hit,Head,Head-FVector(0,0,101),ECC_Pawn,Q) && Hit.ImpactNormal.Z>.72);
            TestTrue(*(Case+TEXT(": a ladder has rungs to hold")),World->LineTraceSingleByChannel(Hit,Ladder.Frame.TransformPositionNoScale(FVector(15,0,50)),Ladder.Frame.TransformPositionNoScale(FVector(-15,0,50)),ECC_Visibility,Q));
        }
        TestTrue(*(Case+TEXT(": ground, rock and routes were measured")),Ground>20 && Rocks>4 && MeasuredTops>4 && Routes>4);
        const uint32 Digest=Map->GetStateDigest(),Geometry=Map->GetTemplateGeometryDigest();
        TestTrue(*(Case+TEXT(": regeneration restores the map")),Map->Regenerate());
        TestEqual(*(Case+TEXT(": regeneration restores the state digest")),Map->GetStateDigest(),Digest);
        TestEqual(*(Case+TEXT(": regeneration restores the geometry digest")),Map->GetTemplateGeometryDigest(),Geometry);
        UE_LOG(LogTemp,Display,TEXT("ER_ROCKY_PHYSICS team=%d seed=%d ground=%d rocks=%d rock_tops=%d ladders=%d routes=%d"),
            Team,Seed,Ground,Rocks,MeasuredTops,Climbs,Routes);
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERRockClimbTest,"EclipseRecoil.Maps.Rocky.RockClimb",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERRockClimbTest::RunTest(const FString&)
{
    auto* Recipe=LoadObject<UERMapTemplate>(nullptr,TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_Rocky"));
    if(!TestNotNull(TEXT("Rocky recipe"),Recipe))return false;
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if(!TestNotNull(TEXT("Rock climbing world"),World))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };

    auto* Map=World->SpawnActor<AERProceduralMap>();
    Map->LayoutConfig.TemplateAsset=Recipe;
    Map->LayoutConfig.ExpectedTeamSize=3;
    if(!TestTrue(TEXT("Rocky generates for rock climbing"),Map->Regenerate()))return false;
    auto* Surface=FindObjectFast<UDynamicMeshComponent>(Map,TEXT("RockField"));
    if(!TestNotNull(TEXT("Rocky rock mesh"),Surface))return false;

    auto* Pawn=World->SpawnActor<AERCharacter>(FVector(0,0,2200),FRotator::ZeroRotator);
    auto* Movement=CastChecked<UERMovement>(Pawn->GetCharacterMovement());
    Movement->bRunPhysicsWithNoController=true;
    Pawn->Weapon=World->SpawnActor<AERWeapon>();
    Pawn->Weapon->SetOwner(Pawn);
    Pawn->Weapon->Data=NewObject<UERWeaponData>(Pawn->Weapon);
    Pawn->Weapon->Data->Payload=EERPayload::Bullet;
    Pawn->Weapon->Data->bOneHanded=true;
    TestTrue(TEXT("the fixture uses a weapon normally allowed on ladders"),Pawn->CanHoldWeaponOnLadder());

    constexpr float FrameTime=1.f/60;
    constexpr uint8 Climb=FSavedMove_Character::FLAG_Custom_1;
    auto Move=[&](uint8 Flags,float Direction)
    {
        Movement->MoveAutonomous(0,FrameTime,Flags,FVector(Direction*Movement->GetMaxAcceleration(),0,0));
    };
    // An actual generated crag must also admit a continuous foothold. The
    // controlled faces below cover exact angles and the transition onto a top.
    bool ClimbedCrag=false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERRockClimbTest),false,Pawn);
    const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);
    for(const auto& Crag:Map->GetLayout().Crags)
    {
        if(ClimbedCrag)break;
        if(Crag.Arch || !Map->IsClimbableRock(Surface,Crag.Center))continue;
        TArray<FVector> Faces;
        ERProcedural::CragTriangles(Crag,Faces);
        for(int32 Index=0;Index+2<Faces.Num();Index+=3)
        {
            const FVector& A=Faces[Index];
            const FVector& EdgeB=Faces[Index+1];
            const FVector& EdgeC=Faces[Index+2];
            const FVector Normal=FVector::CrossProduct(EdgeB-A,EdgeC-A).GetSafeNormal();
            if(Normal.Z<.18 || Normal.Z>.6)continue;
            const FVector Point=(A+EdgeB+EdgeC)/3;
            const FVector Start=Point+Normal*50.+FVector(0,0,59.);
            if(World->OverlapBlockingTestByChannel(Start,FQuat::Identity,ECC_Pawn,Capsule,Query))continue;
            const FVector UpRock=-Normal.GetSafeNormal2D();
            Pawn->SetActorLocationAndRotation(Start,UpRock.Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
            Movement->SetMovementMode(MOVE_Falling);
            Movement->RetryAfter=0;
            Movement->Velocity=FVector::ZeroVector;
            for(int32 Frame=0;Frame<30;++Frame)
            {
                Movement->MoveAutonomous(0,FrameTime,Climb,UpRock*Movement->GetMaxAcceleration());
                if(!Movement->IsRockClimbing())break;
            }
            ClimbedCrag=Movement->IsRockClimbing() && Pawn->GetActorLocation().Z>Start.Z+25.;
            if(ClimbedCrag)break;
        }
    }
    TestTrue(TEXT("a generated Rocky crag can be climbed"),ClimbedCrag);
    Pawn->SetActorRotation(FRotator::ZeroRotator);

    auto Approach=[&](double Angle,double X=0.)
    {
        const double Radians=FMath::DegreesToRadians(Angle);
        const FVector Along(FMath::Cos(Radians),0,FMath::Sin(Radians));
        const FVector Normal(-Along.Z,0,Along.X);
        const FVector Toe(X,0,2000);
        const FVector Top=Toe+Along*410.;
        const FVector Across(0,210.,0);
        UE::Geometry::FDynamicMesh3 TestMesh;
        TestMesh.AppendVertex(Toe-Across);TestMesh.AppendVertex(Top-Across);
        TestMesh.AppendVertex(Toe+Across);TestMesh.AppendVertex(Top+Across);
        TestMesh.AppendVertex(Top-Across+FVector(370,0,0));
        TestMesh.AppendVertex(Top+Across+FVector(370,0,0));
        TestMesh.AppendTriangle(0,2,1);TestMesh.AppendTriangle(1,2,3);
        TestMesh.AppendTriangle(1,3,4);TestMesh.AppendTriangle(4,3,5);
        Surface->SetMesh(MoveTemp(TestMesh));
        Surface->UpdateCollision(false);
        Pawn->SetActorLocation(Toe+Along*85.+Normal*47.+FVector(0,0,59.),false,nullptr,ETeleportType::TeleportPhysics);
        Movement->SetMovementMode(MOVE_Falling);
        Movement->RetryAfter=0;
        Movement->Velocity=FVector::ZeroVector;
    };

    Approach(65.);
    Move(0,1);
    TestFalse(TEXT("forward alone does not attach"),Movement->IsRockClimbing());
    Approach(65.);
    const double Before=Pawn->GetActorLocation().Z;
    for(int32 Frame=0;Frame<60;++Frame)Move(Climb,1);
    TestTrue(TEXT("a steep positive slope supports climbing"),Movement->IsRockClimbing() && Pawn->GetActorLocation().Z>Before+80);
    TestTrue(TEXT("even a pistol is stowed on rock"),Pawn->IsWeaponStowed());
    const float RockSpeed=Movement->GetMaxSpeed();
    const float LightLadderSpeed=Movement->LadderSpeed();
    Pawn->Weapon->Data->bOneHanded=false;
    const float HeavyLadderSpeed=Movement->LadderSpeed();
    Pawn->Weapon->Data->bOneHanded=true;
    TestTrue(TEXT("rock climbing is slower than both ladder speeds"),
        RockSpeed<LightLadderSpeed && RockSpeed<HeavyLadderSpeed);
    TestTrue(TEXT("climbing movement respects the rock speed cap"),Movement->Velocity.Size()<=RockSpeed+.1);
    const double AtRest=Pawn->GetActorLocation().Z;
    Move(Climb,0);
    TestTrue(TEXT("holding position on rock"),FMath::IsNearlyEqual(Pawn->GetActorLocation().Z,AtRest,1.));
    for(int32 Frame=0;Frame<10;++Frame)Move(Climb,-1);
    TestTrue(TEXT("backward input descends the same face"),Pawn->GetActorLocation().Z<AtRest-15);
    Move(0,0);
    TestTrue(TEXT("releasing climb detaches and restores the weapon"),Movement->IsFalling() && !Pawn->IsWeaponStowed());

    Approach(65.);
    for(int32 Frame=0;Frame<275;++Frame)
    {
        Move(Climb,1);
        if(Movement->IsMovingOnGround() && Pawn->GetActorLocation().X>190.)break;
    }
    TestTrue(TEXT("the pawn walks off the top"),Movement->IsMovingOnGround() && !Pawn->IsWeaponStowed() && Pawn->GetActorLocation().X>190.);

    for(double Angle:{30.,90.,105.})
    {
        Approach(Angle);
        Move(Climb,1);
        TestFalse(*FString::Printf(TEXT("no rock climb on a %.0f degree face"),Angle),Movement->IsRockClimbing());
    }
    Approach(88.);
    Move(Climb,1);
    TestTrue(TEXT("an almost vertical face remains climbable"),Movement->IsRockClimbing());
    Approach(65.,Map->GetLayout().ArenaSize.X*.5-300.);
    Move(Climb,1);
    TestFalse(TEXT("the boundary cannot be climbed"),Movement->IsRockClimbing());
    return !HasAnyErrors();
}
#endif
