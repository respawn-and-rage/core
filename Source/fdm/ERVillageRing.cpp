#include "ERProceduralMap.h"
#include "CompGeom/Delaunay2.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

namespace ERProcedural
{
namespace
{
bool InsideVillageBank(TArrayView<const FVector2D> Polygon,FVector2D Point)
{
    bool Result=false;
    for(int32 I=0,J=Polygon.Num()-1;I<Polygon.Num();J=I++)
    {
        const auto& A=Polygon[J];
        const auto& B=Polygon[I];
        if((A.Y>Point.Y)!=(B.Y>Point.Y) && Point.X<A.X+(B.X-A.X)*(Point.Y-A.Y)/(B.Y-A.Y))
            Result=!Result;
    }
    return Result;
}
}
bool VillageRingContains(const FERProceduralLayout& L,FVector2D Point,bool IncludeDryInterior)
{
    return IncludeDryInterior ? InsideVillageBank(L.VillageRingInner,Point):
        InsideVillageBank(L.VillageRingOuter,Point) && !InsideVillageBank(L.VillageRingInner,Point);
}

const FERVillageStream* VillageStreamAt(const FERProceduralLayout& L,FVector2D Point)
{
    // Each cross-section forms one quad with its neighbor. Test the actual
    // banks rather than the conservative bounds used to reserve space.
    for(const auto& Stream:L.VillageStreams)
        for(int32 I=1;I<Stream.LowerBank.Num();++I)
        {
            const FVector2D Quad[]{Stream.LowerBank[I-1],Stream.LowerBank[I],
                Stream.UpperBank[I],Stream.UpperBank[I-1]};
            if(Point.X<FMath::Min(Quad[0].X,Quad[3].X) || Point.X>FMath::Max(Quad[1].X,Quad[2].X))continue;
            if(InsideVillageBank(Quad,Point))return &Stream;
        }
    return nullptr;
}

bool TriangulateVillageWaterways(FERProceduralLayout& L,double BedHeight)
{
    using namespace UE::Geometry;
    TArray<FVector2D> Points;
    TArray<FIndex2i> Edges;
    TArray<FVector> Vertices;
    auto Add=[&](FVector2D Point,double Height)
    {
        Points.Add(Point);
        return Vertices.Emplace(Point,Height);
    };
    TArray<FVector2D> LevelBands;
    auto AddChannel=[&](const TArray<FVector2D>& Lower,const TArray<FVector2D>& Upper,double Bed,bool Closed)
    {
        const int32 First=Vertices.Num(),Count=Lower.Num();
        double MinY=MAX_dbl,MaxY=-MAX_dbl;
        for(int32 I=0;I<Count;++I)
        {
            Add(Lower[I],VillageGroundHeight(L,Lower[I]));
            Add((Lower[I]+Upper[I])*.5,Bed);
            Add(Upper[I],VillageGroundHeight(L,Upper[I]));
            MinY=FMath::Min3(MinY,Lower[I].Y,Upper[I].Y);
            MaxY=FMath::Max3(MaxY,Lower[I].Y,Upper[I].Y);
        }
        for(int32 I=0;I<Count;++I)
        {
            const int32 A=First+3*I,B=First+3*((I+1)%Count);
            if(Closed || I+1<Count)for(int32 K=0;K<3;++K)Edges.Emplace(A+K,B+K);
            Edges.Emplace(A,A+1);
            Edges.Emplace(A+1,A+2);
        }
        LevelBands.Emplace(MinY,MaxY);
    };
    if(!L.VillageRingInner.IsEmpty())AddChannel(L.VillageRingInner,L.VillageRingOuter,BedHeight,true);
    for(const auto& Stream:L.VillageStreams)
        AddChannel(Stream.LowerBank,Stream.UpperBank,Stream.Surface-Stream.Depth,false);

    // Extend the floor to the outlets and keep the original grades between
    // the flat bands used by the channels.
    const double HX=L.VillageStreams.IsEmpty() ? L.ArenaSize.X*.5+20:L.VillageStreams[0].LowerBank.Last().X;
    const double HY=L.ArenaSize.Y*.5+20;
    TArray<double> Rows{-HY,HY};
    for(const auto& Knot:L.VillageTerrainProfile)
        if(!LevelBands.ContainsByPredicate([&](FVector2D Band){return Knot.X>=Band.X && Knot.X<=Band.Y;}))
            Rows.AddUnique(Knot.X);
    Rows.Sort();
    int32 Previous=INDEX_NONE;
    for(double Y:Rows)
    {
        const double Height=VillageGroundHeight(L,{0,Y});
        const int32 Left=Add({-HX,Y},Height),Right=Add({HX,Y},Height);
        Edges.Emplace(Left,Right);
        if(Previous!=INDEX_NONE && L.VillageStreams.IsEmpty())
        {
            Edges.Emplace(Previous,Left);
            Edges.Emplace(Previous+1,Right);
        }
        Previous=Left;
    }
    FDelaunay2 Triangulation;
    Triangulation.RandomStream.Initialize(0x56494c52);
    if(!Triangulation.Triangulate(Points,Edges))return false;

    L.VillageWaterwayTerrainVertices=MoveTemp(Vertices);
    L.VillageWaterwayTerrainFaces.Reset();
    for(const auto& Face:Triangulation.GetTriangles())
        L.VillageWaterwayTerrainFaces.Emplace(Face.A,Face.C,Face.B);
    return true;
}

namespace
{
void SetWaterwayMesh(UDynamicMeshComponent* Component,const TArray<FVector>& Vertices,const TArray<FIntVector>& Faces)
{
    using namespace UE::Geometry;
    FDynamicMesh3 Surface;
    Surface.EnableAttributes();
    auto* Normals=Surface.Attributes()->PrimaryNormals();
    auto* UVs=Surface.Attributes()->PrimaryUV();
    for(const auto& P:Vertices)
    {
        Surface.AppendVertex(P);
        UVs->AppendElement(FVector2f(P.X/250,P.Y/250));
    }
    for(const auto& Face:Faces)
    {
        const FIndex3i Indices(Face.X,Face.Y,Face.Z);
        const int32 Index=Surface.AppendTriangle(Indices);
        const FVector A=Vertices[Face.X],B=Vertices[Face.Y],C=Vertices[Face.Z];
        const FVector3f Normal(FVector::CrossProduct(C-A,B-A).GetSafeNormal());
        const int32 First=Normals->AppendElement(Normal);
        Normals->AppendElement(Normal);
        Normals->AppendElement(Normal);
        Normals->SetTriangle(Index,FIndex3i(First,First+1,First+2));
        UVs->SetTriangle(Index,Indices);
    }
    Component->SetMesh(MoveTemp(Surface));
}
}

void BuildVillageWaterwaySurface(UDynamicMeshComponent* Component,const FERProceduralLayout& L)
{
    TArray<FVector> Vertices;
    TArray<FIntVector> Faces;
    auto AddChannel=[&](const TArray<FVector2D>& Lower,const TArray<FVector2D>& Upper,double Z,bool Closed)
    {
        const int32 First=Vertices.Num(),Count=Lower.Num();
        for(int32 I=0;I<Count;++I)
        {
            Vertices.Emplace(Lower[I],Z);
            Vertices.Emplace(Upper[I],Z);
            if(!Closed && I+1==Count)continue;
            const int32 A=First+I*2,B=First+((I+1)%Count)*2;
            if(Closed)
            {
                Faces.Emplace(A,B,A+1);
                Faces.Emplace(A+1,B,B+1);
            }
            else
            {
                Faces.Emplace(A,A+1,B);
                Faces.Emplace(A+1,B+1,B);
            }
        }
    };
    if(!L.VillageRingInner.IsEmpty())
        AddChannel(L.VillageRingInner,L.VillageRingOuter,L.PoolWater[0].Max.Z,true);
    for(const auto& Stream:L.VillageStreams)
        AddChannel(Stream.LowerBank,Stream.UpperBank,Stream.Surface,false);
    SetWaterwayMesh(Component,Vertices,Faces);
}

void BuildVillageWaterwayTerrain(UDynamicMeshComponent* Component,const FERProceduralLayout& L)
{
    SetWaterwayMesh(Component,L.VillageWaterwayTerrainVertices,L.VillageWaterwayTerrainFaces);
    Component->UpdateCollision(false);
}
}
