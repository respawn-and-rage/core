#pragma once
#include "ERProceduralMap.h"

// Shared, read-only collision queries before a physical world exists.
namespace ERLayoutQueries
{
struct FLayoutSolids
{
    struct FSolid{FTransform Frame;FVector Half;FBox Bounds;bool bPawn,bSight;};
    TArray<FSolid> Solids;
    TArray<FVector> Triangles;
    int32 Unknown=0;
    void Add(const FQuat& Rotation,FVector Center,FVector Size,bool Pawn,bool Sight)
    {
        const FTransform Frame(Rotation,Center);const FVector Half=Size*.5;
        Solids.Add({Frame,Half,FBox(-Half,Half).TransformBy(Frame),Pawn,Sight});
    }
    explicit FLayoutSolids(const FERProceduralLayout& L):FLayoutSolids(L,L.Panels){}
    FLayoutSolids(const FERProceduralLayout& L,const TArray<FERCellPanelDesc>& Panels,bool VisibleOnly=false)
    {
        for(const auto& P:L.FixedPieces)
        {
            if(P.VisualOnly || (VisibleOnly && (P.CollisionOnly || P.Mesh || P.Material==EERBlockMaterial::Glass)))continue;
            if(P.MeshCollision){++Unknown;continue;}
            Add(P.Transform.GetRotation(),P.Transform.GetLocation(),P.Transform.GetScale3D()*100,true,!P.PawnBarrier);
        }
        for(const auto& P:Panels)
        {
            if(!P.Skin.IsEmpty() && !P.bSolidSkin){++Unknown;continue;}
            const bool Sight=P.Material!=EERBlockMaterial::Glass;
            TArray<double> X{0,P.Size.X},Z{0,P.Size.Z};
            for(const auto& H:P.Holes)
            {
                X.AddUnique(FMath::Clamp<double>(H.Min.X,0,P.Size.X));X.AddUnique(FMath::Clamp<double>(H.Max.X,0,P.Size.X));
                Z.AddUnique(FMath::Clamp<double>(H.Min.Y,0,P.Size.Z));Z.AddUnique(FMath::Clamp<double>(H.Max.Y,0,P.Size.Z));
            }
            X.Sort();Z.Sort();
            for(int32 I=1;I<X.Num();++I)for(int32 J=1;J<Z.Num();++J)
            {
                const FVector2D Mid((X[I-1]+X[I])*.5,(Z[J-1]+Z[J])*.5);
                if(P.Holes.ContainsByPredicate([&](const FBox2D& H){return H.IsInside(Mid);}))continue;
                Add(P.Transform.GetRotation(),P.Transform.TransformPosition({Mid.X,P.Size.Y*.5,Mid.Y}),{X[I]-X[I-1],P.Size.Y,Z[J]-Z[J-1]},true,Sight);
            }
        }
        for(const auto& F:L.RoomShell)
        {
            Triangles.Append({F.A,F.B,F.C});
            if(!F.C.Equals(F.D,.001))Triangles.Append({F.A,F.C,F.D});
        }
        if(L.GradedTerrain)++Unknown;
    }
    bool Crosses(const FSolid& S,FVector A,FVector B,FVector Margin) const
    {
        if(!S.Bounds.ExpandBy(Margin).Intersect(FBox(ForceInit)+A+B))return false;
        const FVector LocalA=S.Frame.InverseTransformPositionNoScale(A),LocalB=S.Frame.InverseTransformPositionNoScale(B);
        return FMath::LineBoxIntersection(FBox(-S.Half-Margin,S.Half+Margin),LocalA,LocalB,LocalB-LocalA);
    }
    // Standing capsule clearance with its margin in world axes: a rolled panel (a deck
    // or beam) keeps the full vertical margin rather than the margin of its local axes.
    bool PawnClearWorld(FVector A,FVector B) const
    {
        const FVector Margin(35,35,93);
        return !Solids.ContainsByPredicate([&](const FSolid& S)
        {
            if(!S.bPawn)return false;
            const FQuat Q=S.Frame.GetRotation();FVector Local;
            for(int32 Axis=0;Axis<3;++Axis)
            {
                const FVector Direction=Axis==0 ? Q.GetAxisX():Axis==1 ? Q.GetAxisY():Q.GetAxisZ();
                Local[Axis]=FMath::Abs(Direction.X)*Margin.X+FMath::Abs(Direction.Y)*Margin.Y+FMath::Abs(Direction.Z)*Margin.Z;
            }
            if(!S.Bounds.ExpandBy(Margin).Intersect(FBox(ForceInit)+A+B))return false;
            const FVector LocalA=S.Frame.InverseTransformPositionNoScale(A),LocalB=S.Frame.InverseTransformPositionNoScale(B);
            return FMath::LineBoxIntersection(FBox(-S.Half-Local,S.Half+Local),LocalA,LocalB,LocalB-LocalA);
        });
    }
    bool PawnClear(FVector A,FVector B) const
    {
        return !Solids.ContainsByPredicate([&](const FSolid& S){return S.bPawn && Crosses(S,A,B,FVector(35,35,93));});
    }
    bool SightClear(FVector A,FVector B) const
    {
        // Solids shrink by a centimetre: a ray grazing an edge reads as open, as cell collision may
        // let it pass, so the layout stage can only overstate what opposing starts see.
        if(Solids.ContainsByPredicate([&](const FSolid& S){return S.bSight && Crosses(S,A,B,-(FVector(1).ComponentMin(S.Half*.5)));}))return false;
        const FBox Ray=FBox(ForceInit)+A+B;
        for(int32 I=0;I+2<Triangles.Num();I+=3)
        {
            const FVector& T0=Triangles[I];const FVector& T1=Triangles[I+1];const FVector& T2=Triangles[I+2];
            if(!Ray.Intersect(FBox(ForceInit)+T0+T1+T2))continue;
            FVector Point,Normal;if(FMath::SegmentTriangleIntersection(A,B,T0,T1,T2,Point,Normal))return false;
        }
        return true;
    }
};

}
