#include "ERMeshCells.h"
#include "ERCellMesher.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

namespace
{
using FCellPoly=TArray<FERMeshCorner,TInlineAllocator<12>>;
FERMeshCorner Interpolate(const FERMeshCorner& A,const FERMeshCorner& B,double T)
{return {FMath::Lerp(A.Position,B.Position,T),FMath::Lerp(A.Normal,B.Normal,T),FMath::Lerp(A.UV,B.UV,T)};}
FCellPoly Clip(const FCellPoly& In,int32 Axis,double At,bool Less)
{
    FCellPoly Out;if(In.IsEmpty())return Out;
    auto Distance=[&](const FERMeshCorner& V){return (V.Position[Axis]-At)*(Less ? 1.:-1.);};
    auto A=In.Last();double DA=Distance(A);
    for(const auto& B:In)
    {
        const double DB=Distance(B);
        if((DA<=0)!=(DB<=0))Out.Add(Interpolate(A,B,DA/(DA-DB)));
        if(DB<=0)Out.Add(B);A=B;DA=DB;
    }
    return Out;
}
void Append(const FCellPoly& P,UE::Geometry::FDynamicMesh3& Mesh,int32 Material=0)
{
    if(P.Num()<3)return;
    auto* N=Mesh.Attributes()->PrimaryNormals();auto* UV=Mesh.Attributes()->PrimaryUV();
    for(int32 I=1;I+1<P.Num();++I)
    {
        const FERMeshCorner C[3]={P[0],P[I],P[I+1]};
        if(FVector::CrossProduct(C[1].Position-C[0].Position,C[2].Position-C[0].Position).SizeSquared()<1.e-10)continue;
        int32 V[3],Ns[3],Us[3];
        for(int32 J=0;J<3;++J){V[J]=Mesh.AppendVertex(C[J].Position);Ns[J]=N->AppendElement(FVector3f(C[J].Normal.GetSafeNormal()));Us[J]=UV->AppendElement(FVector2f(C[J].UV));}
        const auto T=Mesh.AppendTriangle(V[0],V[1],V[2]);
        if(T>=0){N->SetTriangle(T,{Ns[0],Ns[1],Ns[2]});UV->SetTriangle(T,{Us[0],Us[1],Us[2]});Mesh.Attributes()->GetMaterialID()->SetValue(T,Material);}
    }
}
bool Bit(const TArray<uint64>& B,int32 I){return (B[I>>6]>>(I&63))&1;}
// Merge removed cells only. Source voids never create damage boxes or fracture
// faces. A completely lost chunk is one cut, including its empty room volume.
void Cuts(const FERCellWorld& W,int32 CI,TArray<FBox>& Out)
{
    const auto& C=W.GetChunks()[CI];const auto& P=W.GetPanels()[C.Panel];
    if(C.Revision==0 || C.Alive==C.InitialAlive)return;
    if(!C.AliveCount){Out.Add(FBox(FVector(C.X0,0,C.Z0)*P.Cell,FVector(C.X0+C.SX,P.Dims.Y,C.Z0+C.SZ)*P.Cell));return;}
    TBitArray<> Used(false,C.SX*C.SZ*P.Dims.Y);
    auto Free=[&](int32 X,int32 Y,int32 Z){const int32 I=FERCellWorld::CellIndex(C,X,Y,Z);return !Used[I] && Bit(C.InitialAlive,I) && !Bit(C.Alive,I);};
    for(int32 Y=0;Y<P.Dims.Y;++Y)for(int32 Z=0;Z<C.SZ;++Z)for(int32 X=0;X<C.SX;++X)if(Free(X,Y,Z))
    {
        int32 X1=X+1,Z1=Z+1,Y1=Y+1;while(X1<C.SX && Free(X1,Y,Z))++X1;
        for(bool Grow=true;Grow && Z1<C.SZ;){for(int32 K=X;K<X1 && Grow;++K)Grow=Free(K,Y,Z1);if(Grow)++Z1;}
        for(bool Grow=true;Grow && Y1<P.Dims.Y;){for(int32 KZ=Z;KZ<Z1 && Grow;++KZ)for(int32 KX=X;KX<X1 && Grow;++KX)Grow=Free(KX,Y1,KZ);if(Grow)++Y1;}
        for(int32 KY=Y;KY<Y1;++KY)for(int32 KZ=Z;KZ<Z1;++KZ)for(int32 KX=X;KX<X1;++KX)Used[FERCellWorld::CellIndex(C,KX,KY,KZ)]=true;
        Out.Add(FBox(FVector(C.X0+X,Y,C.Z0+Z)*P.Cell,FVector(C.X0+X1,Y1,C.Z0+Z1)*P.Cell));
    }
}
}

void ERMeshCells::Rasterize(const FERCellPanel& P,TFunctionRef<void(FIntVector)> Put)
{
    for(int32 I=0;I<P.Skin.Num();I+=3)
    {
        const FCellPoly Triangle{P.Skin[I],P.Skin[I+1],P.Skin[I+2]};
        const auto Normal=FVector::CrossProduct(Triangle[1].Position-Triangle[0].Position,Triangle[2].Position-Triangle[0].Position).GetAbs();
        const int32 Axis=Normal.X>Normal.Y && Normal.X>Normal.Z ? 0:Normal.Y>Normal.Z ? 1:2,U=(Axis+1)%3,V=(Axis+2)%3;
        FBox Bounds(ForceInit);for(const auto& C:Triangle)Bounds+=C.Position;
        const int32 U0=FMath::Clamp(FMath::FloorToInt(Bounds.Min[U]/P.Cell[U]-.0001),0,P.Dims[U]-1),U1=FMath::Clamp(FMath::FloorToInt(Bounds.Max[U]/P.Cell[U]+.0001),0,P.Dims[U]-1);
        const int32 V0=FMath::Clamp(FMath::FloorToInt(Bounds.Min[V]/P.Cell[V]-.0001),0,P.Dims[V]-1),V1=FMath::Clamp(FMath::FloorToInt(Bounds.Max[V]/P.Cell[V]+.0001),0,P.Dims[V]-1);
        for(int32 B=V0;B<=V1;++B)for(int32 A=U0;A<=U1;++A)
        {
            auto Poly=Clip(Clip(Clip(Clip(Triangle,U,A*P.Cell[U]-.001,false),U,(A+1)*P.Cell[U]+.001,true),V,B*P.Cell[V]-.001,false),V,(B+1)*P.Cell[V]+.001,true);
            if(Poly.Num()<3)continue;
            double Lo=DBL_MAX,Hi=-DBL_MAX;for(const auto& C:Poly){Lo=FMath::Min(Lo,C.Position[Axis]);Hi=FMath::Max(Hi,C.Position[Axis]);}
            const int32 D0=FMath::Clamp(FMath::FloorToInt((Lo-.001)/P.Cell[Axis]),0,P.Dims[Axis]-1),D1=FMath::Clamp(FMath::FloorToInt((Hi+.001)/P.Cell[Axis]),0,P.Dims[Axis]-1);
            for(int32 D=D0;D<=D1;++D){FIntVector C;C[Axis]=D;C[U]=A;C[V]=B;Put(C);}
        }
    }
}

void ERMeshCells::Build(const FERCellWorld& World,int32 SkinPanelIndex,UE::Geometry::FDynamicMesh3& Mesh)
{
    const auto& P=World.GetPanels()[SkinPanelIndex];Mesh.EnableAttributes();Mesh.Attributes()->EnableMaterialID();
    TArray<FBox> Removed;TArray<int32> Changed;
    for(int32 C=P.FirstChunk;C<P.FirstChunk+P.ChunksX*P.ChunksZ;++C)if(World.GetChunks()[C].Revision){Cuts(World,C,Removed);Changed.Add(C);}
    for(int32 I=0;I<P.Skin.Num();I+=3)
    {
        TArray<FCellPoly> Polys;Polys.Add({P.Skin[I],P.Skin[I+1],P.Skin[I+2]});
        for(const auto& Cut:Removed)
        {
            const auto Box=Cut.ExpandBy(.001);
            TArray<FCellPoly> Next;
            for(const auto& Poly:Polys)
            {
                FBox Bounds(ForceInit);for(const auto& V:Poly)Bounds+=V.Position;
                if(!Bounds.Intersect(Box)){Next.Add(Poly);continue;}
                auto Inside=Poly;
                for(int32 Axis=0;Axis<3 && Inside.Num()>=3;++Axis)
                {
                    auto Outside=Clip(Inside,Axis,Box.Min[Axis],true);if(Outside.Num()>=3)Next.Add(MoveTemp(Outside));
                    Inside=Clip(Inside,Axis,Box.Min[Axis],false);
                    Outside=Clip(Inside,Axis,Box.Max[Axis],false);if(Outside.Num()>=3)Next.Add(MoveTemp(Outside));
                    Inside=Clip(Inside,Axis,Box.Max[Axis],true);
                }
            }
            Polys=MoveTemp(Next);if(Polys.IsEmpty())break;
        }
        for(const auto& Poly:Polys)Append(Poly,Mesh);
    }
    TSet<int32> Edges(Changed);
    for(int32 C:Changed)
    {
        const int32 Local=C-P.FirstChunk,X=Local%P.ChunksX,Z=Local/P.ChunksX;
        if(X)Edges.Add(C-1);if(X+1<P.ChunksX)Edges.Add(C+1);if(Z)Edges.Add(C-P.ChunksX);if(Z+1<P.ChunksZ)Edges.Add(C+P.ChunksX);
    }
    auto EdgeOrder=Edges.Array();EdgeOrder.Sort();TArray<ERCellMesh::FQuad> Faces;
    for(int32 C:EdgeOrder)
    {
        ERCellMesh::BuildFaces(World,C,Faces,true);
        for(const auto& Q:Faces)
        {
            const int32 Axis=FMath::Abs(Q.Normal.X)>.5 ? 0:FMath::Abs(Q.Normal.Y)>.5 ? 1:2,U=(Axis+1)%3,V=(Axis+2)%3;
            const float Mirror=Q.Normal[Axis]<0 ? -1.f:1.f;
            FCellPoly Poly;for(int32 Corner=0;Corner<4;++Corner)Poly.Add({FVector(Q.Corner[Corner]),FVector(Q.Normal),FVector2D(Mirror*Q.Corner[Corner][U],Q.Corner[Corner][V])/100});
            Append(Poly,Mesh,1);
        }
    }
}

SIZE_T ERMeshCells::FCache::GetAllocatedBytes() const
{
    SIZE_T Bytes=Revisions.GetAllocatedSize()+ChunkTriangles.GetAllocatedSize()+TriangleChunks.GetAllocatedSize()+ChunkCuts.GetAllocatedSize()+TriangleCorners.GetAllocatedSize()+FractureCorners.GetAllocatedSize();
    for(const auto& Items:ChunkTriangles)Bytes+=Items.GetAllocatedSize();
    for(const auto& Items:TriangleChunks)Bytes+=Items.GetAllocatedSize();
    for(const auto& Items:ChunkCuts)Bytes+=Items.GetAllocatedSize();
    for(const auto& Items:TriangleCorners)Bytes+=Items.GetAllocatedSize();
    for(const auto& Items:FractureCorners)Bytes+=Items.GetAllocatedSize();
    return Bytes;
}

void ERMeshCells::BuildCached(const FERCellWorld& World,int32 SkinPanelIndex,UE::Geometry::FDynamicMesh3& Mesh,FCache& Cache)
{
    const auto& P=World.GetPanels()[SkinPanelIndex];
    const int32 ChunkCount=P.ChunksX*P.ChunksZ,TriangleCount=P.Skin.Num()/3;
    constexpr SIZE_T MaxCacheBytes=32*1024*1024;
    constexpr int32 MaxReferences=1000000;
    if(Cache.SourceLayoutDigest!=World.GetLayoutDigest() || Cache.SourceCRC!=P.SkinCRC || Cache.FirstChunk!=P.FirstChunk || Cache.SourceCorners!=P.Skin.Num())Cache=FCache();
    Cache.LastClippedTriangles=Cache.LastCutChunks=0;
    auto Fallback=[&]()
    {
        Cache=FCache();Cache.Disabled=true;Cache.SourceCRC=P.SkinCRC;Cache.SourceLayoutDigest=World.GetLayoutDigest();Cache.FirstChunk=P.FirstChunk;Cache.SourceCorners=P.Skin.Num();
        Build(World,SkinPanelIndex,Mesh);
    };
    if(Cache.Disabled){Build(World,SkinPanelIndex,Mesh);return;}
    const bool Initial=Cache.FirstChunk==INDEX_NONE;
    if(Initial)
    {
        // Intact skins need no clipping history. Defer the triangle index and
        // retained geometry until this particular structure is damaged.
        bool Changed=false;
        for(int32 C=0;C<ChunkCount;++C)if(World.GetChunks()[P.FirstChunk+C].Revision){Changed=true;break;}
        if(!Changed){Build(World,SkinPanelIndex,Mesh);return;}
        Cache.SourceCRC=P.SkinCRC;Cache.SourceLayoutDigest=World.GetLayoutDigest();Cache.FirstChunk=P.FirstChunk;Cache.SourceCorners=P.Skin.Num();
        Cache.Revisions.Init(MAX_uint32,ChunkCount);Cache.ChunkCuts.SetNum(ChunkCount);Cache.ChunkTriangles.SetNum(ChunkCount);
        Cache.FractureCorners.SetNum(ChunkCount);Cache.TriangleChunks.SetNum(TriangleCount);Cache.TriangleCorners.SetNum(TriangleCount);
        int32 References=0;
        const auto& First=World.GetChunks()[P.FirstChunk];
        // A triangle can span several chunks. Index its original bounds, including
        // the exact clipping epsilon, so a later cut cannot escape the index.
        for(int32 T=0;T<TriangleCount;++T)
        {
            FBox Bounds(ForceInit);for(int32 V=0;V<3;++V)Bounds+=P.Skin[T*3+V].Position;
            Bounds=Bounds.ExpandBy(.001);
            const int32 X0=FMath::Clamp(FMath::FloorToInt32(Bounds.Min.X/(P.Cell.X*First.SX)),0,P.ChunksX-1);
            const int32 X1=FMath::Clamp(FMath::FloorToInt32(Bounds.Max.X/(P.Cell.X*First.SX)),0,P.ChunksX-1);
            const int32 Z0=FMath::Clamp(FMath::FloorToInt32(Bounds.Min.Z/(P.Cell.Z*First.SZ)),0,P.ChunksZ-1);
            const int32 Z1=FMath::Clamp(FMath::FloorToInt32(Bounds.Max.Z/(P.Cell.Z*First.SZ)),0,P.ChunksZ-1);
            for(int32 Z=Z0;Z<=Z1;++Z)for(int32 X=X0;X<=X1;++X)
            {
                const int32 C=Z*P.ChunksX+X;
                const auto& K=World.GetChunks()[P.FirstChunk+C];
                const FBox ChunkBox(FVector(K.X0,0,K.Z0)*P.Cell,FVector(K.X0+K.SX,P.Dims.Y,K.Z0+K.SZ)*P.Cell);
                if(!Bounds.Intersect(ChunkBox))continue;
                if(++References>MaxReferences){Fallback();return;}
                Cache.ChunkTriangles[C].Add(T);Cache.TriangleChunks[T].Add(C);
            }
        }
    }
    TBitArray<> DirtyTriangles(Initial,TriangleCount),DirtyFractures(false,ChunkCount),FractureEdges(false,ChunkCount);
    auto MarkEdges=[&](TBitArray<>& Flags,int32 C)
    {
        Flags[C]=true;const int32 X=C%P.ChunksX,Z=C/P.ChunksX;
        if(X)Flags[C-1]=true;if(X+1<P.ChunksX)Flags[C+1]=true;
        if(Z)Flags[C-P.ChunksX]=true;if(Z+1<P.ChunksZ)Flags[C+P.ChunksX]=true;
    };
    for(int32 C=0;C<ChunkCount;++C)
    {
        const uint32 Revision=World.GetChunks()[P.FirstChunk+C].Revision;
        if(Revision)MarkEdges(FractureEdges,C);
        if(Cache.Revisions[C]==Revision)continue;
        Cache.Revisions[C]=Revision;Cache.ChunkCuts[C].Reset();Cuts(World,P.FirstChunk+C,Cache.ChunkCuts[C]);++Cache.LastCutChunks;
        for(int32 T:Cache.ChunkTriangles[C])DirtyTriangles[T]=true;
        MarkEdges(DirtyFractures,C);
    }
    for(int32 T=0;T<TriangleCount;++T)if(DirtyTriangles[T])
    {
        ++Cache.LastClippedTriangles;
        TArray<FCellPoly> Polys;Polys.Add({P.Skin[T*3],P.Skin[T*3+1],P.Skin[T*3+2]});
        // Both lists are in the original chunk/cut order. Skipping boxes outside
        // the original triangle does not alter clipping, winding or interpolation.
        for(int32 C:Cache.TriangleChunks[T])
        {
            for(const auto& Cut:Cache.ChunkCuts[C])
            {
                const FBox Box=Cut.ExpandBy(.001);TArray<FCellPoly> Next;
                for(const auto& Poly:Polys)
                {
                    FBox Bounds(ForceInit);for(const auto& V:Poly)Bounds+=V.Position;
                    if(!Bounds.Intersect(Box)){Next.Add(Poly);continue;}
                    auto Inside=Poly;
                    for(int32 Axis=0;Axis<3 && Inside.Num()>=3;++Axis)
                    {
                        auto Outside=Clip(Inside,Axis,Box.Min[Axis],true);if(Outside.Num()>=3)Next.Add(MoveTemp(Outside));
                        Inside=Clip(Inside,Axis,Box.Min[Axis],false);
                        Outside=Clip(Inside,Axis,Box.Max[Axis],false);if(Outside.Num()>=3)Next.Add(MoveTemp(Outside));
                        Inside=Clip(Inside,Axis,Box.Max[Axis],true);
                    }
                }
                Polys=MoveTemp(Next);if(Polys.IsEmpty())break;
            }
            if(Polys.IsEmpty())break;
        }
        auto& Corners=Cache.TriangleCorners[T];Corners.Reset();
        for(const auto& Poly:Polys)for(int32 V=1;V+1<Poly.Num();++V){Corners.Add(Poly[0]);Corners.Add(Poly[V]);Corners.Add(Poly[V+1]);}
    }
    TArray<ERCellMesh::FQuad> Faces;
    for(int32 C=0;C<ChunkCount;++C)if(DirtyFractures[C])
    {
        auto& Corners=Cache.FractureCorners[C];Corners.Reset();
        if(!FractureEdges[C])continue;
        ERCellMesh::BuildFaces(World,P.FirstChunk+C,Faces,true);
        for(const auto& Q:Faces)
        {
            const int32 Axis=FMath::Abs(Q.Normal.X)>.5 ? 0:FMath::Abs(Q.Normal.Y)>.5 ? 1:2,U=(Axis+1)%3,V=(Axis+2)%3;
            const float Mirror=Q.Normal[Axis]<0 ? -1.f:1.f;
            FERMeshCorner Quad[4];for(int32 Corner=0;Corner<4;++Corner)Quad[Corner]={FVector(Q.Corner[Corner]),FVector(Q.Normal),FVector2D(Mirror*Q.Corner[Corner][U],Q.Corner[Corner][V])/100};
            for(int32 Corner:{0,1,2,0,2,3})Corners.Add(Quad[Corner]);
        }
    }
    if(Cache.GetAllocatedBytes()>MaxCacheBytes){Fallback();return;}
    Mesh.EnableAttributes();Mesh.Attributes()->EnableMaterialID();
    auto Emit=[&](const TArray<FERMeshCorner>& Corners,int32 Material)
    {for(int32 V=0;V<Corners.Num();V+=3)Append({Corners[V],Corners[V+1],Corners[V+2]},Mesh,Material);};
    for(const auto& Corners:Cache.TriangleCorners)Emit(Corners,0);
    for(int32 C=0;C<ChunkCount;++C)if(FractureEdges[C])Emit(Cache.FractureCorners[C],1);
}

void ERMeshCells::BoxSkin(FERCellPanelDesc& P)
{
    for(int32 Axis=0;Axis<3;++Axis)for(int32 Sign:{-1,1})
    {
        const int32 U=(Axis+1)%3,V=(Axis+2)%3;FERMeshCorner Q[4];
        for(int32 I=0;I<4;++I){Q[I].Position[Axis]=Sign>0 ? P.Size[Axis]:0;Q[I].Position[U]=(I==1 || I==2) ? P.Size[U]:0;Q[I].Position[V]=I>=2 ? P.Size[V]:0;Q[I].Normal=FVector::ZeroVector;Q[I].Normal[Axis]=Sign;Q[I].UV={(Sign<0 ? -1.:1.)*Q[I].Position[U]/100,Q[I].Position[V]/100};}
        if(Sign>0)Swap(Q[1],Q[3]);
        for(int32 I:{0,1,2,0,2,3})P.Skin.Add(Q[I]);
    }
    P.SkinCRC=1;P.SkinName=TEXT("AlleyMasonry");P.bSolidSkin=true;
}
