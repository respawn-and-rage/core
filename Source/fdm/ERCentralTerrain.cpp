#include "ERProceduralMap.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

namespace ERProcedural
{
double PlayableAreaM2(const FERProceduralLayout& L)
{
    if(!L.RoomNodes.IsEmpty())
    {
        // Count the primary floor of each sealed room and corridor in plan.
        // Galleries, switchbacks and the upper crossing add no area budget.
        double Area=0;
        for(const auto& V:L.RoomNodes)
        {
            if(V.Junction && V.Center.Z>=620)continue;
            double Twice=0;
            for(int32 I=0;I<V.Polygon.Num();++I)
                Twice+=FVector2D::CrossProduct(V.Polygon[I],V.Polygon[(I+1)%V.Polygon.Num()]);
            Area+=FMath::Abs(Twice)*.5;
        }
        for(const auto& E:L.RoomLinks)
        {
            const auto& A=L.RoomNodes[E.A];const auto& B=L.RoomNodes[E.B];
            if((A.Junction && A.Center.Z>=620) || (B.Junction && B.Center.Z>=620) ||
                E.Start.Z>A.Center.Z+.01 || E.End.Z>B.Center.Z+.01)continue;
            Area+=FVector::Dist2D(E.Start,E.End)*(E.Width-2*E.Cut);
        }
        return Area/10000.;
    }
    if(!L.InaccessibleGround.IsEmpty() || !L.InaccessibleGroundPolygons.IsEmpty())
    {
        // Exact XY union of rectangles and polygonal exclusions, clipped to
        // the envelope. Edge crossings split the sweep into linear strips.
        const FVector Half=L.ArenaSize*.5;
        TArray<TArray<FVector>> Polygons=L.InaccessibleGroundPolygons;
        for(const auto& B:L.InaccessibleGround)
            Polygons.Add({{B.Min.X,B.Min.Y,0},{B.Max.X,B.Min.Y,0},{B.Max.X,B.Max.Y,0},{B.Min.X,B.Max.Y,0}});
        TArray<double> Cuts{-Half.X,Half.X};
        struct FEdge {FVector2D A,B;};TArray<FEdge> Edges;
        for(const auto& Polygon:Polygons)for(int32 I=0;I<Polygon.Num();++I)
        {
            Cuts.AddUnique(FMath::Clamp(Polygon[I].X,-Half.X,Half.X));
            Edges.Add({FVector2D(Polygon[I]),FVector2D(Polygon[(I+1)%Polygon.Num()])});
        }
        for(int32 I=0;I<Edges.Num();++I)for(int32 J=I+1;J<Edges.Num();++J)
        {
            const auto& A=Edges[I];const auto& B=Edges[J];
            const FVector2D D=A.B-A.A,E=B.B-B.A;
            if((D.X==0 || D.Y==0) && (E.X==0 || E.Y==0))continue;
            const double Cross=FVector2D::CrossProduct(D,E);if(FMath::Abs(Cross)<1.e-8)continue;
            const double T=FVector2D::CrossProduct(B.A-A.A,E)/Cross,U=FVector2D::CrossProduct(B.A-A.A,D)/Cross;
            if(T>0 && T<1 && U>0 && U<1)Cuts.AddUnique(FMath::Clamp(A.A.X+T*D.X,-Half.X,Half.X));
        }
        Cuts.Sort();double Excluded=0;
        for(int32 I=1;I<Cuts.Num();++I)
        {
            const double X=(Cuts[I-1]+Cuts[I])*.5;TArray<FVector2D> Intervals;
            for(const auto& Polygon:Polygons)
            {
                TArray<double> Hits;
                for(int32 J=0;J<Polygon.Num();++J)
                {
                    const auto& A=Polygon[J];const auto& B=Polygon[(J+1)%Polygon.Num()];
                    if((A.X<X && B.X>X) || (B.X<X && A.X>X))Hits.Add(FMath::Lerp(A.Y,B.Y,(X-A.X)/(B.X-A.X)));
                }
                Hits.Sort();for(int32 J=1;J<Hits.Num();J+=2)
                    Intervals.Add({FMath::Clamp(Hits[J-1],-Half.Y,Half.Y),FMath::Clamp(Hits[J],-Half.Y,Half.Y)});
            }
            Intervals.Sort([](const FVector2D& A,const FVector2D& B){return A.X<B.X;});
            double Span=0,End=-Half.Y;
            for(const auto& V:Intervals){Span+=FMath::Max(0.,V.Y-FMath::Max(End,V.X));End=FMath::Max(End,V.Y);}
            Excluded+=(Cuts[I]-Cuts[I-1])*Span;
        }
        return FMath::Max(0.,L.ArenaSize.X*L.ArenaSize.Y-Excluded)/10000.;
    }
    return L.CircularArena ? PI*L.ArenaSize.X*L.ArenaSize.Y/40000.:L.ArenaSize.X*L.ArenaSize.Y/10000.;
}
namespace
{
constexpr int32 Sectors=256;
float Smooth(float T){T=FMath::Clamp(T,0.f,1.f);return T*T*(3-2*T);}
float Field(const FERProceduralLayout& L,FVector2D P)
{
    for(const auto& Water:L.PoolWater)
    {
        const FVector Center=Water.GetCenter();
        // Keep terrain below the solid pool floor, including triangle edges.
        if(FMath::Abs(P.X-Center.X)<Water.GetExtent().X+45 &&
           FMath::Abs(P.Y-Center.Y)<Water.GetExtent().Y+45)
            return float(Center.Z-100);
    }
    const FVector2D Building(L.CentralBuildingCenter);
    const FVector2D Half(L.CentralBuildingSize.X*.5+180,L.CentralBuildingSize.Y*.5+180);
    const FVector2D Outside(FMath::Max(0.,FMath::Abs(P.X-Building.X)-Half.X),FMath::Max(0.,FMath::Abs(P.Y)-Half.Y));
    const float Distance=Outside.Size();
    float H=L.CentralHill ? L.HillHeight*(1-Smooth(Distance/L.HillFalloff)):
        L.HillHeight*(1-Smooth((FVector2D::Distance(P,FVector2D(L.HillCenter))-L.HillPlateau)/L.HillFalloff))*Smooth(Distance/250);
    // A neighboring blend must never tilt a foundation that is already level.
    for(const auto& Pad:L.TerrainPads)
        if(P.X>=Pad.Min.X && P.X<=Pad.Max.X && P.Y>=Pad.Min.Y && P.Y<=Pad.Max.Y)return Pad.Min.Z;
    // Flat foundations blend back into the hillside; each pad has a mirrored twin.
    float Sum=0,Weights=1.f/40000;
    for(const auto& Pad:L.TerrainPads)
    {
        const FVector2D Near(FMath::Clamp(P.X,Pad.Min.X,Pad.Max.X),FMath::Clamp(P.Y,Pad.Min.Y,Pad.Max.Y));
        const float DistanceToPad=FVector2D::Distance(P,Near);
        if(DistanceToPad<200)
        {
            const float Weight=FMath::Square(1-DistanceToPad/200)/FMath::Square(FMath::Max(.001f,DistanceToPad));
            Sum+=(float(Pad.Min.Z)-H)*Weight;Weights+=Weight;
        }
    }
    return H+Sum/Weights;
}
FVector2D Vertex(const FERProceduralLayout& L,int32 Ring,int32 Sector)
{
    const int32 Rings=FMath::CeilToInt32(FMath::Max(L.ArenaSize.X,L.ArenaSize.Y)*.5/75);
    const double Angle=2*PI*Sector/Sectors;
    return FVector2D(FMath::Cos(Angle)*L.ArenaSize.X*.5*Ring/Rings,
                     FMath::Sin(Angle)*L.ArenaSize.Y*.5*Ring/Rings);
}
float TriangleHeight(const FERProceduralLayout& L,FVector2D P,FVector2D A,FVector2D B,FVector2D C)
{
    const double Den=FVector2D::CrossProduct(B-A,C-A);
    const double U=FVector2D::CrossProduct(P-A,C-A)/Den,V=FVector2D::CrossProduct(B-A,P-A)/Den;
    return Field(L,A)*(1-U-V)+Field(L,B)*U+Field(L,C)*V;
}
}
float CentralGroundHeight(const FERProceduralLayout& L,FVector2D P)
{
    if(P.IsNearlyZero())return Field(L,P);
    const double YScale=L.ArenaSize.X/L.ArenaSize.Y;
    const FVector2D NormalizedP(P.X,P.Y*YScale);
    double Angle=FMath::Atan2(NormalizedP.Y,NormalizedP.X);if(Angle<0)Angle+=2*PI;
    const int32 Sector=FMath::Min(Sectors-1,FMath::FloorToInt32(Angle*Sectors/(2*PI)));
    const int32 Rings=FMath::CeilToInt32(FMath::Max(L.ArenaSize.X,L.ArenaSize.Y)*.5/75);
    const FVector2D A=Vertex(L,Rings,Sector),B=Vertex(L,Rings,Sector+1);
    const FVector2D NormalizedA(A.X,A.Y*YScale),NormalizedB(B.X,B.Y*YScale);
    const FVector2D Normal=(NormalizedA+NormalizedB).GetSafeNormal();
    const double Fraction=FVector2D::DotProduct(NormalizedP,Normal)/FVector2D::DotProduct(NormalizedA,Normal)*Rings;
    const int32 Ring=FMath::Clamp(FMath::FloorToInt32(Fraction),0,Rings-1);
    const auto C=Vertex(L,Ring,Sector),D=Vertex(L,Ring,Sector+1),E=Vertex(L,Ring+1,Sector),F=Vertex(L,Ring+1,Sector+1);
    if(Ring==0)return TriangleHeight(L,P,C,E,F);
    if(Sector>=Sectors/2)
    {
        if(FVector2D::CrossProduct(F-C,P-C)<=0)return TriangleHeight(L,P,C,E,F);
        return TriangleHeight(L,P,C,F,D);
    }
    if(FVector2D::CrossProduct(E-D,P-D)<=0)return TriangleHeight(L,P,C,E,D);
    return TriangleHeight(L,P,D,E,F);
}
void BuildCentralTerrain(UDynamicMeshComponent* Component,const FERProceduralLayout& L)
{
    using UE::Geometry::FIndex3i;
    UE::Geometry::FDynamicMesh3 Terrain;Terrain.EnableAttributes();
    auto* Normals=Terrain.Attributes()->PrimaryNormals();auto* UVs=Terrain.Attributes()->PrimaryUV();
    auto Add=[&](FVector2D P)
    {
        const float DX=(Field(L,P+FVector2D(5,0))-Field(L,P-FVector2D(5,0)))/10;
        const float DY=(Field(L,P+FVector2D(0,5))-Field(L,P-FVector2D(0,5)))/10;
        Terrain.AppendVertex(FVector(P,Field(L,P)));Normals->AppendElement(FVector3f(FVector(-DX,-DY,1).GetSafeNormal()));UVs->AppendElement(FVector2f(P/250));
    };
    Add(FVector2D::ZeroVector);
    const int32 Rings=FMath::CeilToInt32(FMath::Max(L.ArenaSize.X,L.ArenaSize.Y)*.5/75);
    for(int32 Ring=1;Ring<=Rings;++Ring)for(int32 Sector=0;Sector<Sectors;++Sector)Add(Vertex(L,Ring,Sector));
    auto Index=[](int32 Ring,int32 Sector){return Ring==0 ? 0:1+(Ring-1)*Sectors+Sector%Sectors;};
    auto Triangle=[&](int32 A,int32 B,int32 C)
    {const int32 Id=Terrain.AppendTriangle(A,C,B);Normals->SetTriangle(Id,FIndex3i(A,C,B));UVs->SetTriangle(Id,FIndex3i(A,C,B));};
    for(int32 Sector=0;Sector<Sectors;++Sector)Triangle(0,Index(1,Sector),Index(1,Sector+1));
    for(int32 Ring=1;Ring<Rings;++Ring)for(int32 Sector=0;Sector<Sectors;++Sector)
    {
        if(Sector<Sectors/2)
        {Triangle(Index(Ring,Sector),Index(Ring+1,Sector),Index(Ring,Sector+1));Triangle(Index(Ring,Sector+1),Index(Ring+1,Sector),Index(Ring+1,Sector+1));}
        else
        {Triangle(Index(Ring,Sector),Index(Ring+1,Sector),Index(Ring+1,Sector+1));Triangle(Index(Ring,Sector),Index(Ring+1,Sector+1),Index(Ring,Sector+1));}
    }
    Component->SetMesh(MoveTemp(Terrain));Component->UpdateCollision(false);
}
}
