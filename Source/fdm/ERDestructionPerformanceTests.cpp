#include "ERCellWorld.h"
#include "ERCellSupport.h"
#include "ERMeshCells.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FERCellPanelDesc PerformancePanel(int32 Id,FVector Size,FVector Position=FVector::ZeroVector)
{
    FERCellPanelDesc P;P.Id=Id;P.Material=EERBlockMaterial::Brick;P.Size=Size;P.Cell=FVector(5);P.Transform=FTransform(Position);return P;
}
bool SameMesh(const UE::Geometry::FDynamicMesh3& A,const UE::Geometry::FDynamicMesh3& B)
{
    if(A.VertexCount()!=B.VertexCount() || A.TriangleCount()!=B.TriangleCount())return false;
    const auto* AN=A.Attributes()->PrimaryNormals();const auto* BN=B.Attributes()->PrimaryNormals();
    const auto* AU=A.Attributes()->PrimaryUV();const auto* BU=B.Attributes()->PrimaryUV();
    for(int32 V:A.VertexIndicesItr())if(A.GetVertex(V)!=B.GetVertex(V))return false;
    for(int32 T:A.TriangleIndicesItr())
    {
        if(A.GetTriangle(T)!=B.GetTriangle(T) || AN->GetTriangle(T)!=BN->GetTriangle(T) || AU->GetTriangle(T)!=BU->GetTriangle(T) ||
            A.Attributes()->GetMaterialID()->GetValue(T)!=B.Attributes()->GetMaterialID()->GetValue(T))return false;
        const auto N=AN->GetTriangle(T),U=AU->GetTriangle(T);
        for(int32 K=0;K<3;++K)if(AN->GetElement(N[K])!=BN->GetElement(N[K]) || AU->GetElement(U[K])!=BU->GetElement(U[K]))return false;
    }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERCellSupportSlicesTest,"EclipseRecoil.Destruction.Cells.BudgetedSupport",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERCellSupportSlicesTest::RunTest(const FString& Parameters)
{
    FERCellSettings Settings;Settings.ChunkCells=16;
    auto Wall=PerformancePanel(1,{320,20,320});Wall.bAnchorBottom=true;
    auto Roof=PerformancePanel(2,{320,200,5},{0,0,320});
    auto Loose=PerformancePanel(3,{100,10,100},{-200,0,200});
    auto Hollow=PerformancePanel(4,{100,100,100},{600,0,100});ERMeshCells::BoxSkin(Hollow);Hollow.bSolidSkin=false;
    FERCellWorld World;FString Error;
    if(!TestTrue(TEXT("support fixture builds"),World.Build({Wall,Roof,Loose,Hollow},Settings,GetDefault<UERBlockMaterialCatalog>(),Error)))return false;
    FERCellSupport Sliced;
    if(!TestTrue(TEXT("support graph builds"),Sliced.Build(World,false)))return false;
    TArray<FERCellId> Cut;for(int32 X=0;X<64;++X)for(int32 Y=0;Y<4;++Y)Cut.Add({0,X,Y,0});
    World.DetachCells(Cut);
    auto Reference=Sliced;TArray<TArray<FERCellId>> Expected,Actual;Reference.EvaluateReference(World,Expected);
    TestTrue(TEXT("fixture contains detached components"),Expected.Num()>=3);
    Sliced.BeginEvaluation(World);
    const auto Snapshot=World.MakeSupportSnapshot();const uint32 SnapshotDigest=Snapshot.GetAliveDigest();
    World.DetachCells({{2,0,0,0},{3,0,0,0}});
    TestEqual(TEXT("live writes do not mutate retained alive masks"),Snapshot.GetAliveDigest(),SnapshotDigest);
    TestTrue(TEXT("snapshot differs from subsequently damaged world"),SnapshotDigest!=World.GetAliveDigest());
    int32 Slices=0;
    while(Sliced.IsEvaluating() && Slices++<1000000)
    {
        const bool Complete=Sliced.StepEvaluation(Actual,17,0);
        TestTrue(TEXT("each slice respects its work budget"),Sliced.LastSliceWork<=17);
        if(!Complete && !Actual.IsEmpty()){AddError(TEXT("partial support result escaped before completion"));break;}
    }
    TestFalse(TEXT("bounded evaluation completes"),Sliced.IsEvaluating());
    TestTrue(TEXT("sliced snapshot has identical component membership and ordering"),Actual==Expected);
    Reference.EvaluateReference(World,Expected);Sliced.Evaluate(World,Actual);
    TestTrue(TEXT("damage during the snapshot is included by the following check"),Actual==Expected);
    Sliced.BeginEvaluation(World);Sliced.StepEvaluation(Actual,1,0);Sliced.Reset();
    TestFalse(TEXT("reset cancels a pending evaluation"),Sliced.IsEvaluating());
    AddInfo(FString::Printf(TEXT("ER_SUPPORT_SLICES slices=%d components=%d"),Slices,Expected.Num()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERCellSkinCacheTest,"EclipseRecoil.Destruction.Cells.IncrementalSkin",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERCellSkinCacheTest::RunTest(const FString& Parameters)
{
    for(bool Solid:{false,true})
    {
        FERCellSettings Settings;Settings.ChunkCells=16;
        auto Panel=PerformancePanel(1,{400,20,400});ERMeshCells::BoxSkin(Panel);Panel.bSolidSkin=Solid;
        for(auto& Corner:Panel.Skin)Corner.UV=Corner.UV*1.7+FVector2D(.13,.29);
        FERCellWorld World;FString Error;
        if(!TestTrue(TEXT("skin fixture builds"),World.Build({Panel},Settings,GetDefault<UERBlockMaterialCatalog>(),Error)))return false;
        ERMeshCells::FCache Cache;
        auto Compare=[&]()
        {
            UE::Geometry::FDynamicMesh3 Reference,Cached;ERMeshCells::Build(World,0,Reference);ERMeshCells::BuildCached(World,0,Cached,Cache);
            return SameMesh(Reference,Cached);
        };
        TestTrue(TEXT("intact cached geometry preserves vertices, winding, normals and UVs"),Compare());
        TestEqual(TEXT("intact skins allocate no clipping cache"),Cache.GetAllocatedBytes(),SIZE_T(0));
        TestEqual(TEXT("intact skins defer their triangle index"),Cache.FirstChunk,INDEX_NONE);
        FRandomStream Random(7919);
        for(int32 Step=0;Step<12;++Step)
        {
            TArray<FERCellId> Cut;
            const int32 X=Step==0 ? 15:Random.RandRange(0,77),Z=Step==0 ? 15:Random.RandRange(0,77);
            for(int32 DZ=0;DZ<3;++DZ)for(int32 DX=0;DX<3;++DX)for(int32 Y=0;Y<4;++Y)Cut.Add({0,X+DX,Y,Z+DZ});
            World.DetachCells(Cut);
            if(!TestTrue(TEXT("incremental cut equals full reconstruction"),Compare()))break;
            if(Step==0)TestTrue(TEXT("first damage lazily creates the exact indexed cache"),!Cache.Disabled && !Cache.TriangleChunks.IsEmpty() && Cache.GetAllocatedBytes()>0);
        }
        TArray<FERCellId> WholeChunk;
        for(int32 Z=16;Z<32;++Z)for(int32 X=16;X<32;++X)for(int32 Y=0;Y<4;++Y)WholeChunk.Add({0,X,Y,Z});
        World.DetachCells(WholeChunk);
        TestTrue(TEXT("full-chunk cut and neighbouring fracture faces remain exact"),Compare());
        TestTrue(TEXT("unchanged skin remains exact"),Compare());
        TestEqual(TEXT("unchanged skin does not reclip source triangles"),Cache.LastClippedTriangles,0);
        TestEqual(TEXT("unchanged skin does not rebuild cut volumes"),Cache.LastCutChunks,0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERCellDirtyMaskTest,"EclipseRecoil.Destruction.Cells.BatchedDirtyMasks",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERCellDirtyMaskTest::RunTest(const FString& Parameters)
{
    FERCellSettings Settings;Settings.ChunkCells=16;
    FERCellWorld Server,Client;FString Error;
    const auto Panel=PerformancePanel(1,{165,20,165});
    if(!Server.Build({Panel},Settings,GetDefault<UERBlockMaterialCatalog>(),Error) || !Client.Build({Panel},Settings,GetDefault<UERBlockMaterialCatalog>(),Error))return false;
    const TArray<FERCellId> Cells={{0,0,0,0},{0,15,3,15},{0,16,0,16},{0,31,2,31},{0,32,3,32}};
    const auto Changed=Server.DetachCells(Cells);TSet<int32> Dirty;
    for(int32 C:Changed.ChangedChunks)
    {
        TArray<uint8> Wire;TArray<uint64> Bits;Server.EncodeChunk(C,Wire);
        if(!TestTrue(TEXT("server mask decodes"),Client.DecodeChunk(C,Wire,Bits)))return false;
        Client.SetChunkAlive(C,Bits,Dirty);
    }
    auto Actual=Dirty.Array();Actual.Sort();
    TestTrue(TEXT("wordwise diff has exactly the authoritative face neighbours"),Actual==Changed.DirtyChunks);
    TestEqual(TEXT("batched mask application retains the exact state"),Client.GetAliveDigest(),Server.GetAliveDigest());
    TestEqual(TEXT("removed cell accounting remains exact"),Client.GetRemovedCount(),Server.GetRemovedCount());
    return true;
}
#endif
