#include "ERProceduralMap.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

namespace ERProcedural
{
float VillageGroundHeight(const FERProceduralLayout& L,FVector2D Position)
{
    const auto& Profile=L.VillageTerrainProfile;
    if(Profile.IsEmpty())return 0;
    const double Y=Position.Y;
    for(int32 I=1;I<Profile.Num();++I)if(Y<=Profile[I].X)
    {
        const auto& A=Profile[I-1];const auto& B=Profile[I];
        return FMath::Lerp(A.Y,B.Y,FMath::Clamp((Y-A.X)/(B.X-A.X),0.,1.));
    }
    return Profile.Last().Y;
}
void PlanVillageTerrain(FERProceduralLayout& L,int32 Seed,float Envelope,const TArray<float>& WallYs)
{
    // A second geometry stream preserves the building choices. Both teams see
    // the same profile, and every east/west cross-section has one elevation.
    FRandomStream Shape(int32(HashCombine(uint32(Seed),0x56475244u)));
    const int32 Variant=uint32(Seed)%3;
    const float Half=L.ArenaSize.Y*.5f,Inner=Envelope+550,Outer=Half-1300,Gap=Outer-Inner;
    if(Variant==0 || Gap<400)return;
    const float Rise=FMath::Min(float(Shape.RandRange(4,9)*20),FMath::FloorToFloat((Gap-280)*.25f/10)*10);
    if(Rise<20)return;
    const float Core=Variant==1 ? Rise:0,Base=Variant==2 ? Rise:0;
    auto At=[&](float Y){return FMath::Lerp(Core,Base,FMath::Clamp((FMath::Abs(Y)-Inner)/Gap,0.f,1.f));};
    auto Add=[&](double Y,double H){L.VillageTerrainProfile.AddUnique({Y,H});};
    Add(-Half-20,Base);Add(-Outer,Base);Add(-Inner,Core);Add(Inner,Core);Add(Outer,Base);Add(Half+20,Base);
    // Side walls and their standing bypasses rest on a short level landing.
    TArray<FVector2D> Landings;
    for(float Y:WallYs)for(float Sign:{-1.f,1.f})
    {
        const float Center=Sign*Y;
        if(FMath::Abs(Center)<=Inner+150 || FMath::Abs(Center)>=Outer-150)continue;
        const FVector2D Range(Center-140,Center+140);
        if(Landings.ContainsByPredicate([&](const FVector2D& Other){return Range.X<Other.Y && Range.Y>Other.X;}))continue;
        Landings.Add(Range);Add(Range.X,At(Center));Add(Range.Y,At(Center));
    }
    L.VillageTerrainProfile.Sort([](const FVector2D& A,const FVector2D& B){return A.X<B.X;});
    L.GradedTerrain=true;
}
void ApplyVillageTerrain(FERProceduralLayout& L)
{
    if(L.VillageTerrainProfile.IsEmpty())return;
    auto Lift=[&](FVector& P){P.Z+=VillageGroundHeight(L,FVector2D(P));};
    auto Frame=[&](FTransform& F){auto P=F.GetLocation();Lift(P);F.SetLocation(P);};
    for(auto& Panel:L.Panels)
    {
        const auto Center=Panel.Transform.TransformPosition(Panel.Size*.5f);
        Panel.Transform.AddToTranslation({0,0,VillageGroundHeight(L,FVector2D(Center))});
    }
    float Rise=0;for(const auto& P:L.VillageTerrainProfile)Rise=FMath::Max(Rise,float(P.Y));
    TArray<FERLayoutFixedPiece> Fixed;
    for(const auto& Original:L.FixedPieces)
    {
        auto Piece=Original;
        const FVector Size=Piece.Transform.GetScale3D()*100,Center=Piece.Transform.GetLocation();
        if(!Piece.VisualOnly && !Piece.CollisionOnly && !Piece.Mesh && Size.X<30 && Size.Y>=L.ArenaSize.Y-.01 && FMath::Abs(Center.X)>L.ArenaSize.X*.5f)
        {
            // Native perimeter rails are rebuilt in rigid spans. Imported
            // meshes, cells and material thicknesses keep their authored size.
            TArray<double> Cuts{-L.ArenaSize.Y*.5,L.ArenaSize.Y*.5};
            for(const auto& Knot:L.VillageTerrainProfile)if(Knot.X>Cuts[0] && Knot.X<Cuts[1])Cuts.AddUnique(Knot.X);
            Cuts.Sort();
            for(int32 I=1;I<Cuts.Num();++I)
            {
                const float A=Cuts[I-1],B=Cuts[I],HA=VillageGroundHeight(L,{0,A}),HB=VillageGroundHeight(L,{0,B});
                auto Span=Piece;const float Length=FMath::Sqrt(FMath::Square(B-A)+FMath::Square(HB-HA));
                Span.Transform=FTransform(FQuat(FVector::XAxisVector,FMath::Atan2(HB-HA,B-A)),{Center.X,(A+B)*.5f,Center.Z+(HA+HB)*.5f},{Size.X/100,Length/100,Size.Z/100});
                Fixed.Add(MoveTemp(Span));
            }
            continue;
        }
        if(!Piece.VisualOnly)
        {
            if(Piece.CollisionOnly)
            {
                auto Scale=Piece.Transform.GetScale3D();Scale.Z+=Rise/100;
                auto P=Piece.Transform.GetLocation();P.Z+=Rise*.5f;Piece.Transform.SetLocation(P);Piece.Transform.SetScale3D(Scale);
            }
            else Frame(Piece.Transform);
        }
        Fixed.Add(MoveTemp(Piece));
    }
    L.FixedPieces=MoveTemp(Fixed);
    for(auto* Bank:{&L.AlphaSpawns,&L.OmegaSpawns})for(auto& F:*Bank)Frame(F);
    for(auto* Boxes:{&L.TownBuildings,&L.ElevatedDecks,&L.VillageSideWalls,&L.VillageFloorHoles,&L.LowCovers})for(auto& Box:*Boxes)
        Box=Box.ShiftBy({0,0,VillageGroundHeight(L,FVector2D(Box.GetCenter()))});
    for(auto& Item:L.VillageBlocks)Frame(Item.Frame);
    for(auto& Item:L.Bases)Frame(Item.Frame);
    for(auto& Item:L.Containers)Frame(Item.Frame);
    for(auto& Item:L.SecondaryStructures)Frame(Item.Frame);
    for(auto& Item:L.Ladders)Frame(Item.Frame);
    for(auto& Item:L.VillageUpperLadders)Frame(Item.Frame);
    for(auto& F:L.DistrictTowers)Frame(F);
    for(auto& Site:L.CoverSites){Frame(Site.Frame);Lift(Site.Hide);Lift(Site.Entry);Lift(Site.SightFrom);Lift(Site.SightTo);}
    auto Path=[&](TArray<FVector>& Points)
    {
        TArray<FVector> Next;
        for(int32 I=0;I<Points.Num();++I)
        {
            if(I>0)
            {
                const auto& A=Points[I-1];const auto& B=Points[I];TArray<double> Fractions;
                if(FMath::Abs(B.Y-A.Y)>.01)for(const auto& Knot:L.VillageTerrainProfile)
                {const double Alpha=(Knot.X-A.Y)/(B.Y-A.Y);if(Alpha>.0001 && Alpha<.9999)Fractions.AddUnique(Alpha);}
                Fractions.Sort();for(double Alpha:Fractions){auto P=FMath::Lerp(A,B,Alpha);Lift(P);Next.Add(P);}
            }
            auto P=Points[I];Lift(P);Next.Add(P);
        }
        Points=MoveTemp(Next);
    };
    for(auto* Paths:{&L.Routes,&L.StairRoutes,&L.RoomAccessRoutes,&L.CrossingRoutes,&L.VillagePassages,&L.VillageSideBypasses,&L.RooftopRoutes,&L.GroundRoutes})for(auto& Route:*Paths)Path(Route);
    Path(L.UpperRoute);Path(L.LowerRoute);Lift(L.Spawn);L.ArenaSize.Z+=Rise;
}
void BuildVillageTerrain(UDynamicMeshComponent* Component,const FERProceduralLayout& L)
{
    using UE::Geometry::FIndex3i;
    UE::Geometry::FDynamicMesh3 Mesh;Mesh.EnableAttributes();
    auto* Normals=Mesh.Attributes()->PrimaryNormals();auto* UVs=Mesh.Attributes()->PrimaryUV();
    const float Half=L.ArenaSize.Y*.5f+20,Width=L.ArenaSize.X*.5f+20;
    TArray<double> Rows;for(double Y=-Half;Y<Half;Y+=75)Rows.Add(Y);Rows.Add(Half);
    for(const auto& Knot:L.VillageTerrainProfile)Rows.AddUnique(Knot.X);Rows.Sort();
    for(int32 I=0;I<Rows.Num();++I)for(float X:{-Width,Width})
    {
        const float Y=Rows[I],H=VillageGroundHeight(L,{X,Y});
        const float DY=(VillageGroundHeight(L,{X,Y+.1f})-VillageGroundHeight(L,{X,Y-.1f}))/.2f;
        Mesh.AppendVertex({X,Y,H});Normals->AppendElement(FVector3f(FVector(0,-DY,1).GetSafeNormal()));UVs->AppendElement(FVector2f(X/250,Y/250));
    }
    auto Triangle=[&](int32 A,int32 B,int32 C)
    {const int32 Index=Mesh.AppendTriangle(A,B,C);Normals->SetTriangle(Index,FIndex3i(A,B,C));UVs->SetTriangle(Index,FIndex3i(A,B,C));};
    for(int32 I=1;I<Rows.Num();++I){const int32 A=(I-1)*2;Triangle(A,A+2,A+1);Triangle(A+1,A+2,A+3);}
    Component->SetMesh(MoveTemp(Mesh));Component->UpdateCollision(false);
}
}
