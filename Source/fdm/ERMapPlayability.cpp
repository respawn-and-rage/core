#include "ERMapPlayability.h"
#include "ERProceduralMap.h"
#include "ERLayoutSolids.h"
#include "Algo/Reverse.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonWriter.h"

namespace
{
const TCHAR* const Connected=TEXT("CONNECTED_ZONES");
const TCHAR* const BaseExits=TEXT("BASE_EXIT_PATHS");
const TCHAR* const SpawnSight=TEXT("INITIAL_SPAWN_LOS");
const TCHAR* const Vantage=TEXT("DOMINANT_VANTAGE");
const TCHAR* const Exposure=TEXT("ROUTE_EXPOSURE");
const TCHAR* const Diversity=TEXT("ROUTE_DIVERSITY");
const TCHAR* const CoverAccess=TEXT("COVER_ACCESS");
constexpr float PawnRadius=35,PawnHalfHeight=93;
constexpr float SweepRadius=34,SweepHalfHeight=88;

TArray<TArray<int32>> EdgesOfNodes(const FERTacticalGraph& G)
{
    TArray<TArray<int32>> Out;Out.SetNum(G.Nodes.Num());
    for(int32 I=0;I<G.Edges.Num();++I)if(!G.Edges[I].bBlocked){Out[G.Edges[I].A].Add(I);Out[G.Edges[I].B].Add(I);}
    return Out;
}
TArray<bool> Membership(int32 Count,const TArray<int32>& Set)
{
    TArray<bool> Out;Out.Init(false,Count);for(int32 I:Set)Out[I]=true;return Out;
}
// Breadth-first hops from a set, optionally without one node. Parent is -1 when unreached.
TArray<int32> Hops(const FERTacticalGraph& G,const TArray<TArray<int32>>& Near,const TArray<int32>& From,int32 Without=INDEX_NONE)
{
    TArray<int32> Parent;Parent.Init(INDEX_NONE,G.Nodes.Num());TArray<int32> Queue;
    for(int32 I:From)if(I!=Without && Parent[I]==INDEX_NONE){Parent[I]=I;Queue.Add(I);}
    for(int32 Q=0;Q<Queue.Num();++Q)for(int32 EdgeIndex:Near[Queue[Q]])
    {
        const auto& E=G.Edges[EdgeIndex];const int32 Next=E.A==Queue[Q] ? E.B:E.A;
        if(Next!=Without && Parent[Next]==INDEX_NONE){Parent[Next]=Queue[Q];Queue.Add(Next);}
    }
    return Parent;
}
bool Reaches(const TArray<int32>& Parent,const TArray<int32>& To)
{
    return To.ContainsByPredicate([&](int32 I){return Parent[I]!=INDEX_NONE;});
}

// Known solids of a layout, before any physics exists. A hollow skin or a mesh collider is
// counted in Unknown instead of being guessed: it can only hide more, never less.
using ERLayoutQueries::FLayoutSolids;

// Dense metadata is resampled at turns, slopes and every few metres, keeping graphs small.
TArray<FVector> ThinRoute(const TArray<FVector>& Dense)
{
    TArray<FVector> Out;float Run=0;
    for(int32 I=0;I<Dense.Num();++I)
    {
        if(I)Run+=FVector::Dist(Dense[I-1],Dense[I]);
        const bool Turn=I>0 && I+1<Dense.Num() && FVector::DotProduct((Dense[I]-Dense[I-1]).GetSafeNormal(),(Dense[I+1]-Dense[I]).GetSafeNormal())<.99;
        if(I==0 || I==Dense.Num()-1 || Turn || Run>=400){Out.Add(Dense[I]);Run=0;}
    }
    return Out;
}

// Validated routes become a graph: crossing routes meet, and loose ends join the nearest
// route when a standing pawn fits. Those joins stay unconfirmed until the physical stage.
class FRouteGraph
{
public:
    FRouteGraph(FERTacticalGraph& InGraph,const FERProceduralLayout& InLayout,const FERPlayabilityPolicy& InPolicy,bool InFlatGround):G(InGraph),L(InLayout),P(InPolicy),Solids(InLayout),bFlatGround(InFlatGround && !InLayout.GradedTerrain && InLayout.RoomShell.IsEmpty() && InLayout.UnderpassBreaches.IsEmpty()){}
    void Add(const TArray<FVector>& Path,const FString& Source,EERTacticalMove Move=EERTacticalMove::Walk,bool Confirmed=true,bool Closed=false)
    {
        if(Path.Num()<2)return;
        const auto Points=ThinRoute(Path);
        // Some templates already record both symmetry copies. Re-adding them
        // must not consume the bounded connector search with identical spans.
        for(auto& Existing:Routes)if(Existing.Move==Move && Existing.bClosed==Closed && Existing.Points==Points)
        {Existing.bConfirmed|=Confirmed || Move!=EERTacticalMove::Walk;return;}
        auto& R=Routes.AddDefaulted_GetRef();R.Points=Points;R.Source=Source;R.Move=Move;R.bConfirmed=Confirmed || Move!=EERTacticalMove::Walk;R.bClosed=Closed;
    }
    void AddAll(const TArray<TArray<FVector>>& Paths,const TCHAR* Source,EERTacticalMove Move=EERTacticalMove::Walk,bool Confirmed=true)
    {
        for(int32 I=0;I<Paths.Num();++I)Add(Paths[I],FString::Printf(TEXT("%s%d"),Source,I),Move,Confirmed);
    }
    void AddLadders(const TArray<FERLayoutLadder>& Ladders,const TCHAR* Source)
    {
        for(int32 I=0;I<Ladders.Num();++I)
        {
            const auto& V=Ladders[I];
            Add({V.Frame.TransformPosition({90,0,100}),V.Frame.TransformPosition({-60,0,V.DeckHeight+100})},FString::Printf(TEXT("%s%d"),Source,I),EERTacticalMove::Ladder);
        }
    }
    void Finish(const TArray<FBox>& AlphaZones,const TArray<FBox>& OmegaZones)
    {
        CutCrossings();
        for(int32 I=0;I<Routes.Num();++I)if(!Routes[I].bClosed)
        {
            StitchEnd(Routes[I].Points[0],I,P.LinkRadius,Routes[I].Source);
            StitchEnd(Routes[I].Points.Last(),I,P.LinkRadius,Routes[I].Source);
        }
        for(int32 Team=0;Team<2;++Team)for(const auto& S:Team ? L.OmegaSpawns:L.AlphaSpawns)
            StitchEnd(S.GetLocation(),INDEX_NONE,P.SpawnLinkRadius,FString::Printf(TEXT("spawn%d"),Team));
        for(auto& R:Routes)Emit(R);
        for(int32 Team=0;Team<2;++Team)
        {
            const auto& Starts=Team ? L.OmegaSpawns:L.AlphaSpawns;
            for(int32 I=0;I<Starts.Num();++I)
            {
                auto& V=G.Nodes[Node(Starts[I].GetLocation(),FString::Printf(TEXT("spawn%d.%d"),Team,I))];
                V.Kind=EERTacticalNodeKind::Spawn;V.Team=Team;
            }
        }
        auto ZoneOf=[&](FVector Q)
        {
            for(int32 Team=0;Team<2;++Team)for(const auto& Zone:Team ? OmegaZones:AlphaZones)
                if(Zone.IsInsideOrOnXY(Q) && Q.Z<=Zone.Max.Z)return Team;
            return int32(INDEX_NONE);
        };
        auto StaysInZone=[&](const TArray<FVector>& Path)
        {
            if(Path.IsEmpty())return false;
            const int32 Team=ZoneOf(Path[0]);
            for(int32 I=1;I<Path.Num();++I)
            {
                const FVector A=Path[I-1],Delta=Path[I]-A;
                // Classify every interval cut by a base boundary. Endpoints
                // alone miss a straight or bent connector passing through a base.
                TArray<double> Cuts{0.,1.};
                for(const auto* Zones:{&AlphaZones,&OmegaZones})for(const auto& Zone:*Zones)
                    for(int32 Axis=0;Axis<3;++Axis)if(!FMath::IsNearlyZero(Delta[Axis]))
                        for(double Plane:{Zone.Min[Axis],Zone.Max[Axis]})
                        {
                            const double Along=(Plane-A[Axis])/Delta[Axis];
                            if(Along>0 && Along<1)Cuts.Add(Along);
                        }
                Cuts.Sort();
                for(int32 J=0;J<Cuts.Num();++J)
                {
                    if(ZoneOf(A+Delta*Cuts[J])!=Team)return false;
                    if(J && ZoneOf(A+Delta*((Cuts[J-1]+Cuts[J])*.5))!=Team)return false;
                }
            }
            return true;
        };
        for(const auto& Link:Links)
        {
            const int32 A=Node(Link.A,Link.Source),B=Node(Link.B,Link.Source);
            TArray<FVector> Line{G.Nodes[A].Position};Line.Append(Link.Path);Line.Add(G.Nodes[B].Position);
            // Bounded bases are left through validated routes, never through a guessed join.
            // Merging can move an endpoint across a boundary: inspect the same
            // complete polyline that distance and physical confirmation use.
            if(G.bExitsBounded && !StaysInZone(Line))continue;
            const int32 Edge=Connect(A,B,EERTacticalMove::Walk,Link.Source,false);
            if(Edge!=INDEX_NONE)G.SetPath(Edge,Link.Path);
        }
        for(auto& V:G.Nodes)if(const int32 Team=ZoneOf(V.Position);Team!=INDEX_NONE){V.bBase=true;V.Team=Team;}
    }
    int32 UnknownSolids() const {return Solids.Unknown;}
private:
    struct FCut{int32 Segment;float Along;FVector Point;};
    struct FRoute{TArray<FVector> Points;FString Source;EERTacticalMove Move;bool bConfirmed,bClosed;TArray<FCut> Cuts;};
    struct FLink{FVector A,B;FString Source;TArray<FVector> Path;};
    FERTacticalGraph& G;const FERProceduralLayout& L;const FERPlayabilityPolicy& P;FLayoutSolids Solids;
    bool bFlatGround=false;
    TArray<FRoute> Routes;TArray<FLink> Links;TSet<uint64> Known;
    static bool Walkable(const FRoute& R){return R.Move==EERTacticalMove::Walk || R.Move==EERTacticalMove::Ramp;}
    static int32 SegmentCount(const FRoute& R){return R.Points.Num()-(R.bClosed ? 0:1);}
    static FVector Segment(const FRoute& R,int32 I,FVector& End){End=R.Points[(I+1)%R.Points.Num()];return R.Points[I];}
    int32 Node(FVector Position,const FString& Source)
    {
        for(int32 I=0;I<G.Nodes.Num();++I)
            if(FVector::DistSquared2D(G.Nodes[I].Position,Position)<=FMath::Square(P.MergeRadius) && FMath::Abs(G.Nodes[I].Position.Z-Position.Z)<=P.MergeRadius)return I;
        auto& V=G.Nodes.AddDefaulted_GetRef();V.Position=Position;V.Source=FString::Printf(TEXT("%s.%d"),*Source,G.Nodes.Num()-1);
        V.Kind=Position.Z>220 ? EERTacticalNodeKind::Upper:EERTacticalNodeKind::Waypoint;
        return G.Nodes.Num()-1;
    }
    int32 Connect(int32 A,int32 B,EERTacticalMove Move,const FString& Source,bool Confirmed)
    {
        if(A==B)return INDEX_NONE;
        const uint64 Key=(uint64(FMath::Min(A,B))<<32)|uint32(FMath::Max(A,B));
        if(Known.Contains(Key))
        {
            // The same span may be validated by another route.
            if(Confirmed)for(auto& E:G.Edges)if((E.A==A && E.B==B) || (E.A==B && E.B==A))E.bConfirmed=true;
            return INDEX_NONE;
        }
        Known.Add(Key);return G.AddEdge(A,B,Move,Source,Confirmed);
    }
    void CutCrossings()
    {
        for(int32 I=0;I<Routes.Num();++I)if(Walkable(Routes[I]))for(int32 J=I+1;J<Routes.Num();++J)if(Walkable(Routes[J]))
            for(int32 S=0;S<SegmentCount(Routes[I]);++S)for(int32 T=0;T<SegmentCount(Routes[J]);++T)
            {
                FVector A1,B1,Hit;const FVector A0=Segment(Routes[I],S,A1),B0=Segment(Routes[J],T,B1);
                if(!FMath::SegmentIntersection2D(A0,A1,B0,B1,Hit))continue;
                const float AlongA=FVector::Dist2D(A0,Hit)/FMath::Max(1.,FVector::Dist2D(A0,A1)),AlongB=FVector::Dist2D(B0,Hit)/FMath::Max(1.,FVector::Dist2D(B0,B1));
                const FVector OnA=FMath::Lerp(A0,A1,AlongA),OnB=FMath::Lerp(B0,B1,AlongB);
                if(FMath::Abs(OnA.Z-OnB.Z)>P.MergeRadius)continue;
                Routes[I].Cuts.Add({S,AlongA,OnA});Routes[J].Cuts.Add({T,AlongB,OnA});
            }
    }
    void StitchEnd(FVector End,int32 Self,float Radius,const FString& Source)
    {
        struct FCandidate{float Distance;int32 Route,Segment;float Along;FVector Point;};
        TArray<FCandidate> Candidates;
        auto Ground=[&](FVector Point){return bFlatGround && FMath::Abs(Point.Z-100)<3;};
        auto Candidate=[&](int32 Route,int32 SegmentIndex,float Along,FVector Point)
        {
            const float Distance=FVector::Dist2D(End,Point);
            const bool Floor=Ground(End) && Ground(Point);
            if(Distance<=(Floor ? Radius*2:Radius) && FMath::Abs(Point.Z-End.Z)<=P.MergeRadius+.2f*Distance)
                Candidates.Add({Distance,Route,SegmentIndex,Along,Point});
        };
        for(int32 I=0;I<Routes.Num();++I)if(I!=Self && Walkable(Routes[I]))
            for(int32 S=0;S<SegmentCount(Routes[I]);++S)
            {
                FVector A1;const FVector A0=Segment(Routes[I],S,A1);
                const FVector2D Flat(A1-A0);const double Squared=Flat.SizeSquared();
                const float Along=Squared>0 ? FMath::Clamp(FVector2D::DotProduct(FVector2D(End-A0),Flat)/Squared,0.,1.):0;
                Candidate(I,S,Along,FMath::Lerp(A0,A1,Along));
                if(Ground(End) && Ground(A0) && Ground(A1))
                {if(Along>0)Candidate(I,S,0,A0);if(Along<1)Candidate(I,S,1,A1);}
            }
        // Ladder feet on the continuous primary floor can join each other.
        // Never create a horizontal walk between arbitrary elevated landings.
        if(Ground(End))for(int32 I=0;I<Routes.Num();++I)if(I!=Self && !Walkable(Routes[I]))
            for(int32 J:{0,Routes[I].Points.Num()-1})if(Ground(Routes[I].Points[J]))
                Candidate(I,J==0 ? 0:Routes[I].Points.Num()-2,J==0 ? 0.f:1.f,Routes[I].Points[J]);
        Candidates.Sort([](const FCandidate& A,const FCandidate& B){return A.Distance!=B.Distance ? A.Distance<B.Distance:A.Route!=B.Route ? A.Route<B.Route:A.Segment<B.Segment;});
        // Cover may hide the nearest point of a lane, so a few further spans are tried.
        TArray<int32> Joined;TArray<FVector> TriedPoints;int32 Tries=0;
        for(const auto& C:Candidates)
        {
            if(Joined.Num()>=3 || Tries>=16)break;
            if(Joined.Contains(C.Route))continue;
            if(TriedPoints.ContainsByPredicate([&](const FVector& Point){return Point.Equals(C.Point,.1);}))continue;
            ++Tries;TriedPoints.Add(C.Point);
            TArray<FVector> Walk{End,C.Point};
            if(C.Distance>P.MergeRadius && !Solids.PawnClear(End,C.Point))
            {
                bool Found=false;
                // These templates have a continuous flat primary floor. Test
                // two orthogonal detours around furniture, never through it.
                if(Ground(End) && Ground(C.Point))for(const FVector Corner:{FVector(End.X,C.Point.Y,100),FVector(C.Point.X,End.Y,100)})
                    if(FVector::Dist(End,Corner)+FVector::Dist(Corner,C.Point)<=Radius*2 &&
                       Solids.PawnClear(End,Corner) && Solids.PawnClear(Corner,C.Point))
                    {Walk={End,Corner,C.Point};Found=true;break;}
                if(!Found)continue;
            }
            Routes[C.Route].Cuts.Add({C.Segment,C.Along,C.Point});
            if(C.Distance>P.MergeRadius)Links.Add({End,C.Point,Source+TEXT(".link"),MoveTemp(Walk)});
            Joined.Add(C.Route);
        }
    }
    void Emit(FRoute& R)
    {
        R.Cuts.Sort([](const FCut& A,const FCut& B){return A.Segment!=B.Segment ? A.Segment<B.Segment:A.Along<B.Along;});
        TArray<FVector> Line;int32 Cut=0;
        for(int32 S=0;S<R.Points.Num();++S)
        {
            Line.Add(R.Points[S]);
            for(;Cut<R.Cuts.Num() && R.Cuts[Cut].Segment==S;++Cut)Line.Add(R.Cuts[Cut].Point);
        }
        if(R.bClosed)Line.Add(R.Points[0]);
        int32 Previous=Node(Line[0],R.Source);FVector From=Line[0];
        for(int32 I=1;I<Line.Num();++I)
        {
            const int32 Next=Node(Line[I],R.Source);if(Next==Previous)continue;
            const float Slope=FMath::Abs(Line[I].Z-From.Z)/FMath::Max(1.,FVector::Dist2D(From,Line[I]));
            const auto Move=R.Move!=EERTacticalMove::Walk ? R.Move:Slope>.6f ? EERTacticalMove::Stairs:Slope>.02f ? EERTacticalMove::Ramp:R.Move;
            // A straight sweep cannot climb treads or rungs: each template's pawn probes prove them.
            const int32 Edge=Connect(Previous,Next,Move,R.Source,R.bConfirmed || Move==EERTacticalMove::Stairs || Move==EERTacticalMove::Ladder);
            if(Edge!=INDEX_NONE)G.SetPath(Edge,{From,Line[I]});
            Previous=Next;From=Line[I];
        }
    }
};

void BuildRoomsGraph(const FERProceduralLayout& L,FERTacticalGraph& G)
{
    G.Coverage=EERGraphCoverage::Exact;G.bExitsBounded=true;
    const FLayoutSolids Solids(L);
    auto ClearRoomWalk=[&](FVector A,FVector B)
    {
        if(Solids.Unknown>0 || !Solids.PawnClear(A,B))return false;
        for(const auto& Face:L.RoomShell)
        {
            if(FMath::Abs(FVector::CrossProduct(Face.B-Face.A,Face.C-Face.A).GetSafeNormal().Z)>=.7)continue;
            const FBox Wall=(FBox(ForceInit)+Face.A+Face.B+Face.C+Face.D).ExpandBy(FVector(PawnRadius,PawnRadius,PawnHalfHeight));
            if(FMath::LineBoxIntersection(Wall,A,B,B-A))return false;
        }
        return true;
    };
    G.Note=TEXT("RoomNodes and RoomLinks: the sealed shell allows no other movement");
    const FVector Alpha=L.AlphaSpawns.IsEmpty() ? FVector::ZeroVector:L.AlphaSpawns[0].GetLocation();
    const FVector Omega=L.OmegaSpawns.IsEmpty() ? FVector::ZeroVector:L.OmegaSpawns[0].GetLocation();
    for(int32 I=0;I<L.RoomNodes.Num();++I)
    {
        const auto& R=L.RoomNodes[I];auto& V=G.Nodes.AddDefaulted_GetRef();
        // Room metadata stores floor centres; routes and graph positions store pawn centres.
        V.Position=R.Center+FVector(0,0,100);V.Source=FString::Printf(TEXT("room%d"),I);V.bBase=R.Base;
        V.Kind=R.Junction ? EERTacticalNodeKind::Junction:R.Upper ? EERTacticalNodeKind::Upper:EERTacticalNodeKind::Room;
        // With explicit secondary spawn nodes, retain the original base room
        // in the spawn set as well; it must not disappear from path metrics.
        if(R.Base && L.SpawnZones.Num()>2)V.Kind=EERTacticalNodeKind::Spawn;
        if(R.Base)V.Team=FVector::DistSquared2D(R.Center,Alpha)<=FVector::DistSquared2D(R.Center,Omega) ? 0:1;
    }
    for(int32 I=0;I<L.RoomLinks.Num();++I)
    {
        const auto& E=L.RoomLinks[I];
        const bool Sloped=FMath::Abs(E.End.Z-E.Start.Z)>.02f*FVector::Dist2D(E.Start,E.End);
        const bool Route=L.RoomAccessRoutes.IsValidIndex(I) && L.RoomAccessRoutes[I].Num()>=2;
        const int32 Edge=G.AddEdge(E.A,E.B,Sloped ? EERTacticalMove::Ramp:EERTacticalMove::Walk,FString::Printf(TEXT("link%d"),I),Route,E.Width-2*E.Cut);
        if(Edge!=INDEX_NONE && Route)G.SetPath(Edge,L.RoomAccessRoutes[I]);
    }
    // Each secondary pocket is a separate base in the exact room graph. Join
    // it along the already furnished corridor polyline, never across a wall.
    for(const auto& Zone:L.SpawnZones)if(Zone.FirstStart>0 && Zone.ExitRoute.Num()>1)
    {
        const FVector Join=Zone.ExitRoute.Last();double Best=100.;int32 LinkIndex=INDEX_NONE,Segment=0;
        for(int32 I=0;I<L.RoomLinks.Num() && I<L.RoomAccessRoutes.Num();++I)
        {
            const auto& Path=L.RoomAccessRoutes[I];
            for(int32 J=1;J<Path.Num();++J)
            {
                const double Distance=FVector::Dist(Join,FMath::ClosestPointOnSegment(Join,Path[J-1],Path[J]));
                if(Distance<Best){Best=Distance;LinkIndex=I;Segment=J;}
            }
        }
        auto& Node=G.Nodes.AddDefaulted_GetRef();Node.Position=Zone.Center;Node.Kind=EERTacticalNodeKind::Spawn;Node.Team=Zone.Team;Node.bBase=true;Node.Source=TEXT("spawn-zone");
        if(LinkIndex==INDEX_NONE)
        {
            // A pocket can join a primary circulation route inside a room,
            // rather than a corridor RoomLink. Preserve its validated exit,
            // then connect only to a room centre reachable by a clear sweep.
            int32 Room=INDEX_NONE;double Distance=TNumericLimits<double>::Max();
            for(int32 I=0;I<L.RoomNodes.Num();++I)
            {
                const FVector Center=G.Nodes[I].Position;
                const double Candidate=FVector::DistSquared(Join,Center);
                if(Candidate<Distance && FMath::Abs(Join.Z-Center.Z)<1 && ClearRoomWalk(Join,Center))
                {Room=I;Distance=Candidate;}
            }
            if(Room!=INDEX_NONE)
            {
                const int32 Edge=G.AddEdge(G.Nodes.Num()-1,Room,EERTacticalMove::Walk,TEXT("spawn-zone-room"),false,300);
                if(Edge!=INDEX_NONE){TArray<FVector> Path=Zone.ExitRoute;Path.Add(G.Nodes[Room].Position);G.SetPath(Edge,MoveTemp(Path));}
            }
            continue;
        }
        const auto& Link=L.RoomLinks[LinkIndex];const auto& Route=L.RoomAccessRoutes[LinkIndex];
        const bool TowardA=FVector::DistSquared(Join,Route[0])<FVector::DistSquared(Join,Route.Last());
        const int32 Edge=G.AddEdge(G.Nodes.Num()-1,TowardA ? Link.A:Link.B,EERTacticalMove::Walk,TEXT("spawn-zone-exit"),true,300);
        if(Edge==INDEX_NONE)continue;
        auto& Path=G.Edges[Edge].Path;Path=Zone.ExitRoute;
        if(TowardA){for(int32 J=Segment-1;J>=0;--J)Path.Add(Route[J]);}
        else {for(int32 J=Segment;J<Route.Num();++J)Path.Add(Route[J]);}
        G.SetPath(Edge,MoveTemp(Path));
    }

}

FBox SpawnZone(const TArray<FTransform>& Starts,const FERPlayabilityPolicy& P,const TArray<FBox>& Courts)
{
    FBox Zone(ForceInit);for(const auto& S:Starts)Zone+=S.GetLocation();
    if(!Zone.IsValid)return Zone;
    for(const auto& Court:Courts)if(Court.IsInsideOrOnXY(Zone.GetCenter()))return FBox(Court.Min,FVector(Court.Max.X,Court.Max.Y,Zone.Max.Z+P.BaseMargin));
    return Zone.ExpandBy(FVector(P.BaseMargin,P.BaseMargin,P.BaseMargin));
}

TArray<FBox> SpawnZones(const FERProceduralLayout& L,int32 Team,const FERPlayabilityPolicy& P)
{
    const auto& Bank=Team ? L.OmegaSpawns:L.AlphaSpawns;TArray<FBox> Out;
    for(const auto& Zone:L.SpawnZones)if(Zone.Team==Team)
    {
        TArray<FTransform> Starts;for(int32 I=Zone.FirstStart;I<Zone.FirstStart+Zone.StartCount && I<Bank.Num();++I)Starts.Add(Bank[I]);
        Out.Add(SpawnZone(Starts,P,Zone.FirstStart==0 ? L.WarehouseBaseCourts:TArray<FBox>{}));
    }
    if(Out.IsEmpty())Out.Add(SpawnZone(Bank,P,L.WarehouseBaseCourts));return Out;
}

void BuildRoutesGraph(const FERProceduralLayout& L,EERMapArchetype Archetype,const FERPlayabilityPolicy& P,FERTacticalGraph& G)
{
    const bool Village=Archetype==EERMapArchetype::Village,Fire=Archetype==EERMapArchetype::FireTraining,Alleys=Archetype==EERMapArchetype::MiddleEasternAlleys;
    G.Coverage=EERGraphCoverage::Routes;
    // Courts and walled streets bound a base; open yards and fields do not.
    G.bExitsBounded=Fire || Alleys;
    G.Note=G.bExitsBounded ? TEXT("Validated routes between walls"):TEXT("Validated routes across open ground: free movement exists beside them");
    FRouteGraph Builder(G,L,P,Fire || Village);
    if(!Village && L.Routes.IsValidIndex(1))Builder.Add(L.Routes[1],TEXT("patrol"),EERTacticalMove::Walk,true,true);
    // Village starts may stand on a gallery: its reserved exits lead down the flights.
    if(Village)for(int32 I=0;I<FMath::Min(L.Routes.Num(),2);++I)Builder.Add(L.Routes[I],FString::Printf(TEXT("exit%d"),I));
    Builder.AddAll(L.RoomAccessRoutes,TEXT("access"));
    // Fire Training records one half of its routes; the builder checks both copies.
    if(Fire)for(int32 I=0;I<L.RoomAccessRoutes.Num();++I)
    {
        TArray<FVector> Mirror;for(const auto& Q:L.RoomAccessRoutes[I])Mirror.Emplace(-Q.X,-Q.Y,Q.Z);
        Builder.Add(Mirror,FString::Printf(TEXT("accessturn%d"),I));
    }
    Builder.AddAll(L.CrossingRoutes,TEXT("crossing"));
    Builder.AddAll(L.StairRoutes,TEXT("stairs"),EERTacticalMove::Stairs);
    Builder.AddAll(L.VillagePassages,TEXT("passage"));
    Builder.AddAll(L.VillageSideBypasses,TEXT("bypass"));
    // Metadata that no builder check sweeps: it needs the physical stage.
    // Alley roof routes climb rims and stairs, which a straight sweep cannot follow.
    Builder.AddAll(L.RooftopRoutes,TEXT("roof"),Alleys ? EERTacticalMove::Stairs:EERTacticalMove::Walk,false);
    Builder.AddAll(L.GroundRoutes,TEXT("ground"),EERTacticalMove::Walk,false);
    Builder.Add(L.LowerRoute,TEXT("lower"),EERTacticalMove::Walk,Village);
    // Village records its upper line only as a build reference, through hut walls.
    if(Alleys)Builder.Add(L.UpperRoute,TEXT("upper"),EERTacticalMove::Stairs);
    Builder.AddLadders(L.Ladders,TEXT("ladder"));
    Builder.AddLadders(L.VillageUpperLadders,TEXT("upperladder"));
    Builder.Finish(SpawnZones(L,0,P),SpawnZones(L,1,P));
    if(Builder.UnknownSolids()>0)G.Note+=FString::Printf(TEXT("; %d hollow or mesh solids are unknown before physics"),Builder.UnknownSolids());
}

void NodeSets(const FERTacticalGraph& G,TArray<int32> (&Spawns)[2],TArray<int32> (&Bases)[2])
{
    for(int32 I=0;I<G.Nodes.Num();++I)
    {
        const auto& V=G.Nodes[I];if(V.Team!=0 && V.Team!=1)continue;
        if(V.Kind==EERTacticalNodeKind::Spawn)Spawns[V.Team].Add(I);
        if(V.bBase)Bases[V.Team].Add(I);
    }
    // Sealed rooms hold their starts: the base room itself is the spawn zone.
    for(int32 Team=0;Team<2;++Team)if(Spawns[Team].IsEmpty())Spawns[Team]=Bases[Team];
}

FERPlayabilityMetric& Reset(FERPlayabilityReport& R,const TCHAR* Code,EERPlayabilityStage Stage)
{
    auto* M=R.Find(Code);check(M);
    *M=FERPlayabilityMetric();M->Code=Code;M->Stage=Stage;return *M;
}
void Note(FERPlayabilityMetric& M,const TCHAR* Name,double Value){M.Values.Emplace(Name,Value);}

// Every opposing pair, both directions; no downsampling can hide one exposed start.
template<typename TClear>
void ProbeSpawnSight(const FERProceduralLayout& L,const FERPlayabilityPolicy& P,const FTransform& Frame,FERPlayabilityMetric& M,const TCHAR* Name,int32& Visible,TClear Clear)
{
    const int32 Pairs=L.AlphaSpawns.Num()*L.OmegaSpawns.Num();
    constexpr int32 Stride=1;
    int32 Sampled=0;Visible=0;
    const FVector Samples[]={FVector(0,0,92.5f),FVector(0,0,85),FVector(0,0,P.EyeOffset),FVector(0,0,10),FVector(0,0,-60),FVector(0,0,-90)};
    for(int32 I=0;I<L.AlphaSpawns.Num();I+=Stride)for(int32 J=0;J<L.OmegaSpawns.Num();J+=Stride)
    {
        ++Sampled;
        const FVector A=Frame.TransformPosition(L.AlphaSpawns[I].GetLocation()),B=Frame.TransformPosition(L.OmegaSpawns[J].GetLocation());
        bool Seen=false;
        // Starts are recorded at floor+100; a settled 92.5cm capsule lowers
        // the eye by 7.5cm. Test both heights, including lateral body samples.
        for(float EyeHeight:{P.EyeOffset,P.EyeOffset-7.5f})for(const FVector& Sample:Samples)
            for(const FVector Offset:{FVector::ZeroVector,FVector(-28,0,0),FVector(28,0,0),FVector(0,-28,0),FVector(0,28,0)})
                Seen=Seen || Clear(A+FVector(0,0,EyeHeight),B+Sample+Offset) || Clear(B+FVector(0,0,EyeHeight),A+Sample+Offset);
        if(!Seen)continue;
        ++Visible;
        if(M.Violations.Num()<8)
        {
            auto& V=M.Violations.AddDefaulted_GetRef();V.Message=FString::Printf(TEXT("%s: alpha start %d and omega start %d"),Name,I,J);
            V.Positions={L.AlphaSpawns[I].GetLocation(),L.OmegaSpawns[J].GetLocation()};
        }
    }
    Note(M,TEXT("pairs_total"),Pairs);Note(M,TEXT("pairs_sampled"),Sampled);
}

void EvaluateLayoutSight(FERPlayabilityReport& R,const FERProceduralLayout& L,const FLayoutSolids& Solids,const FERPlayabilityPolicy& P)
{
    auto& M=Reset(R,SpawnSight,EERPlayabilityStage::Layout);
    if(L.AlphaSpawns.IsEmpty() || L.OmegaSpawns.IsEmpty()){M.Reason=TEXT("layout has no paired starts");return;}
    int32 Visible=0;
    ProbeSpawnSight(L,P,FTransform::Identity,M,TEXT("layout sight line"),Visible,[&](FVector A,FVector B){return Solids.SightClear(A,B);});
    Note(M,TEXT("visible_pairs"),Visible);Note(M,TEXT("unknown_solids"),Solids.Unknown);
    if(Visible==0)
    {
        M.Status=EERPlayabilityStatus::Pass;
        M.Reason=TEXT("Known solids block every sampled pair. Ballistic cover needs the physical stage");
    }
    else if(Solids.Unknown>0)
    {
        for(auto& V:M.Violations)V.Severity=EERPlayabilityStatus::NotEvaluated;
        M.Reason=TEXT("Unknown solids may block these lines: decided by the physical stage");
    }
    else
    {
        M.Status=EERPlayabilityStatus::Warn;
        M.Reason=TEXT("Opposing starts see each other through known solids");
    }
}

// Cheapest walk between two sets, without banned nodes. Path holds its nodes, From end first.
float Cheapest(const FERTacticalGraph& G,const TArray<int32>& From,const TArray<int32>& To,const TArray<bool>* Banned,TArray<int32>* Path)
{
    const auto Near=EdgesOfNodes(G);TArray<float> Best;Best.Init(MAX_flt,G.Nodes.Num());
    TArray<int32> Parent;Parent.Init(INDEX_NONE,G.Nodes.Num());
    using FStep=TPair<float,int32>;TArray<FStep> Heap;
    auto Earlier=[](const FStep& A,const FStep& B){return A.Key!=B.Key ? A.Key<B.Key:A.Value<B.Value;};
    for(int32 I:From){Best[I]=0;Parent[I]=I;Heap.HeapPush(FStep(0,I),Earlier);}
    const auto InTo=Membership(G.Nodes.Num(),To);
    while(!Heap.IsEmpty())
    {
        FStep Step;Heap.HeapPop(Step,Earlier,EAllowShrinking::No);
        if(Step.Key>Best[Step.Value])continue;
        if(InTo[Step.Value])
        {
            if(Path){int32 At=Step.Value;for(;Parent[At]!=At;At=Parent[At])Path->Add(At);Path->Add(At);Algo::Reverse(*Path);}
            return Step.Key;
        }
        for(int32 EdgeIndex:Near[Step.Value])
        {
            const auto& E=G.Edges[EdgeIndex];const int32 Next=E.A==Step.Value ? E.B:E.A;
            if(Banned && (*Banned)[Next])continue;
            const float Length=Step.Key+E.Length;
            if(Length<Best[Next]){Best[Next]=Length;Parent[Next]=Step.Value;Heap.HeapPush(FStep(Length,Next),Earlier);}
        }
    }
    return -1;
}
// Use distinct, spatially distributed firing positions, retaining paired samples.
TArray<int32> VantageNodes(const FERTacticalGraph& G,const TArray<int32> (&Spawns)[2],const FERPlayabilityPolicy& P)
{
    double Floor=TNumericLimits<double>::Max();
    for(const auto& Team:Spawns)for(int32 I:Team)Floor=FMath::Min(Floor,G.Nodes[I].Position.Z);
    TArray<int32> Candidates;
    for(int32 I=0;I<G.Nodes.Num();++I)if(!G.Nodes[I].bBase &&
        (G.Nodes[I].Kind==EERTacticalNodeKind::Upper || G.Nodes[I].Position.Z>Floor+P.VantageHeight))Candidates.Add(I);
    return ERPlayability::SelectObservers(G,Candidates,P.MaxVantages);
}
TArray<int32> RouteObservers(const FERTacticalGraph& G,const TArray<int32>& Direct,const FERPlayabilityPolicy& P)
{
    TArray<int32> High,Ground;
    const auto Near=EdgesOfNodes(G);
    double Floor=TNumericLimits<double>::Max();
    for(const auto& N:G.Nodes)if(N.bBase)Floor=FMath::Min(Floor,N.Position.Z);
    for(int32 I=0;I<G.Nodes.Num();++I)if(!G.Nodes[I].bBase && !Near[I].IsEmpty() && !Direct.Contains(I))
    {
        if(G.Nodes[I].Kind==EERTacticalNodeKind::Upper || G.Nodes[I].Position.Z>Floor+P.VantageHeight)High.Add(I);
        else Ground.Add(I);
    }
    // Reserve at least half the ray budget for street corners and side routes.
    auto Out=ERPlayability::SelectObservers(G,High,P.MaxObservers/2);
    Out.Append(ERPlayability::SelectObservers(G,Ground,P.MaxObservers-Out.Num()));
    return Out;
}
// The outer end of every edge leaving a base, spatially thinned to the bound.
TArray<int32> ExitNodes(const FERTacticalGraph& G,const TArray<int32>& Base,const FERPlayabilityPolicy& P)
{
    const auto InZone=Membership(G.Nodes.Num(),Base);TArray<int32> All;
    for(const auto& E:G.Edges)if(!E.bBlocked && InZone[E.A]!=InZone[E.B])All.AddUnique(InZone[E.A] ? E.B:E.A);
    All.Sort();
    if(All.Num()<=P.MaxExits)return All;
    return ERPlayability::SelectObservers(G,All,P.MaxExits);
}
// A common polyline supplies distance, exposure and physical confirmation.
TArray<FVector> EdgeLine(const FERTacticalGraph& G,const FERTacticalEdge& E,bool Forward=true)
{
    TArray<FVector> Line{G.Nodes[E.A].Position};
    for(const auto& Q:E.Path)if(!Line.Last().Equals(Q,.01))Line.Add(Q);
    if(!Line.Last().Equals(G.Nodes[E.B].Position,.01))Line.Add(G.Nodes[E.B].Position);
    if(!Forward)Algo::Reverse(Line);
    return Line;
}
TArray<FVector> RouteSamples(const FERTacticalGraph& G,const TArray<int32>& Path,const FERPlayabilityPolicy& P,float& Step)
{
    return ERPlayability::SampleRoute(G,Path,P,Step);
}
// One bounded sight pass over the graph. Clear takes world points, so both stages share it.
template<typename TClear>
void EvaluateRouteSight(FERPlayabilityReport& R,const FERPlayabilityPolicy& P,EERPlayabilityStage Stage,bool Unknown,const FTransform& Frame,TClear Clear)
{
    auto& V=Reset(R,Vantage,Stage);auto& X=Reset(R,Exposure,Stage);
    const auto& G=R.Graph;
    if(G.Coverage==EERGraphCoverage::None){V.Reason=X.Reason=G.Note;return;}
    TArray<int32> Spawns[2],Bases[2];NodeSets(G,Spawns,Bases);
    if(Bases[0].IsEmpty() || Bases[1].IsEmpty()){V.Reason=X.Reason=TEXT("The graph does not place both team bases");return;}
    const FVector Eye(0,0,P.EyeOffset),Chest(0,0,10);
    auto Sees=[&](FVector From,FVector To){return Clear(Frame.TransformPosition(From+Eye),Frame.TransformPosition(To+Chest));};
    const auto Raised=VantageNodes(G,Spawns,P);
    Note(V,TEXT("vantages"),Raised.Num());
    bool Commanded=false;double Worst=0;
    for(int32 Team=0;Team<2;++Team)
    {
        const auto Exits=ExitNodes(G,Bases[Team],P);
        int32 Most=0,Commander=INDEX_NONE;
        for(int32 High:Raised)
        {
            int32 Seen=0;for(int32 Exit:Exits)Seen+=High!=Exit && Sees(G.Nodes[High].Position,G.Nodes[Exit].Position);
            if(Seen>Most){Most=Seen;Commander=High;}
        }
        const double Share=Exits.IsEmpty() ? 0.:double(Most)/Exits.Num();
        Note(V,Team ? TEXT("omega_exit_share"):TEXT("alpha_exit_share"),Share);
        Worst=FMath::Max(Worst,Share);
        if(Exits.Num()>=2 && Most==Exits.Num())
        {
            Commanded=true;
            auto& Item=V.Violations.AddDefaulted_GetRef();Item.Severity=Unknown ? EERPlayabilityStatus::NotEvaluated:EERPlayabilityStatus::Warn;
            Item.Message=FString::Printf(TEXT("One raised position sees every sampled %s base exit"),Team ? TEXT("omega"):TEXT("alpha"));
            Item.Nodes.Add(Commander);Item.Positions.Add(G.Nodes[Commander].Position);
        }
    }
    Note(V,TEXT("worst_exit_share"),Worst);
    if(Raised.IsEmpty()){V.Status=EERPlayabilityStatus::Pass;V.Reason=TEXT("No raised position on the recorded routes");}
    else if(!Commanded)V.Status=EERPlayabilityStatus::Pass;
    else if(Unknown)V.Reason=TEXT("Unknown solids may block these lines: decided by the physical stage");
    else V.Status=EERPlayabilityStatus::Warn;

    TArray<int32> Direct;
    if(Cheapest(G,Bases[0],Bases[1],nullptr,&Direct)<0){X.Reason=TEXT("No walk joins the bases");return;}
    float Step=P.RouteStep;const auto Samples=RouteSamples(G,Direct,P,Step);
    const auto Observers=RouteObservers(G,Direct,P);
    if(Observers.IsEmpty()){X.Reason=TEXT("No sampled firing position outside the measured route");return;}
    TArray<bool> Open;Open.Init(false,Samples.Num());int32 Longest=0;
    for(int32 Watcher:Observers)
    {
        int32 Run=0;
        for(int32 I=0;I<Samples.Num();++I)
        {
            const bool Seen=Sees(G.Nodes[Watcher].Position,Samples[I]);
            Open[I]=Open[I] || Seen;Run=Seen ? Run+1:0;Longest=FMath::Max(Longest,Run);
        }
    }
    int32 Seen=0;for(bool Item:Open)Seen+=Item;
    Note(X,TEXT("samples"),Samples.Num());Note(X,TEXT("observers"),Observers.Num());
    Note(X,TEXT("open_share"),Samples.IsEmpty() ? 0.:double(Seen)/Samples.Num());
    Note(X,TEXT("longest_open_run_m"),FMath::Max(0,Longest-1)*Step/100.);
    X.Status=Unknown ? EERPlayabilityStatus::NotEvaluated:EERPlayabilityStatus::Pass;
    X.Reason=Unknown ? TEXT("Unknown solids can reduce this exposure upper bound: decided by the physical stage"):
        TEXT("Bounded samples from raised positions and ground side routes; no calibrated balance threshold");
}

// Cover is read against the same bounded threats as route exposure, including elevated fire.
// A direct clear approach is a lower-bound local measurement, not a navigation guarantee.
template<typename TSight,typename TWalk>
void EvaluateCoverAccess(FERPlayabilityReport& R,const FERProceduralLayout& L,const FERPlayabilityPolicy& P,
    EERPlayabilityStage Stage,bool Unknown,const FTransform& Frame,TSight SightClear,TWalk WalkClear)
{
    auto& M=Reset(R,CoverAccess,Stage);const auto& G=R.Graph;
    Note(M,TEXT("cover_sites"),L.CoverSites.Num());
    if(L.CoverSites.IsEmpty()){M.Reason=TEXT("No authored hiding sites; buildings and incidental cover are not classified");return;}
    TArray<int32> Spawns[2],Bases[2];NodeSets(G,Spawns,Bases);
    TArray<int32> Direct;
    if(Bases[0].IsEmpty() || Bases[1].IsEmpty() || Cheapest(G,Bases[0],Bases[1],nullptr,&Direct)<0)
    {M.Reason=TEXT("No recorded route between the bases");return;}
    const auto Raised=VantageNodes(G,Spawns,P),Observers=RouteObservers(G,Direct,P);
    if(Observers.IsEmpty()){M.Reason=TEXT("No sampled firing position outside the measured route");return;}
    float Step=P.RouteStep;const auto Samples=RouteSamples(G,Direct,P,Step);
    int32 Exposed=0,Approachable=0,Sheltered=0;double Worst=0;TSet<int32> ExaminedSites;
    auto Sees=[&](int32 Observer,FVector Target)
    {return SightClear(Frame.TransformPosition(G.Nodes[Observer].Position+FVector(0,0,P.EyeOffset)),Frame.TransformPosition(Target));};
    for(const auto& Sample:Samples)
    {
        TArray<int32> Threats;
        for(int32 Observer:Observers)if(Sees(Observer,Sample+FVector(0,0,10)))Threats.Add(Observer);
        if(Threats.IsEmpty())continue;
        ++Exposed;
        TArray<int32> Nearest;
        for(int32 I=0;I<L.CoverSites.Num();++I)if(FVector::Dist(Sample,L.CoverSites[I].Entry)<=P.CoverReach)Nearest.Add(I);
        Nearest.Sort([&](int32 A,int32 B)
        {
            const double DA=FVector::DistSquared(Sample,L.CoverSites[A].Entry),DB=FVector::DistSquared(Sample,L.CoverSites[B].Entry);
            return DA!=DB ? DA<DB:A<B;
        });
        bool Approach=false;
        for(int32 I=0;I<FMath::Min(P.MaxCoverCandidates,Nearest.Num());++I)
        {
            const int32 SiteIndex=Nearest[I];const auto& Site=L.CoverSites[SiteIndex];ExaminedSites.Add(SiteIndex);
            // Preserve the route's floor clearance while adopting the site's
            // authored stance. A standing capsule centred at a crouch entry
            // would penetrate the floor and falsely reject usable low cover.
            const FVector ApproachStart=Sample+FVector(0,0,Site.HalfHeight-PawnHalfHeight);
            if(!WalkClear(Frame.TransformPosition(ApproachStart),Frame.TransformPosition(Site.Entry),Site.HalfHeight) ||
               !WalkClear(Frame.TransformPosition(Site.Entry),Frame.TransformPosition(Site.Hide),Site.HalfHeight))continue;
            Approach=true;bool Protected=true;
            for(int32 Threat:Threats)if(Sees(Threat,Site.SightTo) || Sees(Threat,Site.Hide+FVector(0,0,10)))
            {Protected=false;break;}
            if(!Protected)continue;
            ++Sheltered;Worst=FMath::Max(Worst,FVector::Dist(ApproachStart,Site.Entry)+FVector::Dist(Site.Entry,Site.Hide));break;
        }
        Approachable+=Approach;
    }
    int32 RaisedExposed=0;
    for(int32 I:ExaminedSites)for(int32 High:Raised)if(Sees(High,L.CoverSites[I].SightTo))
    {++RaisedExposed;break;}
    Note(M,TEXT("samples"),Samples.Num());Note(M,TEXT("exposed_samples"),Exposed);Note(M,TEXT("observers"),Observers.Num());
    Note(M,TEXT("clear_approach_samples"),Approachable);Note(M,TEXT("sheltered_samples"),Sheltered);
    Note(M,TEXT("sheltered_share"),Exposed>0 ? double(Sheltered)/Exposed:-1.);
    Note(M,TEXT("max_sheltered_approach_m"),Worst/100.);Note(M,TEXT("sites_examined"),ExaminedSites.Num());
    Note(M,TEXT("sites_exposed_from_height"),RaisedExposed);Note(M,TEXT("search_radius_m"),P.CoverReach/100.);
    M.Status=Unknown ? EERPlayabilityStatus::NotEvaluated:EERPlayabilityStatus::Pass;
    M.Reason=Unknown ? TEXT("Unknown solids may change sight and local access: decided by the physical stage"):
        TEXT("Nearest authored sites with a clear direct approach, sheltered from sampled threats; no navigation or balance verdict");
}

double Rounded(double Value){return FMath::RoundToDouble(Value*100)/100;}
const TCHAR* StageName(EERPlayabilityStage Stage){return Stage==EERPlayabilityStage::Physical ? TEXT("physical"):TEXT("layout");}
const TCHAR* MoveName(EERTacticalMove Move){return Move==EERTacticalMove::Ladder ? TEXT("ladder"):Move==EERTacticalMove::Stairs ? TEXT("stairs"):Move==EERTacticalMove::Ramp ? TEXT("ramp"):TEXT("walk");}
const TCHAR* KindName(EERTacticalNodeKind Kind)
{
    switch(Kind)
    {
    case EERTacticalNodeKind::Spawn:return TEXT("spawn");
    case EERTacticalNodeKind::Room:return TEXT("room");
    case EERTacticalNodeKind::Junction:return TEXT("junction");
    case EERTacticalNodeKind::Upper:return TEXT("upper");
    default:return TEXT("waypoint");
    }
}
const TCHAR* CoverageName(EERGraphCoverage Coverage){return Coverage==EERGraphCoverage::Exact ? TEXT("exact"):Coverage==EERGraphCoverage::Routes ? TEXT("routes"):TEXT("none");}
double Metres(double Cm){return FMath::RoundToDouble(Cm)/100;}
template<typename TWriter>
void WritePosition(TWriter& W,FVector P)
{
    W->WriteArrayStart();W->WriteValue(Metres(P.X));W->WriteValue(Metres(P.Y));W->WriteValue(Metres(P.Z));W->WriteArrayEnd();
}
}

int32 FERTacticalGraph::AddEdge(int32 A,int32 B,EERTacticalMove Move,const FString& Source,bool Confirmed,float Width)
{
    if(A==B || !Nodes.IsValidIndex(A) || !Nodes.IsValidIndex(B))return INDEX_NONE;
    auto& E=Edges.AddDefaulted_GetRef();E.A=A;E.B=B;E.Move=Move;E.Source=Source;E.bConfirmed=Confirmed;E.Width=Width;
    E.Length=FVector::Dist(Nodes[A].Position,Nodes[B].Position);
    return Edges.Num()-1;
}
void FERTacticalGraph::SetPath(int32 Edge,TArray<FVector> Path)
{
    if(!Edges.IsValidIndex(Edge))return;
    auto& E=Edges[Edge];E.Path=MoveTemp(Path);E.Length=0;
    const auto Line=EdgeLine(*this,E);
    for(int32 I=1;I<Line.Num();++I)E.Length+=FVector::Dist(Line[I-1],Line[I]);
}
TArray<FVector> ERPlayability::SampleRoute(const FERTacticalGraph& G,const TArray<int32>& Path,const FERPlayabilityPolicy& P,float& Step)
{
    TArray<FVector> Line;
    for(int32 I=1;I<Path.Num();++I)
    {
        const auto* E=G.Edges.FindByPredicate([&](const FERTacticalEdge& Edge)
            {return !Edge.bBlocked && ((Edge.A==Path[I-1] && Edge.B==Path[I]) || (Edge.B==Path[I-1] && Edge.A==Path[I]));});
        if(!E)continue;
        for(const auto& Q:EdgeLine(G,*E,E->A==Path[I-1]))if(Line.IsEmpty() || !Line.Last().Equals(Q,.01))Line.Add(Q);
    }
    float Total=0;for(int32 I=1;I<Line.Num();++I)Total+=FVector::Dist(Line[I-1],Line[I]);
    Step=FMath::Max(P.RouteStep,Total/FMath::Max(1,P.MaxRouteSamples-1));
    TArray<FVector> Out;if(Step<=0)return Out;
    float Next=0,Run=0;
    for(int32 I=1;I<Line.Num();++I)
    {
        const float Length=FVector::Dist(Line[I-1],Line[I]);
        while(Next<=Run+Length && Out.Num()<P.MaxRouteSamples)
        {Out.Add(FMath::Lerp(Line[I-1],Line[I],Length>0 ? (Next-Run)/Length:0.f));Next+=Step;}
        Run+=Length;
    }
    return Out;
}
TArray<int32> ERPlayability::SelectObservers(const FERTacticalGraph& G,const TArray<int32>& Candidates,int32 Limit)
{
    TArray<int32> Pending=Candidates;
    Pending.Sort([&](int32 A,int32 B)
    {
        const FVector P=G.Nodes[A].Position,Q=G.Nodes[B].Position;
        return P.X!=Q.X ? P.X<Q.X:P.Y!=Q.Y ? P.Y<Q.Y:P.Z!=Q.Z ? P.Z<Q.Z:A<B;
    });
    TArray<TArray<int32>> Groups;TSet<int32> Used;
    for(int32 I:Pending)if(!Used.Contains(I))
    {
        TArray<int32> Group{I};Used.Add(I);
        if(G.Symmetry!=EERTemplateSymmetry::None)
        {
            const FVector P=G.Nodes[I].Position,Other=G.Symmetry==EERTemplateSymmetry::Mirror ? FVector(P.X,-P.Y,P.Z):FVector(-P.X,-P.Y,P.Z);
            int32 Mate=INDEX_NONE;double Best=FMath::Square(60.);
            if(!P.Equals(Other,1.))for(int32 J:Pending)if(!Used.Contains(J))
            {
                const double Distance=FVector::DistSquared(G.Nodes[J].Position,Other);
                if(Distance<Best){Best=Distance;Mate=J;}
            }
            if(Mate!=INDEX_NONE){Group.Add(Mate);Used.Add(Mate);}
        }
        Groups.Add(MoveTemp(Group));
    }
    TArray<int32> Out;
    while(Out.Num()<Limit)
    {
        int32 Best=INDEX_NONE;double BestDistance=-1;
        for(int32 I=0;I<Groups.Num();++I)if(!Groups[I].IsEmpty() && Out.Num()+Groups[I].Num()<=Limit)
        {
            double Distance=TNumericLimits<double>::Max();
            for(int32 Node:Groups[I])
            {
                if(Out.IsEmpty())Distance=FMath::Min(Distance,G.Nodes[Node].Position.SizeSquared());
                else for(int32 Selected:Out)Distance=FMath::Min(Distance,FVector::DistSquared(G.Nodes[Node].Position,G.Nodes[Selected].Position));
            }
            if(Distance>BestDistance){BestDistance=Distance;Best=I;}
        }
        if(Best==INDEX_NONE)break;
        Out.Append(Groups[Best]);Groups[Best].Reset();
    }
    return Out;
}
double FERPlayabilityMetric::Value(const TCHAR* Name,double Default) const
{
    for(const auto& V:Values)if(V.Key==Name)return V.Value;
    return Default;
}
FERPlayabilityMetric* FERPlayabilityReport::Find(const TCHAR* Code)
{
    return Metrics.FindByPredicate([&](const FERPlayabilityMetric& M){return M.Code==Code;});
}
const FERPlayabilityMetric* FERPlayabilityReport::Find(const TCHAR* Code) const
{
    return Metrics.FindByPredicate([&](const FERPlayabilityMetric& M){return M.Code==Code;});
}
EERPlayabilityStatus FERPlayabilityReport::Overall() const
{
    bool Warned=false,Passed=false;
    for(const auto& M:Metrics)
    {
        if(M.Status==EERPlayabilityStatus::Fail)return EERPlayabilityStatus::Fail;
        Warned=Warned || M.Status==EERPlayabilityStatus::Warn || M.Status==EERPlayabilityStatus::NotEvaluated;
        Passed=Passed || M.Status==EERPlayabilityStatus::Pass;
    }
    return Warned ? EERPlayabilityStatus::Warn:Passed ? EERPlayabilityStatus::Pass:EERPlayabilityStatus::NotEvaluated;
}
int32 FERPlayabilityReport::UnevaluatedMetrics() const
{
    int32 Count=0;for(const auto& M:Metrics)Count+=M.Status==EERPlayabilityStatus::NotEvaluated;return Count;
}

EERPlayabilityMode ERPlayability::Mode()
{
#if UE_BUILD_SHIPPING
    return EERPlayabilityMode::Off;
#else
    static const EERPlayabilityMode Cached=[]
    {
        FString Value;
        return FParse::Value(FCommandLine::Get(),TEXT("ERMapPlayability="),Value) && Value==TEXT("ReportOnly") ? EERPlayabilityMode::ReportOnly:EERPlayabilityMode::Off;
    }();
    return Cached;
#endif
}
bool ERPlayability::InitialSpawnsOccluded(const FERProceduralLayout& L)
{
    if(L.AlphaSpawns.IsEmpty() || L.OmegaSpawns.IsEmpty())return false;
    FLayoutSolids Solids(L,L.Panels,true);
    // All rays lie in this box. Cull unrelated perimeter/slab volumes once,
    // rather than rescanning them for every body sample of every spawn pair.
    FBox BeamBounds(ForceInit);
    for(const auto* Bank:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& Start:*Bank)BeamBounds+=Start.GetLocation();
    BeamBounds=BeamBounds.ExpandBy(FVector(35,35,100));
    Solids.Solids.RemoveAll([&](const FLayoutSolids::FSolid& Solid){return !Solid.bSight || !Solid.Bounds.Intersect(BeamBounds);});
    FERPlayabilityMetric Metric;int32 Visible=0;
    ProbeSpawnSight(L,FERPlayabilityPolicy(),FTransform::Identity,Metric,TEXT("initial spawn exposure"),Visible,
        [&](FVector A,FVector B){return Solids.SightClear(A,B);});
    return Visible==0;
}
bool ERPlayability::InitialSpawnsOccludedInWorld(UWorld* World,const FTransform& Frame,const FERProceduralLayout& L)
{
    if(!World || L.AlphaSpawns.IsEmpty() || L.OmegaSpawns.IsEmpty())return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERInitialSpawnOcclusion),false);
    for(TActorIterator<APawn> It(World);It;++It)Query.AddIgnoredActor(*It);
    for(const auto Channel:{ECC_Visibility,ECC_GameTraceChannel1})
    {
        FERPlayabilityMetric Metric;int32 Visible=0;
        ProbeSpawnSight(L,FERPlayabilityPolicy(),Frame,Metric,TEXT("initial spawn exposure"),Visible,
            [&](FVector A,FVector B){FHitResult Hit;return !World->LineTraceSingleByChannel(Hit,A,B,Channel,Query);});
        if(Visible>0)return false;
    }
    return true;
}
FERPlayabilityPolicy ERPlayability::PolicyFor(EERMapArchetype Archetype,int32 ExpectedTeamSize)
{
    // Measurement parameters only: no archetype has calibrated thresholds yet. Open floors
    // keep stairs and ladders further from any recorded route than walled streets do.
    FERPlayabilityPolicy P;
    if(Archetype==EERMapArchetype::MiddleEasternAlleys)P.LinkRadius=P.SpawnLinkRadius=800;
    else if(Archetype!=EERMapArchetype::Rooms)P.LinkRadius=P.SpawnLinkRadius=2500;
    // Rooms have no low cover, and only Central Building has timber crossings that fall.
    if(Archetype==EERMapArchetype::Rooms)P.Stress=EERStressScenario::CentralBreach|EERStressScenario::HighSightline;
    else if(Archetype!=EERMapArchetype::CentralBuilding)P.Stress=EERStressScenario::All&~EERStressScenario::WalkwayLost;
    return P;
}
TArray<int32> ERPlayability::Components(const FERTacticalGraph& G)
{
    TArray<int32> Out;Out.Init(INDEX_NONE,G.Nodes.Num());
    const auto Near=EdgesOfNodes(G);int32 Next=0;
    for(int32 I=0;I<Out.Num();++I)if(Out[I]==INDEX_NONE)
    {
        const auto Parent=Hops(G,Near,{I});
        for(int32 J=0;J<Out.Num();++J)if(Parent[J]!=INDEX_NONE)Out[J]=Next;
        ++Next;
    }
    return Out;
}
int32 ERPlayability::DisjointPaths(const FERTacticalGraph& G,const TArray<int32>& From,const TArray<int32>& To,int32 Limit)
{
    // Unit vertex capacity through split nodes, so two routes that merge count as one.
    struct FArc{int32 Head,Capacity,Reverse;};
    const int32 N=G.Nodes.Num(),Source=2*N,Sink=2*N+1,Open=Limit+1;
    TArray<TArray<FArc>> Arcs;Arcs.SetNum(2*N+2);
    auto Link=[&](int32 U,int32 V,int32 Capacity)
    {
        const int32 Forward=Arcs[U].Num(),Backward=Arcs[V].Num();
        Arcs[U].Add({V,Capacity,Backward});Arcs[V].Add({U,0,Forward});
    };
    const auto InFrom=Membership(N,From),InTo=Membership(N,To);
    for(int32 I=0;I<N;++I)Link(2*I,2*I+1,InFrom[I] || InTo[I] ? Open:1);
    for(const auto& E:G.Edges)if(!E.bBlocked){Link(2*E.A+1,2*E.B,1);Link(2*E.B+1,2*E.A,1);}
    for(int32 I:From)Link(Source,2*I,Open);
    for(int32 I:To)Link(2*I+1,Sink,Open);
    int32 Flow=0;
    while(Flow<Limit)
    {
        TArray<FIntPoint> Parent;Parent.Init(FIntPoint(INDEX_NONE,INDEX_NONE),Arcs.Num());Parent[Source]=FIntPoint(Source,0);
        TArray<int32> Queue{Source};
        for(int32 Q=0;Q<Queue.Num() && Parent[Sink].X==INDEX_NONE;++Q)
            for(int32 A=0;A<Arcs[Queue[Q]].Num();++A)
            {
                const auto& Arc=Arcs[Queue[Q]][A];
                if(Arc.Capacity>0 && Parent[Arc.Head].X==INDEX_NONE){Parent[Arc.Head]=FIntPoint(Queue[Q],A);Queue.Add(Arc.Head);}
            }
        if(Parent[Sink].X==INDEX_NONE)break;
        for(int32 V=Sink;V!=Source;V=Parent[V].X)
        {
            auto& Arc=Arcs[Parent[V].X][Parent[V].Y];
            --Arc.Capacity;++Arcs[V][Arc.Reverse].Capacity;
        }
        ++Flow;
    }
    return Flow;
}
TArray<int32> ERPlayability::CommonChokes(const FERTacticalGraph& G,const TArray<int32>& From,const TArray<int32>& To)
{
    TArray<int32> Out;const auto Near=EdgesOfNodes(G);const auto Parent=Hops(G,Near,From);
    int32 At=INDEX_NONE;for(int32 I:To)if(Parent[I]!=INDEX_NONE){At=I;break;}
    if(At==INDEX_NONE)return Out;
    // Every common node lies on any one path, so only those need a removal test.
    TArray<int32> Path;for(;Parent[At]!=At;At=Parent[At])Path.Add(At);
    Algo::Reverse(Path);
    const auto InTo=Membership(G.Nodes.Num(),To);
    for(int32 I:Path)if(!InTo[I] && !Reaches(Hops(G,Near,From,I),To))Out.Add(I);
    return Out;
}
float ERPlayability::PathLength(const FERTacticalGraph& G,const TArray<int32>& From,const TArray<int32>& To)
{
    return Cheapest(G,From,To,nullptr,nullptr);
}
float ERPlayability::LateralPathLength(const FERTacticalGraph& G,const TArray<int32>& From,const TArray<int32>& To)
{
    TArray<int32> Direct;if(Cheapest(G,From,To,nullptr,&Direct)<0)return -1;
    TArray<bool> Banned;Banned.Init(false,G.Nodes.Num());
    for(int32 I:Direct)Banned[I]=!From.Contains(I) && !To.Contains(I);
    return Cheapest(G,From,To,&Banned,nullptr);
}

FERTacticalGraph ERPlayability::BuildGraph(const FERProceduralLayout& L,EERMapArchetype Archetype,const FERPlayabilityPolicy& P)
{
    FERTacticalGraph G;G.Symmetry=L.LayoutSymmetry;
    if(Archetype==EERMapArchetype::Rooms)BuildRoomsGraph(L,G);
    else if(Archetype!=EERMapArchetype::LegacyAssembly)BuildRoutesGraph(L,Archetype,P,G);
    else G.Note=TEXT("Legacy assemblies carry no tactical metadata");
    return G;
}
void ERPlayability::EvaluateGraph(FERPlayabilityReport& R,const FERPlayabilityPolicy& P)
{
    auto& C=Reset(R,Connected,R.Stage);auto& X=Reset(R,BaseExits,R.Stage);auto& D=Reset(R,Diversity,R.Stage);
    const auto& G=R.Graph;
    if(G.Coverage==EERGraphCoverage::None){C.Reason=X.Reason=D.Reason=G.Note;return;}
    TArray<int32> Spawns[2],Bases[2];NodeSets(G,Spawns,Bases);
    if(Spawns[0].IsEmpty() || Spawns[1].IsEmpty() || Bases[0].IsEmpty() || Bases[1].IsEmpty())
    {C.Reason=X.Reason=D.Reason=TEXT("The graph does not place both team bases");return;}
    const bool Exact=G.Coverage==EERGraphCoverage::Exact;
    const auto Ids=Components(G);
    int32 Parts=0;for(int32 Id:Ids)Parts=FMath::Max(Parts,Id+1);
    int32 Unconfirmed=0,Blocked=0;
    for(const auto& E:G.Edges){Unconfirmed+=!E.bConfirmed && !E.bBlocked;Blocked+=E.bBlocked;}
    TArray<int32> Isolated;for(int32 I=0;I<Ids.Num();++I)if(Ids[I]!=Ids[Spawns[0][0]])Isolated.Add(I);
    const bool Linked=Ids[Spawns[0][0]]==Ids[Spawns[1][0]];
    const float Crossing=PathLength(G,Spawns[0],Spawns[1]);
    Note(C,TEXT("nodes"),G.Nodes.Num());Note(C,TEXT("edges"),G.Edges.Num());Note(C,TEXT("components"),Parts);
    Note(C,TEXT("isolated_nodes"),Isolated.Num());Note(C,TEXT("edges_unconfirmed"),Unconfirmed);Note(C,TEXT("edges_blocked"),Blocked);
    Note(C,TEXT("base_to_base_m"),Crossing<0 ? -1.:Crossing/100.);
    C.Status=EERPlayabilityStatus::Pass;
    if(!Isolated.IsEmpty())
    {
        auto& V=C.Violations.AddDefaulted_GetRef();
        // Open ground may join what route metadata cannot.
        V.Severity=Exact ? EERPlayabilityStatus::Fail:EERPlayabilityStatus::Warn;
        V.Message=Linked ? TEXT("Nodes unreachable from the alpha base"):TEXT("The two bases are not connected");
        for(int32 I=0;I<FMath::Min(Isolated.Num(),32);++I){V.Nodes.Add(Isolated[I]);V.Positions.Add(G.Nodes[Isolated[I]].Position);}
        C.Status=V.Severity;
    }
    if(C.Status==EERPlayabilityStatus::Pass && Unconfirmed>0)
    {
        C.Status=EERPlayabilityStatus::Warn;
        C.Reason=TEXT("Some edges still need a physical sweep");
    }
    bool Weak=false;
    for(int32 Team=0;Team<2;++Team)
    {
        const TCHAR* Name=Team ? TEXT("omega"):TEXT("alpha");
        const auto InZone=Membership(G.Nodes.Num(),Bases[Team]);
        int32 Exits=0;for(const auto& E:G.Edges)Exits+=!E.bBlocked && InZone[E.A]!=InZone[E.B];
        const int32 Paths=DisjointPaths(G,Bases[Team],Bases[1-Team],P.PathLimit);
        const auto Chokes=CommonChokes(G,Spawns[Team],Bases[1-Team]);
        // Neighbouring common nodes are one physical choke, such as both sides of a door.
        int32 Groups=0;
        for(int32 I=0;I<Chokes.Num();++I)
        {
            const bool Joined=I>0 && G.Edges.ContainsByPredicate([&](const FERTacticalEdge& E){return !E.bBlocked && ((E.A==Chokes[I] && E.B==Chokes[I-1]) || (E.B==Chokes[I] && E.A==Chokes[I-1]));});
            if(Joined){auto& V=X.Violations.Last();V.Nodes.Add(Chokes[I]);V.Positions.Add(G.Nodes[Chokes[I]].Position);continue;}
            ++Groups;auto& V=X.Violations.AddDefaulted_GetRef();
            V.Message=FString::Printf(TEXT("Every %s route crosses this %s"),Name,InZone[Chokes[I]] ? TEXT("point inside the base"):TEXT("point"));
            V.Nodes.Add(Chokes[I]);V.Positions.Add(G.Nodes[Chokes[I]].Position);
        }
        Note(X,*FString::Printf(TEXT("%s_exits"),Name),Exits);
        Note(X,*FString::Printf(TEXT("%s_independent_paths"),Name),Paths);
        Note(X,*FString::Printf(TEXT("%s_chokes"),Name),Groups);
        Weak=Weak || Paths<P.MinIndependentPaths || Groups>0;
    }
    const float Direct=PathLength(G,Bases[0],Bases[1]),Lateral=LateralPathLength(G,Bases[0],Bases[1]);
    Note(D,TEXT("direct_m"),Direct<0 ? -1.:Direct/100.);Note(D,TEXT("lateral_m"),Lateral<0 ? -1.:Lateral/100.);
    Note(D,TEXT("lateral_ratio"),Direct>0 && Lateral>=0 ? Lateral/Direct:-1.);
    if(Direct<0)D.Reason=TEXT("No walk joins the bases");
    else if(Lateral>=0)D.Status=EERPlayabilityStatus::Pass;
    else if(G.bExitsBounded){D.Status=EERPlayabilityStatus::Warn;D.Reason=TEXT("No walk between the bases avoids the direct route");}
    else D.Reason=TEXT("Open ground beside the routes may offer what the graph does not record");
    if(!G.bExitsBounded)
    {
        X.Reason=TEXT("Open ground beside the routes: these counts do not bound the real exits");
        for(auto& V:X.Violations)V.Severity=EERPlayabilityStatus::NotEvaluated;
        return;
    }
    X.Status=Weak || Unconfirmed>0 ? EERPlayabilityStatus::Warn:EERPlayabilityStatus::Pass;
    if(!Weak && Unconfirmed>0)X.Reason=TEXT("Some edges still need a physical sweep");
}
FERPlayabilityReport ERPlayability::Analyze(const FERProceduralLayout& L,const FERProceduralConfig& C,const FERPlayabilityPolicy& P)
{
    FERPlayabilityReport R;R.Seed=C.Seed;R.ExpectedTeamSize=C.ExpectedTeamSize;
    if(const auto* T=C.TemplateAsset.Get()){R.TemplateId=T->TemplateId;R.TemplateRevision=T->Revision;R.Archetype=T->Archetype;}
    for(const TCHAR* Code:{Connected,BaseExits,SpawnSight,TEXT("DOMINANT_VANTAGE"),TEXT("ROUTE_EXPOSURE"),TEXT("ROUTE_DIVERSITY"),TEXT("COVER_ACCESS")})
    {
        auto& M=R.Metrics.AddDefaulted_GetRef();M.Code=Code;M.Reason=TEXT("Planned metric, not computed yet");
    }
    double Clock=FPlatformTime::Seconds();
    auto Lap=[&](const TCHAR* Name){const double Now=FPlatformTime::Seconds();R.TimingsMs.Emplace(Name,(Now-Clock)*1000);Clock=Now;};
    R.Graph=BuildGraph(L,R.Archetype,P);Lap(TEXT("graph"));
    EvaluateGraph(R,P);Lap(TEXT("topology"));
    const FLayoutSolids Solids(L);Lap(TEXT("solids"));
    EvaluateLayoutSight(R,L,Solids,P);Lap(TEXT("spawn_sight"));
    EvaluateRouteSight(R,P,EERPlayabilityStage::Layout,Solids.Unknown>0,FTransform::Identity,[&](FVector A,FVector B){return Solids.SightClear(A,B);});Lap(TEXT("route_sight"));
    EvaluateCoverAccess(R,L,P,EERPlayabilityStage::Layout,Solids.Unknown>0,FTransform::Identity,
        [&](FVector A,FVector B){return Solids.SightClear(A,B);},[&](FVector A,FVector B,float HalfHeight)
        {return !Solids.Solids.ContainsByPredicate([&](const FLayoutSolids::FSolid& S){return S.bPawn && Solids.Crosses(S,A,B,FVector(PawnRadius,PawnRadius,HalfHeight));});});Lap(TEXT("cover_access"));
    return R;
}
void ERPlayability::ConfirmInWorld(FERPlayabilityReport& R,UWorld* World,const FTransform& Frame,const FERProceduralLayout& L,const FERPlayabilityPolicy& P)
{
    if(!World)return;
    R.Stage=EERPlayabilityStage::Physical;
    double Clock=FPlatformTime::Seconds();
    auto Lap=[&](const TCHAR* Name){const double Now=FPlatformTime::Seconds();R.TimingsMs.Emplace(Name,(Now-Clock)*1000);Clock=Now;};
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERPlayability),false);
    for(TActorIterator<APawn> Pawn(World);Pawn;++Pawn)Query.AddIgnoredActor(*Pawn);
    const auto Capsule=FCollisionShape::MakeCapsule(SweepRadius,SweepHalfHeight);
    TArray<int32> Stopped;TArray<FString> Stoppers;
    for(int32 I=0;I<R.Graph.Edges.Num();++I)
    {
        auto& E=R.Graph.Edges[I];
        if(E.Move!=EERTacticalMove::Walk && E.Move!=EERTacticalMove::Ramp)continue;
        const auto Line=EdgeLine(R.Graph,E);
        bool Clear=true;FHitResult Hit;
        // Walking may touch a walkable slope; a wall, ceiling or steep face stops it.
        for(int32 J=1;J<Line.Num() && Clear;++J)
            Clear=!World->SweepSingleByChannel(Hit,Frame.TransformPosition(Line[J-1]),Frame.TransformPosition(Line[J]),FQuat::Identity,ECC_Pawn,Capsule,Query) || Hit.ImpactNormal.Z>=.72;
        E.bConfirmed=Clear;E.bBlocked=!Clear;
        if(!Clear)
        {
            Stopped.Add(I);
            Stoppers.Add(FString::Printf(TEXT("%s at %s"),*GetNameSafe(Hit.GetComponent()),*Frame.InverseTransformPosition(Hit.ImpactPoint).ToCompactString()));
        }
    }
    Lap(TEXT("physical_edges"));
    EvaluateGraph(R,P);
    if(auto* C=R.Find(Connected))
    {
        Note(*C,TEXT("edges_stopped_by_collision"),Stopped.Num());
        for(int32 I=0;I<FMath::Min(Stopped.Num(),16);++I)
        {
            const auto& E=R.Graph.Edges[Stopped[I]];auto& V=C->Violations.AddDefaulted_GetRef();
            V.Message=FString::Printf(TEXT("A standing capsule cannot follow %s: stopped by %s"),*E.Source,*Stoppers[I]);V.Edges.Add(Stopped[I]);
            V.Positions={R.Graph.Nodes[E.A].Position,R.Graph.Nodes[E.B].Position};
        }
    }
    Lap(TEXT("physical_topology"));
    auto& M=Reset(R,SpawnSight,EERPlayabilityStage::Physical);
    if(L.AlphaSpawns.IsEmpty() || L.OmegaSpawns.IsEmpty()){M.Reason=TEXT("layout has no paired starts");return;}
    int32 Sight=0,Ballistic=0;
    ProbeSpawnSight(L,P,Frame,M,TEXT("sight line"),Sight,[&](FVector A,FVector B){FHitResult Hit;return !World->LineTraceSingleByChannel(Hit,A,B,ECC_Visibility,Query);});
    FERPlayabilityMetric Shots;
    ProbeSpawnSight(L,P,Frame,Shots,TEXT("open shot line"),Ballistic,[&](FVector A,FVector B){FHitResult Hit;return !World->LineTraceSingleByChannel(Hit,A,B,ECC_GameTraceChannel1,Query);});
    Note(M,TEXT("visible_pairs"),Sight);Note(M,TEXT("open_shot_pairs"),Ballistic);
    for(const auto& V:Shots.Violations)if(M.Violations.Num()<16)M.Violations.Add(V);
    const bool Exposed=Sight>0 || Ballistic>0;
    M.Status=Exposed ? EERPlayabilityStatus::Warn:EERPlayabilityStatus::Pass;
    M.Reason=Exposed ? TEXT("Opposing starts have an unobstructed line in the real collision"):TEXT("Real collision blocks every sampled pair. A blocked shot may still penetrate");
    Lap(TEXT("physical_spawn_sight"));
    EvaluateRouteSight(R,P,EERPlayabilityStage::Physical,false,Frame,[&](FVector A,FVector B){FHitResult Hit;return !World->LineTraceSingleByChannel(Hit,A,B,ECC_Visibility,Query);});Lap(TEXT("physical_route_sight"));
    EvaluateCoverAccess(R,L,P,EERPlayabilityStage::Physical,false,Frame,
        [&](FVector A,FVector B){FHitResult Hit;return !World->LineTraceSingleByChannel(Hit,A,B,ECC_Visibility,Query);},
        [&](FVector A,FVector B,float HalfHeight){return !World->SweepTestByChannel(A,B,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(SweepRadius,HalfHeight),Query);});Lap(TEXT("physical_cover_access"));
}

namespace
{
bool Breakable(const FERCellPanelDesc& P){return !P.bIndestructible && (P.Skin.IsEmpty() || P.bSolidSkin);}
// Where a segment enters each breakable panel, as its share of the segment.
TArray<TPair<float,int32>> PanelsOn(const TArray<FERCellPanelDesc>& Panels,FVector A,FVector B)
{
    TArray<TPair<float,int32>> Out;
    for(int32 I=0;I<Panels.Num();++I)
    {
        const auto& P=Panels[I];if(!Breakable(P))continue;
        const FVector LocalA=P.Transform.InverseTransformPositionNoScale(A),LocalB=P.Transform.InverseTransformPositionNoScale(B);
        FVector Hit,Normal;float Time=0;
        if(FMath::LineExtentBoxIntersection(FBox(FVector::ZeroVector,P.Size),LocalA,LocalB,FVector::ZeroVector,Hit,Normal,Time))Out.Emplace(Time,I);
    }
    Out.Sort([](const TPair<float,int32>& X,const TPair<float,int32>& Y){return X.Key!=Y.Key ? X.Key<Y.Key:X.Value<Y.Value;});
    return Out;
}
// A door-sized breach through every layer of the wall at Share along the segment.
int32 Breach(TArray<FERCellPanelDesc>& Panels,FVector A,FVector B,float Share,TArray<TPair<int32,FBox2D>>& Openings)
{
    const auto On=PanelsOn(Panels,A,B);if(On.IsEmpty())return 0;
    float At=On[0].Key;for(const auto& Item:On)if(FMath::Abs(Item.Key-Share)<FMath::Abs(At-Share))At=Item.Key;
    constexpr float Depth=60,Width=240,Height=220;
    const float Span=FMath::Max(1.,FVector::Dist(A,B));int32 Opened=0;
    for(const auto& Item:On)if(FMath::Abs(Item.Key-At)*Span<=Depth)
    {
        auto& P=Panels[Item.Value];const FVector Local=P.Transform.InverseTransformPositionNoScale(FMath::Lerp(A,B,Item.Key));
        const double Bottom=FMath::Clamp(Local.Z-Height*.5,0.,FMath::Max(0.,P.Size.Z-Height));
        const FBox2D Hole(FVector2D(Local.X-Width*.5,Bottom),FVector2D(Local.X+Width*.5,Bottom+Height));
        P.Holes.Add(Hole);Openings.Emplace(Item.Value,Hole);++Opened;
    }
    return Opened;
}
FVector Middle(const TArray<FTransform>& Starts)
{
    FVector Sum=FVector::ZeroVector;for(const auto& S:Starts)Sum+=S.GetLocation();
    return Starts.IsEmpty() ? Sum:Sum/Starts.Num();
}
double ExitShare(const FERPlayabilityReport& R){const auto* M=R.Find(Vantage);return M ? M->Value(TEXT("worst_exit_share")):0.;}
double RouteOpenShare(const FERPlayabilityReport& R){const auto* M=R.Find(Exposure);return M ? M->Value(TEXT("open_share")):0.;}
int32 VisiblePairs(const FERPlayabilityReport& R){const auto* M=R.Find(SpawnSight);return M ? int32(M->Value(TEXT("visible_pairs"))):0;}
int32 BasePaths(const FERPlayabilityReport& R)
{
    const auto* M=R.Find(BaseExits);
    return M ? FMath::Min(int32(M->Value(TEXT("alpha_independent_paths"))),int32(M->Value(TEXT("omega_independent_paths")))):0;
}
}
TArray<FERStressResult> ERPlayability::Stress(const FERProceduralLayout& L,const FERPlayabilityReport& R,const FERPlayabilityPolicy& P)
{
    TArray<FERStressResult> Out;
    TArray<int32> Spawns[2],Bases[2];NodeSets(R.Graph,Spawns,Bases);
    const bool Placed=!Bases[0].IsEmpty() && !Bases[1].IsEmpty() && !L.AlphaSpawns.IsEmpty() && !L.OmegaSpawns.IsEmpty();
    // Damage only reaches sight and walking through these two inputs.
    auto Run=[&](const TCHAR* Name,const TArray<FERCellPanelDesc>& Panels,const FERTacticalGraph& Graph,const TArray<TPair<int32,FBox2D>>& Openings,const TCHAR* Skip)
    {
        auto& S=Out.AddDefaulted_GetRef();S.Scenario=Name;S.Openings=Openings;
        S.VisiblePairs=VisiblePairs(R);S.Paths=BasePaths(R);S.ExitShare=ExitShare(R);S.OpenShare=RouteOpenShare(R);
        S.VisiblePairsDamaged=S.VisiblePairs;S.PathsDamaged=S.Paths;S.ExitShareDamaged=S.ExitShare;S.OpenShareDamaged=S.OpenShare;
        if(Skip){S.Reason=Skip;return;}
        S.bApplicable=true;
        FERPlayabilityReport Damaged;Damaged.Metrics=R.Metrics;Damaged.Graph=Graph;Damaged.Stage=EERPlayabilityStage::Layout;
        EvaluateGraph(Damaged,P);
        const FLayoutSolids Solids(L,Panels);
        EvaluateLayoutSight(Damaged,L,Solids,P);
        EvaluateRouteSight(Damaged,P,EERPlayabilityStage::Layout,Solids.Unknown>0,FTransform::Identity,[&](FVector A,FVector B){return Solids.SightClear(A,B);});
        S.VisiblePairsDamaged=VisiblePairs(Damaged);S.PathsDamaged=BasePaths(Damaged);S.ExitShareDamaged=ExitShare(Damaged);S.OpenShareDamaged=RouteOpenShare(Damaged);
    };
    if(EnumHasAnyFlags(P.Stress,EERStressScenario::CentralBreach))
    {
        auto Panels=L.Panels;TArray<TPair<int32,FBox2D>> Openings;
        const FVector Lift(0,0,P.EyeOffset);
        if(Placed)Breach(Panels,Middle(L.AlphaSpawns)+Lift,Middle(L.OmegaSpawns)+Lift,.5f,Openings);
        Run(TEXT("central_breach"),Panels,R.Graph,Openings,Openings.IsEmpty() ? TEXT("No breakable wall lies between the bases"):nullptr);
    }
    if(EnumHasAnyFlags(P.Stress,EERStressScenario::CoverRemoved))
    {
        auto Panels=L.Panels;TArray<TPair<int32,FBox2D>> Openings;TArray<int32> Direct;
        if(Placed)Cheapest(R.Graph,Bases[0],Bases[1],nullptr,&Direct);
        float Step=P.RouteStep;const auto Samples=RouteSamples(R.Graph,Direct,P,Step);
        // Whole low panels near the route, base side first: chest cover shot away, walls untouched.
        // Joists, floor plates and slats are thin on Z too, and they carry the structure instead of hiding anyone.
        constexpr float Reach=300,Low=170,Tall=60;constexpr int32 MaxPanels=3;
        for(const FVector& Sample:Samples)for(int32 I=0;I<Panels.Num() && Openings.Num()<MaxPanels;++I)
        {
            auto& Panel=Panels[I];
            if(!Breakable(Panel) || Panel.Size.Z>Low || Panel.Size.Z<Tall || Panel.Holes.Num()>L.Panels[I].Holes.Num())continue;
            const FVector Local=Panel.Transform.InverseTransformPositionNoScale(Sample);
            if(FBox(FVector::ZeroVector,Panel.Size).ComputeSquaredDistanceToPoint(Local)>Reach*Reach)continue;
            const FBox2D Whole(FVector2D(0,0),FVector2D(Panel.Size.X,Panel.Size.Z));
            Panel.Holes.Add(Whole);Openings.Emplace(I,Whole);
        }
        Run(TEXT("cover_removed"),Panels,R.Graph,Openings,Openings.IsEmpty() ? TEXT("No breakable low cover along the direct route"):nullptr);
    }
    if(EnumHasAnyFlags(P.Stress,EERStressScenario::HighSightline))
    {
        auto Panels=L.Panels;TArray<TPair<int32,FBox2D>> Openings;
        if(Placed)
        {
            const FLayoutSolids Solids(L);const FVector Eye(0,0,P.EyeOffset),Chest(0,0,10);
            for(int32 High:VantageNodes(R.Graph,Spawns,P))
            {
                for(int32 Exit:ExitNodes(R.Graph,Bases[0],P))
                {
                    const FVector A=R.Graph.Nodes[High].Position+Eye,B=R.Graph.Nodes[Exit].Position+Chest;
                    if(!Solids.SightClear(A,B))Breach(Panels,A,B,.5f,Openings);
                    if(!Openings.IsEmpty())break;
                }
                if(!Openings.IsEmpty())break;
            }
        }
        Run(TEXT("high_sightline"),Panels,R.Graph,Openings,Openings.IsEmpty() ? TEXT("No breakable wall hides a base exit from a raised node"):nullptr);
    }
    if(EnumHasAnyFlags(P.Stress,EERStressScenario::WalkwayLost))
    {
        FERTacticalGraph Graph=R.Graph;int32 Changed=0;
        // Timber crossings really fall with their supports. Fixed catwalks cannot be lost.
        constexpr float Reach=400;
        if(!L.WallBridges.IsEmpty())for(auto& E:Graph.Edges)
        {
            const bool Near=FVector::Dist(Graph.Nodes[E.A].Position,L.WallBridges[0])<Reach && FVector::Dist(Graph.Nodes[E.B].Position,L.WallBridges[0])<Reach;
            if(Near && !E.bBlocked){E.bBlocked=true;++Changed;}
        }
        Run(TEXT("walkway_lost"),L.Panels,Graph,{},Changed ? nullptr:TEXT("No collapsible crossing on the recorded routes"));
    }
    return Out;
}

FString ERPlayability::ToJson(const FERPlayabilityReport& R,bool bTimings)
{
    FString Out;const auto W=TJsonWriterFactory<>::Create(&Out);
    W->WriteObjectStart();
    W->WriteValue(TEXT("report_version"),ReportVersion);W->WriteValue(TEXT("seed"),R.Seed);W->WriteValue(TEXT("expected_team_size"),R.ExpectedTeamSize);
    W->WriteValue(TEXT("archetype"),ArchetypeName(R.Archetype));W->WriteValue(TEXT("template_id"),R.TemplateId);W->WriteValue(TEXT("template_revision"),R.TemplateRevision);
    W->WriteValue(TEXT("mode"),TEXT("report_only"));W->WriteValue(TEXT("stage"),StageName(R.Stage));W->WriteValue(TEXT("overall"),StatusName(R.Overall()));
    W->WriteValue(TEXT("evaluation_complete"),R.UnevaluatedMetrics()==0);W->WriteValue(TEXT("unevaluated_metrics"),R.UnevaluatedMetrics());
    W->WriteObjectStart(TEXT("graph"));
    W->WriteValue(TEXT("coverage"),CoverageName(R.Graph.Coverage));W->WriteValue(TEXT("exits_bounded"),R.Graph.bExitsBounded);W->WriteValue(TEXT("note"),R.Graph.Note);
    W->WriteArrayStart(TEXT("nodes"));
    for(int32 I=0;I<R.Graph.Nodes.Num();++I)
    {
        const auto& V=R.Graph.Nodes[I];
        W->WriteObjectStart();W->WriteValue(TEXT("id"),I);W->WriteValue(TEXT("kind"),KindName(V.Kind));W->WriteValue(TEXT("source"),V.Source);
        W->WriteValue(TEXT("team"),V.Team);W->WriteValue(TEXT("base"),V.bBase);W->WriteIdentifierPrefix(TEXT("position_m"));WritePosition(W,V.Position);W->WriteObjectEnd();
    }
    W->WriteArrayEnd();
    W->WriteArrayStart(TEXT("edges"));
    for(const auto& E:R.Graph.Edges)
    {
        W->WriteObjectStart();W->WriteValue(TEXT("a"),E.A);W->WriteValue(TEXT("b"),E.B);W->WriteValue(TEXT("move"),MoveName(E.Move));W->WriteValue(TEXT("source"),E.Source);
        W->WriteValue(TEXT("length_m"),Metres(E.Length));W->WriteValue(TEXT("width_m"),Metres(E.Width));W->WriteValue(TEXT("confirmed"),E.bConfirmed);W->WriteValue(TEXT("blocked"),E.bBlocked);W->WriteObjectEnd();
    }
    W->WriteArrayEnd();W->WriteObjectEnd();
    W->WriteArrayStart(TEXT("metrics"));
    for(const auto& M:R.Metrics)
    {
        W->WriteObjectStart();W->WriteValue(TEXT("code"),M.Code);W->WriteValue(TEXT("status"),StatusName(M.Status));W->WriteValue(TEXT("stage"),StageName(M.Stage));W->WriteValue(TEXT("reason"),M.Reason);
        W->WriteObjectStart(TEXT("values"));for(const auto& V:M.Values)W->WriteValue(V.Key,Rounded(V.Value));W->WriteObjectEnd();
        W->WriteArrayStart(TEXT("violations"));
        for(const auto& V:M.Violations)
        {
            W->WriteObjectStart();W->WriteValue(TEXT("severity"),StatusName(V.Severity));W->WriteValue(TEXT("message"),V.Message);
            W->WriteArrayStart(TEXT("nodes"));for(int32 I:V.Nodes)W->WriteValue(I);W->WriteArrayEnd();
            W->WriteArrayStart(TEXT("edges"));for(int32 I:V.Edges)W->WriteValue(I);W->WriteArrayEnd();
            W->WriteArrayStart(TEXT("positions_m"));for(const FVector& P:V.Positions)WritePosition(W,P);W->WriteArrayEnd();
            W->WriteObjectEnd();
        }
        W->WriteArrayEnd();W->WriteObjectEnd();
    }
    W->WriteArrayEnd();
    W->WriteArrayStart(TEXT("stress"));
    for(const auto& S:R.Stress)
    {
        W->WriteObjectStart();W->WriteValue(TEXT("scenario"),S.Scenario);W->WriteValue(TEXT("applicable"),S.bApplicable);W->WriteValue(TEXT("reason"),S.Reason);W->WriteValue(TEXT("openings"),S.Openings.Num());
        W->WriteValue(TEXT("visible_pairs"),S.VisiblePairs);W->WriteValue(TEXT("visible_pairs_damaged"),S.VisiblePairsDamaged);W->WriteValue(TEXT("paths"),S.Paths);W->WriteValue(TEXT("paths_damaged"),S.PathsDamaged);
        W->WriteValue(TEXT("exit_share"),Rounded(S.ExitShare));W->WriteValue(TEXT("exit_share_damaged"),Rounded(S.ExitShareDamaged));W->WriteValue(TEXT("open_share"),Rounded(S.OpenShare));W->WriteValue(TEXT("open_share_damaged"),Rounded(S.OpenShareDamaged));
        W->WriteObjectEnd();
    }
    W->WriteArrayEnd();
    if(bTimings)
    {
        W->WriteObjectStart(TEXT("timings_ms"));for(const auto& T:R.TimingsMs)W->WriteValue(T.Key,Rounded(T.Value));W->WriteObjectEnd();
    }
    W->WriteObjectEnd();W->Close();
    return Out;
}
FString ERPlayability::Summary(const FERPlayabilityReport& R)
{
    FString Text=FString::Printf(TEXT("archetype=%s team=%d seed=%d stage=%s overall=%s evaluation=%s unevaluated=%d"),ArchetypeName(R.Archetype),R.ExpectedTeamSize,R.Seed,StageName(R.Stage),StatusName(R.Overall()),R.UnevaluatedMetrics() ? TEXT("partial"):TEXT("complete"),R.UnevaluatedMetrics());
    for(const auto& M:R.Metrics)if(M.Status!=EERPlayabilityStatus::NotEvaluated || !M.Values.IsEmpty())Text+=FString::Printf(TEXT(" %s=%s"),*M.Code,StatusName(M.Status));
    for(const auto& S:R.Stress)if(S.bApplicable)Text+=FString::Printf(TEXT(" %s=%d->%d"),*S.Scenario,S.VisiblePairs,S.VisiblePairsDamaged);
    return Text;
}
const TCHAR* ERPlayability::ArchetypeName(EERMapArchetype Archetype)
{
    switch(Archetype)
    {
    case EERMapArchetype::CentralBuilding:return TEXT("CentralBuilding");
    case EERMapArchetype::MiddleEasternAlleys:return TEXT("MiddleEasternAlleys");
    case EERMapArchetype::FireTraining:return TEXT("FireTraining");
    case EERMapArchetype::Rooms:return TEXT("Rooms");
    case EERMapArchetype::Village:return TEXT("Village");
    case EERMapArchetype::UrbanTown:return TEXT("Town");
    default:return TEXT("LegacyAssembly");
    }
}
const TCHAR* ERPlayability::StatusName(EERPlayabilityStatus Status)
{
    switch(Status)
    {
    case EERPlayabilityStatus::Pass:return TEXT("pass");
    case EERPlayabilityStatus::Warn:return TEXT("warn");
    case EERPlayabilityStatus::Fail:return TEXT("fail");
    default:return TEXT("not_evaluated");
    }
}
bool ERPlayability::Save(const FERPlayabilityReport& R,const FString& Directory)
{
    IFileManager::Get().MakeDirectory(*Directory,true);
    const FString Name=FString::Printf(TEXT("%s-%dv%d-seed%d-%s.json"),ArchetypeName(R.Archetype),R.ExpectedTeamSize,R.ExpectedTeamSize,R.Seed,StageName(R.Stage));
    return FFileHelper::SaveStringToFile(ToJson(R,true),*(Directory/Name),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
