#include "ERProceduralMap.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

// Graded ground stored as a grid of heights. Any outdoor template can fill the grid; the mesh,
// its collision and every layout query read the same triangles, so a start, a route or a plinth
// stands exactly on what is drawn.
namespace ERProcedural
{
namespace
{
bool FlippedTerrainDiagonal(const FERProceduralLayout& L,int32 Row)
{
    return L.LayoutSymmetry==EERTemplateSymmetry::Mirror && L.TerrainOrigin.Y+Row*L.TerrainCell>=0;
}
}

float TerrainFieldHeight(const FERProceduralLayout& L,FVector2D P)
{
    const int32 NX=L.TerrainColumns,NY=NX>0 ? L.TerrainHeights.Num()/NX:0;
    if(NX<2 || NY<2 || L.TerrainCell<=0)return 0;
    const double GX=FMath::Clamp((P.X-L.TerrainOrigin.X)/L.TerrainCell,0.,double(NX-1)),GY=FMath::Clamp((P.Y-L.TerrainOrigin.Y)/L.TerrainCell,0.,double(NY-1));
    const int32 X=FMath::Min(FMath::FloorToInt32(GX),NX-2),Y=FMath::Min(FMath::FloorToInt32(GY),NY-2);
    const double U=GX-X,V=GY-Y;
    auto At=[&](int32 I,int32 J){return double(L.TerrainHeights[J*NX+I]);};
    if(FlippedTerrainDiagonal(L,Y))
    {
        const double A=At(X,Y),D=At(X+1,Y+1);
        return float(U>=V ? A*(1-U)+At(X+1,Y)*(U-V)+D*V : A*(1-V)+At(X,Y+1)*(V-U)+D*U);
    }
    if(U+V<=1){const double A=At(X,Y);return float(A+(At(X+1,Y)-A)*U+(At(X,Y+1)-A)*V);}
    const double D=At(X+1,Y+1);return float(D+(At(X,Y+1)-D)*(1-U)+(At(X+1,Y)-D)*(1-V));
}

float GroundHeight(const FERProceduralLayout& L,FVector2D P)
{
    if(!L.TerrainHeights.IsEmpty())return TerrainFieldHeight(L,P);
    if(L.CircularArena)return CentralGroundHeight(L,P);
    if(!L.VillageBlocks.IsEmpty())return VillageGroundHeight(L,P);
    return L.GradedTerrain ? AlleyGroundHeight(L,P):0.f;
}

void DrapeRoute(const FERProceduralLayout& L,TArray<FVector>& Path,float Height)
{
    TArray<FVector> Out;
    auto Lift=[&](FVector P){P.Z=GroundHeight(L,FVector2D(P))+Height;return P;};
    for(int32 I=0;I<Path.Num();++I)
    {
        if(I>0)
        {
            // A new point wherever the ground stops following the straight line from the last one.
            const FVector A=Path[I-1],B=Path[I];
            const int32 Steps=FMath::Max(1,FMath::CeilToInt32(FVector::Dist2D(A,B)/25));
            FVector From=Out.Last(),Last=From;
            for(int32 S=1;S<Steps;++S)
            {
                const FVector Here=Lift(FMath::Lerp(A,B,double(S)/Steps));
                const double Run=FVector::Dist2D(From,Here),Expected=From.Z+(Last.Z-From.Z)*(Run/FMath::Max(1.,FVector::Dist2D(From,Last)));
                if(S>1 && FMath::Abs(Here.Z-Expected)>1){Out.Add(Last);From=Last;}
                Last=Here;
            }
        }
        Out.Add(Lift(Path[I]));
    }
    Path=MoveTemp(Out);
}

void BuildTerrainField(UDynamicMeshComponent* Component,const FERProceduralLayout& L,bool RockSurface)
{
    using UE::Geometry::FIndex3i;
    UE::Geometry::FDynamicMesh3 Mesh;Mesh.EnableAttributes();
    auto* Normals=Mesh.Attributes()->PrimaryNormals();auto* UVs=Mesh.Attributes()->PrimaryUV();
    // Sculpt the entire Rocky floor in the same stone used by the crags.
    if(L.RockyCoverOnly && L.BoulderSurface)Component->SetMaterial(0,L.BoulderSurface);
    const bool HasRock=!L.TerrainRockBase.IsEmpty();
    const auto& Heights=HasRock && !RockSurface ? L.TerrainRockBase:L.TerrainHeights;
    const int32 NX=L.TerrainColumns,NY=NX>0 ? Heights.Num()/NX:0;
    auto At=[&](int32 I,int32 J){return double(Heights[FMath::Clamp(J,0,NY-1)*NX+FMath::Clamp(I,0,NX-1)]);};
    for(int32 Y=0;Y<NY;++Y)for(int32 X=0;X<NX;++X)
    {
        const FVector2D P=L.TerrainOrigin+FVector2D(X,Y)*L.TerrainCell;
        const double DX=(At(X+1,Y)-At(X-1,Y))/(2*L.TerrainCell),DY=(At(X,Y+1)-At(X,Y-1))/(2*L.TerrainCell);
        Mesh.AppendVertex(FVector(P,At(X,Y)));Normals->AppendElement(FVector3f(FVector(-DX,-DY,1).GetSafeNormal()));UVs->AppendElement(FVector2f(P/250));
    }
    auto Triangle=[&](int32 A,int32 B,int32 C)
    {
        if(RockSurface && L.TerrainRockBase[A]==Heights[A] &&
            L.TerrainRockBase[B]==Heights[B] && L.TerrainRockBase[C]==Heights[C])return;
        const int32 T=Mesh.AppendTriangle(A,B,C);
        Normals->SetTriangle(T,FIndex3i(A,B,C));UVs->SetTriangle(T,FIndex3i(A,B,C));
    };
    for(int32 Y=0;Y+1<NY;++Y)for(int32 X=0;X+1<NX;++X)
    {
        if(L.CircularArena)
        {
            const FVector2D Min=L.TerrainOrigin+FVector2D(X,Y)*L.TerrainCell;
            const FVector2D Max=Min+FVector2D(L.TerrainCell);
            const FVector2D Closest(FMath::Clamp(0.,Min.X,Max.X),FMath::Clamp(0.,Min.Y,Max.Y));
            if((Closest/(FVector2D(L.ArenaSize)*.5)).SizeSquared()>1)continue;
        }
        const int32 A=Y*NX+X,B=A+1,C=A+NX,D=C+1;
        if(FlippedTerrainDiagonal(L,Y)){Triangle(A,C,D);Triangle(A,D,B);}
        else {Triangle(A,C,B);Triangle(B,C,D);}
    }
    Component->SetMesh(MoveTemp(Mesh));Component->UpdateCollision(false);
}
}
