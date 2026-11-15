#include "ERProceduralMap.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

// Boulders are generated, not imported: an icosahedron split once, each of its 42 directions
// pushed in or out by a hash of the boulder's shape seed, scaled to the radii and cut flat on top.
// The world mesh and its collision are the same triangles, so a round stops where the rock is seen.
namespace ERProcedural
{
void BoulderTriangles(const FERLayoutBoulder& Boulder,TArray<FVector>& Out)
{
    const double G=(1+FMath::Sqrt(5.))*.5;
    TArray<FVector> V{{-1,G,0},{1,G,0},{-1,-G,0},{1,-G,0},{0,-1,G},{0,1,G},{0,-1,-G},{0,1,-G},{G,0,-1},{G,0,1},{-G,0,-1},{-G,0,1}};
    TArray<FIntVector> Faces{{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
    for(auto& P:V)P.Normalize();
    TMap<uint32,int32> Mids;
    auto Mid=[&](int32 A,int32 B)
    {
        const uint32 Key=uint32(FMath::Min(A,B))*4096u+uint32(FMath::Max(A,B));
        if(const int32* Known=Mids.Find(Key))return *Known;
        const int32 Index=V.Add((V[A]+V[B]).GetSafeNormal());Mids.Add(Key,Index);return Index;
    };
    TArray<FIntVector> Split;
    for(const auto& F:Faces)
    {
        const int32 A=Mid(F.X,F.Y),B=Mid(F.Y,F.Z),C=Mid(F.Z,F.X);
        Split.Append({{F.X,A,C},{F.Y,B,A},{F.Z,C,B},{A,B,C}});
    }
    const FQuat Turn(FRotator(0,Boulder.Yaw,0));
    for(int32 I=0;I<V.Num();++I)
    {
        const double Push=.84+.26*double(HashCombineFast(Boulder.Shape,uint32(I)*0x9e3779b9u)%1024u)/1023.;
        FVector P=V[I]*Push*Boulder.Radii;
        P.Z=FMath::Min(P.Z,double(Boulder.Radii.Z*BoulderTop));
        if(Boulder.Reflected)P.Y=-P.Y;
        V[I]=Boulder.Center+Turn.RotateVector(P);
    }
    // Every triangle leaves counter-clockwise seen from outside, reflected or not.
    for(const auto& F:Split)
    {
        const bool Outward=FVector::DotProduct(FVector::CrossProduct(V[F.Y]-V[F.X],V[F.Z]-V[F.X]),(V[F.X]+V[F.Y]+V[F.Z])/3-Boulder.Center)>0;
        if(Outward)Out.Append({V[F.X],V[F.Y],V[F.Z]});
        else Out.Append({V[F.X],V[F.Z],V[F.Y]});
    }
}

// Round the footprint of the closed lattice and vary each stratum before
// adding smaller facets. The cross section tapers toward the crown.
void CragTriangles(const FERLayoutCrag& Crag,TArray<FVector>& Out)
{
    const int32 NX=FMath::Clamp(FMath::RoundToInt32(Crag.Half.X/45),4,28),NY=FMath::Clamp(FMath::RoundToInt32(Crag.Half.Y/45),4,22);
    const int32 NZ=FMath::Clamp(FMath::RoundToInt32(Crag.Half.Z/45),3,15);
    const FVector Step(Crag.Half.X*2/NX,Crag.Half.Y*2/NY,Crag.Half.Z*2/NZ);
    const FQuat Turn(FRotator(0,Crag.Yaw,0));
    auto Noise=[&](int32 I,int32 J,int32 K,uint32 Salt)
    {
        return double(HashCombineFast(Crag.Shape,uint32(I)*73856093u^uint32(J)*19349663u^uint32(K)*83492791u^Salt*0x9e3779b9u)%1024u)/1023.;
    };
    auto Point=[&](int32 I,int32 J,int32 K)
    {
        FVector P(-Crag.Half.X+Step.X*I,-Crag.Half.Y+Step.Y*J,-Crag.Half.Z+Step.Z*K);
        const int32 SX=I==0 ? -1:I==NX ? 1:0,SY=J==0 ? -1:J==NY ? 1:0;
        const bool Top=K==NZ,Side=SX!=0 || SY!=0;
        const double U=P.X/Crag.Half.X,V=P.Y/Crag.Half.Y,Level=double(K)/NZ;
        // The upper beds pull back in broad ledges. Preserve a blocky footprint
        // and taper each end differently to show the direction of the strata.
        const double Upper=FMath::Max(0.,2*Level-1);
        const double Taper=1-.10*Level-.12*Upper;
        const double Dip=(Noise(0,0,0,8)-.5)*.105*Level;
        P.X*=Taper*FMath::Sqrt(1-.30*V*V);
        P.Y*=(Taper-Dip*U)*FMath::Sqrt(1-.30*U*U);
        if(Side)
        {
            // Broad offsets vary the strata before the smaller facets break up each face.
            const double Rib=Noise(I/3,J/3,K/2,6);
            const double Push=Crag.Skin*((Top ? .08+.32*Rib:.15+.70*Rib)+
                .28*Noise(I,J,K,1)+.45*(1-Level));
            const FVector2D Away=FVector2D(SX,SY).GetSafeNormal();
            P.X+=Away.X*Push;P.Y+=Away.Y*Push;
            // Slide along the wall so the facets do not read as a grid.
            if(SX==0)P.X+=(Noise(I,J,K,2)-.5)*.5*Step.X;
            if(SY==0)P.Y+=(Noise(I,J,K,3)-.5)*.5*Step.Y;
            if(K>0 && !Top)P.Z+=(Noise(I,J,K,4)-.5)*.55*Step.Z;
        }
        const double Crown=1-FMath::Max(U*U,V*V);
        if(Top)
        {
            const double DipDirection=Noise(0,0,0,9)<.5 ? U:-U;
            P.Z+=CragPeak*(.06+.23*Crown+.42*(DipDirection+1)*.5+.20*Noise(I/3,J/3,K,7)+.09*Noise(I,J,K,5));
        }
        P.Z-=CragArchDrop(Crag)*U*U*(1-Level);
        if(Crag.Reflected)P.Y=-P.Y;
        return Crag.Center+Turn.RotateVector(P);
    };
    // Counter-clockwise seen from outside; a reflected crag turns its winding back.
    auto Quad=[&](FVector A,FVector B,FVector C,FVector D)
    {
        if(Crag.Reflected)Out.Append({A,C,B,A,D,C});
        else Out.Append({A,B,C,A,C,D});
    };
    // The rim of the box seen from above, walked counter-clockwise.
    TArray<FIntPoint> Rim;
    for(int32 I=0;I<NX;++I)Rim.Add({I,0});
    for(int32 J=0;J<NY;++J)Rim.Add({NX,J});
    for(int32 I=NX;I>0;--I)Rim.Add({I,NY});
    for(int32 J=NY;J>0;--J)Rim.Add({0,J});
    for(int32 M=0;M<Rim.Num();++M)for(int32 K=0;K<NZ;++K)
    {
        const FIntPoint A=Rim[M],B=Rim[(M+1)%Rim.Num()];
        Quad(Point(A.X,A.Y,K),Point(B.X,B.Y,K),Point(B.X,B.Y,K+1),Point(A.X,A.Y,K+1));
    }
    for(int32 J=0;J<NY;++J)for(int32 I=0;I<NX;++I)
    {
        Quad(Point(I,J,NZ),Point(I+1,J,NZ),Point(I+1,J+1,NZ),Point(I,J+1,NZ));
        Quad(Point(I,J,0),Point(I,J+1,0),Point(I+1,J+1,0),Point(I+1,J,0));
    }
}

void BuildRockDamagePanels(FERProceduralLayout& Layout, float CellSize)
{
    int32 NextId=0;
    for(const auto& Panel:Layout.Panels)NextId=FMath::Max(NextId,Panel.Id+1);

    TArray<FVector> Points;
    auto AddRock=[&](const FVector& Center, float Yaw, uint32 Shape)
    {
        const FQuat Rotation(FRotator(0,Yaw,0));
        FBox Bounds(ForceInit);
        for(const FVector& Point:Points)Bounds+=Rotation.UnrotateVector(Point-Center);
        Bounds=Bounds.ExpandBy(1.);
        auto& Panel=Layout.Panels.AddDefaulted_GetRef();
        Panel.Id=NextId++;
        Panel.Material=EERBlockMaterial::Stone;
        Panel.Surface=Panel.InteriorSurface=Layout.BoulderSurface;
        Panel.Transform=FTransform(Rotation, Center+Rotation.RotateVector(Bounds.Min));
        Panel.Size=Bounds.GetSize();
        Panel.Cell={FERCellWorld::FitCell(Panel.Size.X,CellSize),FERCellWorld::FitCell(Panel.Size.Y,CellSize),
            FERCellWorld::FitCell(Panel.Size.Z,CellSize)};
        // Damage removes the local stone cells. Remaining faces stay attached to the rock formation.
        Panel.bAnchored=true;
        Panel.SkinName=TEXT("ERRockyStone");
        Panel.SkinCRC=Shape ? Shape:1;
        Panel.Skin.Reserve(Points.Num());
        for(int32 Index=0;Index<Points.Num();Index+=3)
        {
            const FVector Normal=Rotation.UnrotateVector(
                FVector::CrossProduct(Points[Index+1]-Points[Index],Points[Index+2]-Points[Index]).GetSafeNormal());
            for(int32 Corner:{0,2,1})
            {
                const FVector& P=Points[Index+Corner];
                Panel.Skin.Add({Panel.Transform.InverseTransformPositionNoScale(P),Normal,{P.X/250.,(P.Y+P.Z)/250.}});
            }
        }
        Points.Reset();
    };

    for(const auto& Boulder:Layout.Boulders)
    {
        BoulderTriangles(Boulder,Points);
        AddRock(Boulder.Center,Boulder.Yaw,Boulder.Shape);
    }
    for(const auto& Crag:Layout.Crags)
    {
        if(Crag.Perimeter)continue;
        CragTriangles(Crag,Points);
        AddRock(Crag.Center,Crag.Yaw,Crag.Shape);
    }
}

void BuildBoulders(UDynamicMeshComponent* Component,const FERProceduralLayout& L)
{
    using UE::Geometry::FIndex3i;
    UE::Geometry::FDynamicMesh3 Mesh;Mesh.EnableAttributes();
    auto* Normals=Mesh.Attributes()->PrimaryNormals();auto* UVs=Mesh.Attributes()->PrimaryUV();
    TArray<FVector> Points;
    if(!L.RockyCoverOnly)
        for(const auto& Boulder:L.Boulders)BoulderTriangles(Boulder,Points);
    for(const auto& Crag:L.Crags)
        if(!L.RockyCoverOnly || Crag.Perimeter)CragTriangles(Crag,Points);
    for(int32 I=0;I+2<Points.Num();I+=3)
    {
        // Front faces wind clockwise seen from outside, as the terrain's do: the outward order is reversed.
        const FVector A=Points[I],B=Points[I+2],C=Points[I+1];
        const FVector3f N(FVector::CrossProduct(C-A,B-A).GetSafeNormal());
        const int32 Start=Mesh.VertexCount();
        for(const FVector& P:{A,B,C}){Mesh.AppendVertex(P);Normals->AppendElement(N);UVs->AppendElement(FVector2f(float(P.X/250),float((P.Y+P.Z)/250)));}
        const int32 T=Mesh.AppendTriangle(Start,Start+1,Start+2);
        Normals->SetTriangle(T,FIndex3i(Start,Start+1,Start+2));UVs->SetTriangle(T,FIndex3i(Start,Start+1,Start+2));
    }
    Component->SetMesh(MoveTemp(Mesh));Component->UpdateCollision(false);
}
}
