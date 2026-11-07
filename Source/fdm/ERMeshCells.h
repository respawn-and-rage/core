#pragma once
#include "ERCellWorld.h"
#include "DynamicMesh/DynamicMesh3.h"

namespace ERMeshCells
{
    // CPU clipping cache only. Source geometry and resulting triangle order stay
    // unchanged; collision still commits with the complete visible skin.
    struct FCache
    {
        TArray<uint32> Revisions;
        TArray<TArray<int32>> ChunkTriangles,TriangleChunks;
        TArray<TArray<FBox>> ChunkCuts;
        TArray<TArray<FERMeshCorner>> TriangleCorners;
        TArray<TArray<FERMeshCorner>> FractureCorners;
        uint32 SourceCRC=0,SourceLayoutDigest=0;
        int32 FirstChunk=INDEX_NONE,SourceCorners=0,LastClippedTriangles=0,LastCutChunks=0;
        bool Disabled=false;
        SIZE_T GetAllocatedBytes() const;
    };
    void Rasterize(const FERCellPanel& P,TFunctionRef<void(FIntVector)> Put);
    void Build(const FERCellWorld& World,int32 Panel,UE::Geometry::FDynamicMesh3& Mesh);
    void BuildCached(const FERCellWorld& World,int32 Panel,UE::Geometry::FDynamicMesh3& Mesh,FCache& Cache);
    void BoxSkin(FERCellPanelDesc& Panel);
}
