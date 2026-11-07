#pragma once
#include "CoreMinimal.h"
#include "ERCellWorld.h"

// Structural support over 5cm cells. Nodes are face-connected components of
// alive cells inside one chunk; edges join components of neighbouring chunks of
// a panel and of panels in geometric face contact. Labels and the edges of a
// chunk pair are recomputed only when one of the two chunks changed, so a check
// costs the dirty chunks plus a union-find over components. Anchored cells
// seed support. Connectivity, not load bearing.
class FDM_API FERCellSupport
{
public:
    // Precomputes chunk pairs and panel-to-panel face contacts. Bottom rows on the
    // ground plane anchor when GroundAnchors is set, in addition to panel flags.
    bool Build(const FERCellWorld& World, bool GroundAnchors = true, float GroundZ = 0);
    void Reset();
    // Unsupported components, each a sorted cell list; deterministic order.
    void Evaluate(const FERCellWorld& World, TArray<TArray<FERCellId>>& Unsupported);
    // One immutable snapshot, advanced in bounded slices. No live world writes
    // occur until a complete, canonically ordered result is returned.
    void BeginEvaluation(const FERCellWorld& World);
    bool StepEvaluation(TArray<TArray<FERCellId>>& Unsupported,int32 WorkBudget=262144,double TimeBudgetMs=1.5);
    bool IsEvaluating() const {return Evaluation.IsValid();}
#if WITH_DEV_AUTOMATION_TESTS
    void EvaluateReference(const FERCellWorld& World,TArray<TArray<FERCellId>>& Unsupported);
#endif
    bool IsBuilt() const { return bBuilt; }
    int32 GetContactCount() const { return ContactCount; }
    SIZE_T GetAllocatedBytes() const;
    double BuildMs = 0, LastMs = 0;
    int32 LastRelabelled = 0, LastComponents = 0, LastWork = 0;
    int32 LastSliceWork=0;
    double LastSliceMs=0,PeakSliceMs=0;
private:
    struct FLabels { uint32 Revision = MAX_uint32; int32 Count = 0; bool Uniform=false; TArray<int32> Label; TArray<FIntPoint> Sparse; TArray<uint8> Anchored; int32 At(int32 Index) const; };
    // Two chunks that can touch: grid neighbours (Axis 0/2) or contact partners (Axis 3).
    struct FPair { int32 A = 0, B = 0, Axis = 0; uint32 RevA = MAX_uint32, RevB = MAX_uint32; TArray<FIntPoint> Contacts; TArray<FIntPoint> Edges; };
    TArray<FLabels> Labels;
    TArray<FPair> Pairs;
    TArray<uint8> GroundPanel;
    int32 ContactCount = 0;
    bool bBuilt = false;
    struct FEvaluation;
    TSharedPtr<FEvaluation> Evaluation;
    void Relabel(const FERCellWorld& World, int32 Chunk);
    void RefreshEdges(const FERCellWorld& World, FPair& Pair);
};
