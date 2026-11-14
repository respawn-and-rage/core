#include "ERProceduralMap.h"
#include "ERLayoutSolids.h"

// Shared audit of a layout on foot, before any physical world exists. Standing places are the
// ground and the top of every solid, sampled every 50cm. A player walks between neighbours,
// steps or jumps up to JumpRise, drops any height and climbs the recorded ladders. A place that
// can be reached from a start and never left again is a trap; a flat rock top or a raised deck
// that cannot be reached, or left, is a ledge the layout promised and did not deliver.
namespace ERProcedural
{
FTraversalAudit AuditTraversal(const FERProceduralLayout& L)
{
    FTraversalAudit Out;
    constexpr double Cell=50,Bucket=200,Radius=35,Stand=96;
    // Rocky's side slopes can be steeper than the trail. Keep the audit below
    // the pawn's walkable grade when checking whether it can leave them.
    const double Walk=L.RockyCoverOnly ? .85:.45;
    const FVector2D Arena(L.ArenaSize),Origin=-Arena*.5;
    const int32 NX=FMath::CeilToInt32(Arena.X/Cell),NY=FMath::CeilToInt32(Arena.Y/Cell);
    if(NX<4 || NY<4 || int64(NX)*NY>4000000 || L.AlphaSpawns.IsEmpty() || L.OmegaSpawns.IsEmpty())return Out;
    const ERLayoutQueries::FLayoutSolids Known(L);
    // A standing capsule against each solid, with its margin in the solid's own axes.
    TArray<FVector> Margins;Margins.Reserve(Known.Solids.Num());
    for(const auto& S:Known.Solids)
    {
        const FQuat Q=S.Frame.GetRotation();FVector Local;
        for(int32 Axis=0;Axis<3;++Axis)
        {
            const FVector Direction=Axis==0 ? Q.GetAxisX():Axis==1 ? Q.GetAxisY():Q.GetAxisZ();
            Local[Axis]=(FMath::Abs(Direction.X)+FMath::Abs(Direction.Y))*Radius+FMath::Abs(Direction.Z)*93;
        }
        Margins.Add(Local);
    }
    const int32 BX=FMath::CeilToInt32(Arena.X/Bucket),BY=FMath::CeilToInt32(Arena.Y/Bucket);
    TArray<TArray<int32>> Buckets;Buckets.SetNum(BX*BY);
    for(int32 I=0;I<Known.Solids.Num();++I)
    {
        const auto& S=Known.Solids[I];if(!S.bPawn)continue;
        const int32 X0=FMath::Clamp(FMath::FloorToInt32((S.Bounds.Min.X-Radius-Origin.X)/Bucket),0,BX-1),X1=FMath::Clamp(FMath::FloorToInt32((S.Bounds.Max.X+Radius-Origin.X)/Bucket),0,BX-1);
        const int32 Y0=FMath::Clamp(FMath::FloorToInt32((S.Bounds.Min.Y-Radius-Origin.Y)/Bucket),0,BY-1),Y1=FMath::Clamp(FMath::FloorToInt32((S.Bounds.Max.Y+Radius-Origin.Y)/Bucket),0,BY-1);
        if(S.Bounds.Max.X+Radius<Origin.X || S.Bounds.Max.Y+Radius<Origin.Y || S.Bounds.Min.X-Radius>-Origin.X || S.Bounds.Min.Y-Radius>-Origin.Y)continue;
        for(int32 Y=Y0;Y<=Y1;++Y)for(int32 X=X0;X<=X1;++X)Buckets[Y*BX+X].Add(I);
    }
    auto Near=[&](FVector2D P)->const TArray<int32>&
    {
        return Buckets[FMath::Clamp(FMath::FloorToInt32((P.Y-Origin.Y)/Bucket),0,BY-1)*BX+FMath::Clamp(FMath::FloorToInt32((P.X-Origin.X)/Bucket),0,BX-1)];
    };
    auto Free=[&](FVector P)
    {
        for(int32 I:Near(FVector2D(P)))
        {
            const auto& S=Known.Solids[I];
            const FVector Local=S.Frame.InverseTransformPositionNoScale(P),Reach=S.Half+Margins[I];
            if(FMath::Abs(Local.X)<Reach.X && FMath::Abs(Local.Y)<Reach.Y && FMath::Abs(Local.Z)<Reach.Z)return false;
        }
        return true;
    };
    auto Center=[&](int32 X,int32 Y){return Origin+FVector2D((X+.5)*Cell,(Y+.5)*Cell);};
    // Standing heights of every cell, lowest first. Ground marks the ones that are bare terrain.
    TArray<int32> First;First.SetNumUninitialized(NX*NY+1);
    TArray<float> Height;TArray<uint8> Ground;TArray<int32> CellOf;
    for(int32 Y=0;Y<NY;++Y)for(int32 X=0;X<NX;++X)
    {
        First[Y*NX+X]=Height.Num();
        const FVector2D P=Center(X,Y);
        if(FMath::Abs(P.X)>Arena.X*.5-Radius || FMath::Abs(P.Y)>Arena.Y*.5-Radius)continue;
        if(L.CircularArena && (P/(Arena*.5-FVector2D(Radius))).SizeSquared()>1)continue;
        const double Floor=GroundHeight(L,P);
        TArray<double,TInlineAllocator<8>> Tops{Floor};
        for(int32 I:Near(P))
        {
            const auto& S=Known.Solids[I];const double Top=S.Bounds.Max.Z;
            if(Top<=Floor+5 || Tops.Num()>=8 || !S.bWalkableTop)continue;
            // A level top needs one local axis upright; the other two span the footprint.
            const FQuat Q=S.Frame.GetRotation();
            const int32 Up=FMath::Abs(Q.GetAxisZ().Z)>.999 ? 2:FMath::Abs(Q.GetAxisY().Z)>.999 ? 1:FMath::Abs(Q.GetAxisX().Z)>.999 ? 0:INDEX_NONE;
            if(Up==INDEX_NONE)continue;
            const FVector Local=S.Frame.InverseTransformPositionNoScale(FVector(P,S.Frame.GetLocation().Z));
            bool Inside=true;
            for(int32 Axis=0;Axis<3;++Axis)if(Axis!=Up)Inside&=FMath::Abs(Local[Axis])<=S.Half[Axis];
            if(Inside && !Tops.ContainsByPredicate([&](double Other){return FMath::Abs(Other-Top)<3;}))Tops.Add(Top);
        }
        Tops.Sort();
        for(double Top:Tops)if(Free(FVector(P,Top+Stand)))
        {Height.Add(float(Top));Ground.Add(FMath::Abs(Top-Floor)<.01 ? 1:0);CellOf.Add(Y*NX+X);}
    }
    First[NX*NY]=Height.Num();
    const int32 Nodes=Height.Num();
    if(Nodes==0)return Out;
    TArray<FIntPoint> Edges;Edges.Reserve(Nodes*4);
    for(int32 A=0;A<Nodes;++A)
    {
        const int32 X=CellOf[A]%NX,Y=CellOf[A]/NX;const double Z=Height[A];const FVector2D P=Center(X,Y);
        for(int32 DY=-2;DY<=2;++DY)for(int32 DX=-2;DX<=2;++DX)
        {
            const int32 Steps=FMath::Max(FMath::Abs(DX),FMath::Abs(DY));
            if(Steps==0 || DX*DX+DY*DY>5)continue;
            const int32 OX=X+DX,OY=Y+DY;
            if(OX<0 || OY<0 || OX>=NX || OY>=NY)continue;
            const FVector2D Q=Center(OX,OY),Mid=(P+Q)*.5;
            int32 Below=INDEX_NONE;
            for(int32 B=First[OY*NX+OX];B<First[OY*NX+OX+1];++B)
            {
                const double Rise=Height[B]-Z;
                // A capsule beside a vertical face can lose the nearest grid sample.
                // Permit a short jump between surface samples, with space over the lip.
                if(Steps>1 && Ground[A] && Ground[B])continue;
                if(Rise<=1){Below=B;continue;}
                // Bare ground steeper than a walkable slope cannot be climbed or landed on.
                if(Rise>JumpRise+1 || (Ground[A] && Ground[B] && Rise>FVector2D::Distance(P,Q)*Walk))continue;
                if(Free(FVector(P,Height[B]+Stand)) && Free(FVector(Mid,Height[B]+Stand)))Edges.Add({A,B});
            }
            // Level, down a slope or off an edge: onto the highest place that is not above this one.
            if(Below!=INDEX_NONE && Free(FVector(Mid,Z+Stand)) && (Z-Height[Below]<=1 || Free(FVector(Q,Z+Stand))))Edges.Add({A,Below});
        }
    }
    auto Find=[&](FVector P,double Tolerance)
    {
        const int32 X=FMath::FloorToInt32((P.X-Origin.X)/Cell),Y=FMath::FloorToInt32((P.Y-Origin.Y)/Cell);
        int32 Best=INDEX_NONE;double Score=TNumericLimits<double>::Max();
        for(int32 DY=-1;DY<=1;++DY)for(int32 DX=-1;DX<=1;++DX)
        {
            if(X+DX<0 || Y+DY<0 || X+DX>=NX || Y+DY>=NY)continue;
            for(int32 B=First[(Y+DY)*NX+X+DX];B<First[(Y+DY)*NX+X+DX+1];++B)
            {
                const double Off=FMath::Abs(Height[B]-P.Z);
                if(Off>Tolerance)continue;
                const double Distance=FVector2D::Distance(Center(X+DX,Y+DY),FVector2D(P))+Off*4;
                if(Distance<Score){Score=Distance;Best=B;}
            }
        }
        return Best;
    };
    for(const auto* Set:{&L.Ladders,&L.VillageUpperLadders})for(const auto& Ladder:*Set)
    {
        const int32 Foot=Find(Ladder.Frame.TransformPositionNoScale(FVector(55,0,0)),40),Head=Find(Ladder.Frame.TransformPositionNoScale(FVector(-100,0,Ladder.DeckHeight)),40);
        if(Foot==INDEX_NONE || Head==INDEX_NONE){++Out.BrokenLadders;continue;}
        Edges.Add({Foot,Head});Edges.Add({Head,Foot});
    }
    // Forward and backward reach from the first start, over the same edges.
    auto Reach=[&](bool Backward,int32 From)
    {
        TArray<int32> Count;Count.Init(0,Nodes+1);
        for(const auto& E:Edges)++Count[(Backward ? E.Y:E.X)+1];
        for(int32 I=0;I<Nodes;++I)Count[I+1]+=Count[I];
        TArray<int32> Fill=Count,To;To.SetNumUninitialized(Edges.Num());
        for(const auto& E:Edges)To[Fill[Backward ? E.Y:E.X]++]=Backward ? E.X:E.Y;
        TArray<uint8> Seen;Seen.Init(0,Nodes);TArray<int32> Queue{From};Seen[From]=1;
        for(int32 I=0;I<Queue.Num();++I)for(int32 E=Count[Queue[I]];E<Count[Queue[I]+1];++E)
            if(!Seen[To[E]]){Seen[To[E]]=1;Queue.Add(To[E]);}
        return Seen;
    };
    auto StartOf=[&](const FTransform& Start){return Find(Start.GetLocation()-FVector(0,0,100),30);};
    const int32 Home=StartOf(L.AlphaSpawns[0]),Away=StartOf(L.OmegaSpawns[0]);
    if(Home==INDEX_NONE || Away==INDEX_NONE)return Out;
    const TArray<uint8> Forward=Reach(false,Home),Back=Reach(true,Home);
    auto Place=[&](int32 Node){return FVector(Center(CellOf[Node]%NX,CellOf[Node]/NX),Height[Node]);};
    for(int32 I=0;I<Nodes;++I)if(Forward[I])
    {
        ++Out.Reached;
        if(!Back[I]){++Out.Trapped;if(Out.TrapSamples.Num()<8)Out.TrapSamples.Add(Place(I));}
    }
    // The other base is part of the same ground: a map split in two has no way back either.
    if(!Forward[Away] || !Back[Away]){++Out.Trapped;if(Out.TrapSamples.Num()<8)Out.TrapSamples.Add(Place(Away));}
    auto Ledge=[&](FVector Top)
    {
        ++Out.Ledges;const int32 Node=Find(Top,3);
        if(Node!=INDEX_NONE && Forward[Node] && Back[Node])return;
        ++Out.LedgesMissed;if(Out.MissedSamples.Num()<8)Out.MissedSamples.Add(Top);
    };
    for(const auto& Deck:L.ElevatedDecks)Ledge(FVector(Deck.GetCenter().X,Deck.GetCenter().Y,Deck.Max.Z));
    return Out;
}
}
