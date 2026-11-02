#include "ERProceduralMap.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

namespace ERProcedural
{
double PlayableAreaM2(const FERProceduralLayout& L)
{
    return L.CircularArena ? PI*FMath::Square(L.ArenaSize.X*.5)/10000.:L.ArenaSize.X*L.ArenaSize.Y/10000.;
}
namespace
{
constexpr int32 Sectors=256;
float Smooth(float T){T=FMath::Clamp(T,0.f,1.f);return T*T*(3-2*T);}
float Field(const FERProceduralLayout& L,FVector2D P)
{
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
    const int32 Rings=FMath::CeilToInt32(L.ArenaSize.X*.5/75);
    const double Angle=2*PI*Sector/Sectors,R=L.ArenaSize.X*.5*Ring/Rings;
    return FVector2D(FMath::Cos(Angle)*R,FMath::Sin(Angle)*R);
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
    double Angle=FMath::Atan2(P.Y,P.X);if(Angle<0)Angle+=2*PI;
    const int32 Sector=FMath::Min(Sectors-1,FMath::FloorToInt32(Angle*Sectors/(2*PI)));
    const int32 Rings=FMath::CeilToInt32(L.ArenaSize.X*.5/75);
    const FVector2D A=Vertex(L,Rings,Sector),B=Vertex(L,Rings,Sector+1);
    const FVector2D Normal=(A+B).GetSafeNormal();
    const double Fraction=FVector2D::DotProduct(P,Normal)/FVector2D::DotProduct(A,Normal)*Rings;
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
    const int32 Rings=FMath::CeilToInt32(L.ArenaSize.X*.5/75);
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
