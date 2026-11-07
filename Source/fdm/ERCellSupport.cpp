#include "ERCellSupport.h"
#include "HAL/PlatformTime.h"
#include "Algo/BinarySearch.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
bool Bit(const FERCellChunk& C, int32 I) { return (C.Alive[I >> 6] >> (I & 63)) & 1; }
int32 Find(TArray<int32>& Parent, int32 X)
{
    while (Parent[X] != X) { Parent[X] = Parent[Parent[X]]; X = Parent[X]; }
    return X;
}
}

int32 FERCellSupport::FLabels::At(int32 I) const
{
    if(Uniform)return 0;
    if(!Label.IsEmpty())return Label[I];
    const int32 K=Algo::LowerBoundBy(Sparse,I,[](FIntPoint P){return P.X;});
    return Sparse.IsValidIndex(K) && Sparse[K].X==I ? Sparse[K].Y:INDEX_NONE;
}

void FERCellSupport::Reset()
{
    Evaluation.Reset();LastSliceWork=0;LastSliceMs=PeakSliceMs=0;
    Pairs.Empty(); Labels.Empty(); GroundPanel.Empty(); ContactCount = 0; bBuilt = false; BuildMs = LastMs = 0; LastRelabelled = LastComponents = LastWork = 0;
}

bool FERCellSupport::Build(const FERCellWorld& World, bool GroundAnchors, float GroundZ)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ERCell_SupportBuild);
    const double Start = FPlatformTime::Seconds();
    Reset();
    const auto& Panels = World.GetPanels();
    if (Panels.IsEmpty() || !FMath::IsFinite(GroundZ)) return false;
    GroundPanel.Init(0, Panels.Num());
    for (int32 P = 0; P < Panels.Num(); ++P)
        GroundPanel[P] = GroundAnchors && Panels[P].Transform.GetRotation().GetAxisZ().Z > .99 && Panels[P].Bounds.Min.Z <= GroundZ + .01;
    // Face contacts: sample just outside each boundary cell of A where another
    // panel's box overlaps that face; both directions, so thin cells are covered.
    TMap<FIntPoint, int32> ContactPair;
    for (int32 A = 0; A < Panels.Num(); ++A)
    {
        const auto& PA = Panels[A];
        for (int32 Axis = 0; Axis < 3; ++Axis)
            for (int32 Sign : {-1, 1})
            {
                FVector FaceMin(0), FaceMax = PA.Size;
                FaceMin[Axis] = FaceMax[Axis] = Sign > 0 ? PA.Size[Axis] : 0;
                const FBox Face = FBox(FaceMin, FaceMax).TransformBy(PA.Transform).ExpandBy(.05);
                for (int32 B = 0; B < Panels.Num(); ++B)
                {
                    if (B == A || !Face.Intersect(Panels[B].Bounds)) continue;
                    const auto& PB = Panels[B];
                    // B's box in A's frame bounds the face cells worth sampling.
                    const FBox Local = FBox(FVector::ZeroVector, PB.Size).TransformBy(PB.Transform * PA.Transform.Inverse());
                    const int32 U = (Axis + 1) % 3, V = (Axis + 2) % 3;
                    const int32 U0 = FMath::Max(0, FMath::FloorToInt(Local.Min[U] / PA.Cell[U])), U1 = FMath::Min(PA.Dims[U] - 1, FMath::CeilToInt(Local.Max[U] / PA.Cell[U]) - 1);
                    const int32 V0 = FMath::Max(0, FMath::FloorToInt(Local.Min[V] / PA.Cell[V])), V1 = FMath::Min(PA.Dims[V] - 1, FMath::CeilToInt(Local.Max[V] / PA.Cell[V]) - 1);
                    for (int32 IV = V0; IV <= V1; ++IV)
                        for (int32 IU = U0; IU <= U1; ++IU)
                        {
                            FIntVector CA; CA[Axis] = Sign > 0 ? PA.Dims[Axis] - 1 : 0; CA[U] = IU; CA[V] = IV;
                            FVector Sample = (FVector(CA) + .5) * PA.Cell;
                            Sample[Axis] = (Sign > 0 ? PA.Size[Axis] : 0) + Sign * .02;
                            const FVector InB = PB.Transform.InverseTransformPosition(PA.Transform.TransformPosition(Sample));
                            if (InB.X < 0 || InB.Y < 0 || InB.Z < 0 || InB.X > PB.Size.X || InB.Y > PB.Size.Y || InB.Z > PB.Size.Z) continue;
                            FIntVector CB;
                            for (int32 K = 0; K < 3; ++K) CB[K] = FMath::Clamp(FMath::FloorToInt(InB[K] / PB.Cell[K]), 0, PB.Dims[K] - 1);
                            const int32 ChunkA = World.GetChunkIndex(A, CA.X, CA.Z), ChunkB = World.GetChunkIndex(B, CB.X, CB.Z);
                            const auto& KA = World.GetChunks()[ChunkA]; const auto& KB = World.GetChunks()[ChunkB];
                            const int32 IA = FERCellWorld::CellIndex(KA, CA.X - KA.X0, CA.Y, CA.Z - KA.Z0), IB = FERCellWorld::CellIndex(KB, CB.X - KB.X0, CB.Y, CB.Z - KB.Z0);
                            const FIntPoint Key(FMath::Min(ChunkA, ChunkB), FMath::Max(ChunkA, ChunkB));
                            int32* Index = ContactPair.Find(Key);
                            if (!Index) { Index = &ContactPair.Add(Key, Pairs.Num()); auto& New = Pairs.AddDefaulted_GetRef(); New.A = Key.X; New.B = Key.Y; New.Axis = 3; }
                            Pairs[*Index].Contacts.Add(ChunkA == Pairs[*Index].A ? FIntPoint(IA, IB) : FIntPoint(IB, IA));
                            if (++ContactCount > 20000000) { Reset(); return false; }
                        }
                }
            }
    }
    // Grid neighbours inside each panel: +X and +Z.
    for (const auto& P : Panels)
        for (int32 CZ = 0; CZ < P.ChunksZ; ++CZ)
            for (int32 CX = 0; CX < P.ChunksX; ++CX)
            {
                const int32 C = P.FirstChunk + CZ * P.ChunksX + CX;
                if (CX + 1 < P.ChunksX) { auto& New = Pairs.AddDefaulted_GetRef(); New.A = C; New.B = C + 1; New.Axis = 0; }
                if (CZ + 1 < P.ChunksZ) { auto& New = Pairs.AddDefaulted_GetRef(); New.A = C; New.B = C + P.ChunksX; New.Axis = 2; }
            }
    Labels.SetNum(World.GetChunks().Num());
    for (int32 C = 0; C < Labels.Num(); ++C) Relabel(World, C);
    for (auto& Pair : Pairs) RefreshEdges(World, Pair);
    bBuilt = true;
    BuildMs = (FPlatformTime::Seconds() - Start) * 1000;
    return true;
}

void FERCellSupport::Relabel(const FERCellWorld& World, int32 Index)
{
    const auto& C = World.GetChunks()[Index];
    const auto& P = World.GetPanels()[C.Panel];
    auto& L = Labels[Index];
    const int32 NY = P.Dims.Y, Bits = C.SX * C.SZ * NY;
    L.Revision=C.Revision;L.Count=0;L.Uniform=C.AliveCount==Bits;L.Label.Reset();L.Sparse.Reset();L.Anchored.Reset();
    if(L.Uniform)
    {
        L.Count=1;L.Anchored.Add(P.bAnchored || P.bIndestructible || ((P.bAnchorBottom || GroundPanel[C.Panel]) && C.Z0==0) || (P.bAnchorBottom && C.Z0*P.Cell.Z<P.AnchorHeight));++LastRelabelled;return;
    }
    if(!C.AliveCount){L.Label.Empty();L.Sparse.Empty();++LastRelabelled;return;}
    L.Label.Init(INDEX_NONE,Bits);
    TArray<int32> Stack;
    for (int32 Seed = 0; Seed < Bits; ++Seed)
    {
        if (L.Label[Seed] != INDEX_NONE || !Bit(C, Seed)) continue;
        const int32 Component = L.Count++;
        bool bAnchored = false;
        L.Label[Seed] = Component; Stack.Add(Seed);
        while (!Stack.IsEmpty())
        {
            const int32 I = Stack.Pop();
            const int32 X = I % C.SX, Z = (I / C.SX) % C.SZ, Y = I / (C.SX * C.SZ);
            if (!bAnchored)
            {
                const FERCellId Id{C.Panel, C.X0 + X, Y, C.Z0 + Z};
                bAnchored = World.IsAnchored(Id) || (GroundPanel[C.Panel] && Id.Z == 0);
            }
            const int32 Next[6][3] = {{X - 1, Y, Z}, {X + 1, Y, Z}, {X, Y - 1, Z}, {X, Y + 1, Z}, {X, Y, Z - 1}, {X, Y, Z + 1}};
            for (const auto& N : Next)
            {
                if (N[0] < 0 || N[0] >= C.SX || N[1] < 0 || N[1] >= NY || N[2] < 0 || N[2] >= C.SZ) continue;
                const int32 J = FERCellWorld::CellIndex(C, N[0], N[1], N[2]);
                if (L.Label[J] == INDEX_NONE && Bit(C, J)) { L.Label[J] = Component; Stack.Add(J); }
            }
        }
        L.Anchored.Add(bAnchored);
    }
    if(P.bSkinned){L.Sparse.Reserve(C.AliveCount);for(int32 I=0;I<Bits;++I)if(L.Label[I]!=INDEX_NONE)L.Sparse.Add({I,L.Label[I]});L.Label.Empty();}
    ++LastRelabelled;
}

void FERCellSupport::RefreshEdges(const FERCellWorld& World, FPair& Pair)
{
    const auto& LA = Labels[Pair.A], &LB = Labels[Pair.B];
    TSet<FIntPoint> Unique;
    auto Add = [&](int32 IA, int32 IB) { const int32 A = LA.At(IA), B = LB.At(IB); if (A != INDEX_NONE && B != INDEX_NONE) Unique.Add(FIntPoint(A, B)); };
    const auto& KA = World.GetChunks()[Pair.A], &KB = World.GetChunks()[Pair.B];
    const int32 NY = World.GetPanels()[KA.Panel].Dims.Y;
    if (Pair.Axis == 0) for (int32 Z = 0; Z < KA.SZ; ++Z) for (int32 Y = 0; Y < NY; ++Y) Add(FERCellWorld::CellIndex(KA, KA.SX - 1, Y, Z), FERCellWorld::CellIndex(KB, 0, Y, Z));
    else if (Pair.Axis == 2) for (int32 X = 0; X < KA.SX; ++X) for (int32 Y = 0; Y < NY; ++Y) Add(FERCellWorld::CellIndex(KA, X, Y, KA.SZ - 1), FERCellWorld::CellIndex(KB, X, Y, 0));
    else for (const FIntPoint& C : Pair.Contacts) Add(C.X, C.Y);
    Pair.Edges = Unique.Array(); Pair.Edges.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
    Pair.RevA = LA.Revision; Pair.RevB = LB.Revision;
    LastWork += Pair.Axis == 3 ? Pair.Contacts.Num() : (Pair.Axis == 0 ? KA.SZ : KA.SX) * NY;
}

#if WITH_DEV_AUTOMATION_TESTS
void FERCellSupport::EvaluateReference(const FERCellWorld& World, TArray<TArray<FERCellId>>& Unsupported)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ERCell_SupportEvaluate);
    const double Start = FPlatformTime::Seconds();
    Unsupported.Reset(); LastRelabelled = 0; LastWork = 0;
    const auto& Chunks = World.GetChunks();
    if (!bBuilt || Labels.Num() != Chunks.Num()) return;
    // Only chunks whose alive bits changed are relabelled, only their pairs refreshed.
    for (int32 C = 0; C < Chunks.Num(); ++C) if (Labels[C].Revision != Chunks[C].Revision) Relabel(World, C);
    for (auto& Pair : Pairs) if (Pair.RevA != Labels[Pair.A].Revision || Pair.RevB != Labels[Pair.B].Revision) RefreshEdges(World, Pair);
    TArray<int32> Offset; Offset.SetNum(Chunks.Num() + 1); Offset[0] = 0;
    for (int32 C = 0; C < Chunks.Num(); ++C) Offset[C + 1] = Offset[C] + Labels[C].Count;
    const int32 Nodes = Offset.Last();
    LastComponents = Nodes;
    TArray<int32> Parent; Parent.SetNumUninitialized(Nodes);
    for (int32 I = 0; I < Nodes; ++I) Parent[I] = I;
    for (const auto& Pair : Pairs)
        for (const FIntPoint& E : Pair.Edges)
        {
            const int32 RA = Find(Parent, Offset[Pair.A] + E.X), RB = Find(Parent, Offset[Pair.B] + E.Y);
            if (RA != RB) Parent[FMath::Max(RA, RB)] = FMath::Min(RA, RB);
        }
    TArray<uint8> Supported; Supported.Init(0, Nodes);
    for (int32 C = 0; C < Chunks.Num(); ++C)
        for (int32 L = 0; L < Labels[C].Count; ++L) if (Labels[C].Anchored[L]) Supported[Find(Parent, Offset[C] + L)] = 1;
    // Group unsupported nodes by root, then collect their cells in stable order.
    TMap<int32, int32> GroupOfRoot;
    for (int32 C = 0; C < Chunks.Num(); ++C)
    {
        const auto& L = Labels[C];
        bool bAny = false;
        for (int32 I = 0; I < L.Count && !bAny; ++I) bAny = !Supported[Find(Parent, Offset[C] + I)];
        if (!bAny) continue;
        const auto& K = Chunks[C];
        const int32 Items=L.Uniform ? K.SX*K.SZ*World.GetPanels()[K.Panel].Dims.Y:L.Label.IsEmpty() ? L.Sparse.Num():L.Label.Num();
        for(int32 Item=0;Item<Items;++Item)
        {
            const int32 I=!L.Uniform && L.Label.IsEmpty() ? L.Sparse[Item].X:Item;
            const int32 Label=L.Uniform ? 0:L.Label.IsEmpty() ? L.Sparse[Item].Y:L.Label[I];
            if(Label==INDEX_NONE)continue;
            const int32 Root=Find(Parent,Offset[C]+Label);
            if(Supported[Root])continue;
            int32* Group = GroupOfRoot.Find(Root);
            if (!Group) { Group = &GroupOfRoot.Add(Root, Unsupported.Num()); Unsupported.AddDefaulted(); }
            const int32 X = I % K.SX, Z = (I / K.SX) % K.SZ, Y = I / (K.SX * K.SZ);
            Unsupported[*Group].Add({K.Panel, K.X0 + X, Y, K.Z0 + Z});
        }
    }
    for (auto& Group : Unsupported) Group.Sort();
    Unsupported.Sort([](const TArray<FERCellId>& A, const TArray<FERCellId>& B) { return A[0] < B[0]; });
    LastMs = (FPlatformTime::Seconds() - Start) * 1000;
}
#endif

struct FERCellSupport::FEvaluation
{
    enum class EPhase:uint8 {Relabel,Sparse,Edges,Offsets,Nodes,Union,Anchors,Collect,Sort,Publish};
    EPhase Phase=EPhase::Relabel;
    FERCellWorld Snapshot;
    int32 Chunk=0,Cursor=0,Component=INDEX_NONE,PairIndex=0,Contact=0;
    bool Active=false,Anchored=false,Collecting=false;
    TArray<int32> Stack,Offset,Parent;
    TArray<uint8> Supported;
    TSet<FIntPoint> UniqueEdges;
    TMap<int32,int32> GroupOfRoot;
    TArray<TArray<FERCellId>> Groups,Result;
    TArray<FERCellId> SortScratch;
    int32 SortGroup=0,Width=1,Base=0,Left=0,LeftEnd=0,Right=0,RightEnd=0,Output=0;
    bool Merging=false;
    TArray<TPair<FERCellId,int32>> GroupHeap;
};

void FERCellSupport::BeginEvaluation(const FERCellWorld& World)
{
    Evaluation.Reset();LastMs=LastSliceMs=PeakSliceMs=0;LastWork=LastRelabelled=LastComponents=LastSliceWork=0;
    if(!bBuilt || Labels.Num()!=World.GetChunks().Num())return;
    Evaluation=MakeShared<FEvaluation>();
    Evaluation->Snapshot=World.MakeSupportSnapshot();
}

void FERCellSupport::Evaluate(const FERCellWorld& World,TArray<TArray<FERCellId>>& Unsupported)
{
    Unsupported.Reset();BeginEvaluation(World);
    while(IsEvaluating())StepEvaluation(Unsupported,1048576,0);
}

bool FERCellSupport::StepEvaluation(TArray<TArray<FERCellId>>& Unsupported,int32 WorkBudget,double TimeBudgetMs)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ERCell_SupportSlice);
    if(!Evaluation)return true;
    const double Start=FPlatformTime::Seconds();
    LastSliceWork=0;WorkBudget=FMath::Max(1,WorkBudget);
    auto& W=*Evaluation;
    const auto& World=W.Snapshot;const auto& Chunks=World.GetChunks();
    auto GroupLess=[](const TPair<FERCellId,int32>& A,const TPair<FERCellId,int32>& B){return A.Key<B.Key;};
    bool Complete=false;
    while(LastSliceWork<WorkBudget)
    {
        if(LastSliceWork && (LastSliceWork&127)==0 && TimeBudgetMs>0 && (FPlatformTime::Seconds()-Start)*1000>=TimeBudgetMs)break;
        ++LastSliceWork;
        switch(W.Phase)
        {
        case FEvaluation::EPhase::Relabel:
        {
            if(W.Chunk>=Chunks.Num()){W.Phase=FEvaluation::EPhase::Edges;W.Active=false;break;}
            const auto& C=Chunks[W.Chunk];const auto& P=World.GetPanels()[C.Panel];auto& L=Labels[W.Chunk];
            const int32 Bits=C.SX*C.SZ*P.Dims.Y;
            if(!W.Active)
            {
                if(L.Revision==C.Revision){++W.Chunk;break;}
                L.Revision=MAX_uint32;L.Count=0;L.Uniform=C.AliveCount==Bits;L.Label.Reset();L.Sparse.Reset();L.Anchored.Reset();++LastRelabelled;
                if(L.Uniform)
                {
                    L.Count=1;L.Anchored.Add(P.bAnchored || P.bIndestructible || ((P.bAnchorBottom || GroundPanel[C.Panel]) && C.Z0==0) || (P.bAnchorBottom && C.Z0*P.Cell.Z<P.AnchorHeight));
                    L.Revision=C.Revision;++W.Chunk;break;
                }
                if(!C.AliveCount){L.Label.Empty();L.Sparse.Empty();L.Revision=C.Revision;++W.Chunk;break;}
                L.Label.SetNumUninitialized(Bits);W.Cursor=0;W.Component=INDEX_NONE;W.Stack.Reset();W.Active=true;
                // Initialize a bounded page per iteration, including large hollow
                // skins whose logical chunk contains many empty depth layers.
                W.Contact=0;
            }
            if(W.Contact<Bits)
            {
                const int32 End=FMath::Min(Bits,W.Contact+64);
                while(W.Contact<End)L.Label[W.Contact++]=INDEX_NONE;
                break;
            }
            if(!W.Stack.IsEmpty())
            {
                const int32 I=W.Stack.Pop(EAllowShrinking::No),X=I%C.SX,Z=(I/C.SX)%C.SZ,Y=I/(C.SX*C.SZ);
                if(!W.Anchored)W.Anchored=World.IsAnchored({C.Panel,C.X0+X,Y,C.Z0+Z}) || (GroundPanel[C.Panel] && C.Z0+Z==0);
                const int32 Next[6][3]={{X-1,Y,Z},{X+1,Y,Z},{X,Y-1,Z},{X,Y+1,Z},{X,Y,Z-1},{X,Y,Z+1}};
                for(const auto& N:Next)
                {
                    if(N[0]<0 || N[0]>=C.SX || N[1]<0 || N[1]>=P.Dims.Y || N[2]<0 || N[2]>=C.SZ)continue;
                    const int32 J=FERCellWorld::CellIndex(C,N[0],N[1],N[2]);
                    if(L.Label[J]==INDEX_NONE && Bit(C,J)){L.Label[J]=W.Component;W.Stack.Add(J);}
                }
                break;
            }
            if(W.Component!=INDEX_NONE){L.Anchored.Add(W.Anchored);W.Component=INDEX_NONE;break;}
            if(W.Cursor<Bits)
            {
                const int32 Seed=W.Cursor++;
                if(L.Label[Seed]==INDEX_NONE && Bit(C,Seed))
                {W.Component=L.Count++;W.Anchored=false;L.Label[Seed]=W.Component;W.Stack.Add(Seed);}
                break;
            }
            if(P.bSkinned){W.Phase=FEvaluation::EPhase::Sparse;W.Cursor=0;L.Sparse.Reserve(C.AliveCount);}
            else {L.Revision=C.Revision;W.Active=false;++W.Chunk;}
            break;
        }
        case FEvaluation::EPhase::Sparse:
        {
            auto& L=Labels[W.Chunk];
            if(W.Cursor<L.Label.Num())
            {const int32 I=W.Cursor++;if(L.Label[I]!=INDEX_NONE)L.Sparse.Add({I,L.Label[I]});}
            else {L.Label.Empty();L.Revision=Chunks[W.Chunk].Revision;W.Active=false;++W.Chunk;W.Phase=FEvaluation::EPhase::Relabel;}
            break;
        }
        case FEvaluation::EPhase::Edges:
        {
            if(W.PairIndex>=Pairs.Num())
            {W.Offset.SetNumUninitialized(Chunks.Num()+1);W.Offset[0]=0;W.Chunk=0;W.Phase=FEvaluation::EPhase::Offsets;break;}
            auto& Pair=Pairs[W.PairIndex];const auto& LA=Labels[Pair.A];const auto& LB=Labels[Pair.B];
            if(!W.Active)
            {
                if(Pair.RevA==LA.Revision && Pair.RevB==LB.Revision){++W.PairIndex;break;}
                W.Contact=0;W.UniqueEdges.Reset();W.Active=true;
            }
            const auto& KA=Chunks[Pair.A];const auto& KB=Chunks[Pair.B];const int32 NY=World.GetPanels()[KA.Panel].Dims.Y;
            const int32 Count=Pair.Axis==3 ? Pair.Contacts.Num():(Pair.Axis==0 ? KA.SZ:KA.SX)*NY;
            if(W.Contact<Count)
            {
                const int32 I=W.Contact++;FIntPoint Contact;
                if(Pair.Axis==3)Contact=Pair.Contacts[I];
                else if(Pair.Axis==0)Contact={FERCellWorld::CellIndex(KA,KA.SX-1,I%NY,I/NY),FERCellWorld::CellIndex(KB,0,I%NY,I/NY)};
                else Contact={FERCellWorld::CellIndex(KA,I/NY,I%NY,KA.SZ-1),FERCellWorld::CellIndex(KB,I/NY,I%NY,0)};
                const int32 A=LA.At(Contact.X),B=LB.At(Contact.Y);
                if(A!=INDEX_NONE && B!=INDEX_NONE)W.UniqueEdges.Add({A,B});
                ++LastWork;break;
            }
            // Union always picks the smaller root, so edge iteration order cannot
            // affect the result; avoid a monolithic sort of a fragmented face.
            Pair.Edges=W.UniqueEdges.Array();Pair.RevA=LA.Revision;Pair.RevB=LB.Revision;
            W.Active=false;++W.PairIndex;break;
        }
        case FEvaluation::EPhase::Offsets:
            if(W.Chunk<Chunks.Num()){W.Offset[W.Chunk+1]=W.Offset[W.Chunk]+Labels[W.Chunk].Count;++W.Chunk;}
            else
            {
                LastComponents=W.Offset.Last();W.Parent.SetNumUninitialized(LastComponents);W.Supported.SetNumUninitialized(LastComponents);
                W.Cursor=0;W.Phase=FEvaluation::EPhase::Nodes;
            }
            break;
        case FEvaluation::EPhase::Nodes:
            if(W.Cursor<W.Parent.Num()){W.Parent[W.Cursor]=W.Cursor;W.Supported[W.Cursor++]=0;}
            else {W.PairIndex=W.Cursor=0;W.Phase=FEvaluation::EPhase::Union;}
            break;
        case FEvaluation::EPhase::Union:
            if(W.PairIndex>=Pairs.Num()){W.Chunk=W.Cursor=0;W.Phase=FEvaluation::EPhase::Anchors;break;}
            if(W.Cursor<Pairs[W.PairIndex].Edges.Num())
            {
                const auto& Pair=Pairs[W.PairIndex];const auto& E=Pair.Edges[W.Cursor++];
                const int32 RA=Find(W.Parent,W.Offset[Pair.A]+E.X),RB=Find(W.Parent,W.Offset[Pair.B]+E.Y);
                if(RA!=RB)W.Parent[FMath::Max(RA,RB)]=FMath::Min(RA,RB);
            }
            else {++W.PairIndex;W.Cursor=0;}
            break;
        case FEvaluation::EPhase::Anchors:
            if(W.Chunk>=Chunks.Num()){W.Chunk=W.Cursor=0;W.Phase=FEvaluation::EPhase::Collect;break;}
            if(W.Cursor<Labels[W.Chunk].Count)
            {const int32 L=W.Cursor++;if(Labels[W.Chunk].Anchored[L])W.Supported[Find(W.Parent,W.Offset[W.Chunk]+L)]=1;}
            else {++W.Chunk;W.Cursor=0;}
            break;
        case FEvaluation::EPhase::Collect:
        {
            if(W.Chunk>=Chunks.Num()){W.Phase=FEvaluation::EPhase::Sort;break;}
            const auto& L=Labels[W.Chunk];const auto& C=Chunks[W.Chunk];
            if(!W.Collecting)
            {
                if(W.Cursor>=L.Count){++W.Chunk;W.Cursor=0;break;}
                if(!W.Supported[Find(W.Parent,W.Offset[W.Chunk]+W.Cursor++)]){W.Collecting=true;W.Cursor=0;}
                break;
            }
            const int32 Items=L.Uniform ? C.SX*C.SZ*World.GetPanels()[C.Panel].Dims.Y:L.Label.IsEmpty() ? L.Sparse.Num():L.Label.Num();
            if(W.Cursor>=Items){++W.Chunk;W.Cursor=0;W.Collecting=false;break;}
            const int32 Item=W.Cursor++,I=!L.Uniform && L.Label.IsEmpty() ? L.Sparse[Item].X:Item;
            const int32 Label=L.Uniform ? 0:L.Label.IsEmpty() ? L.Sparse[Item].Y:L.Label[I];
            if(Label==INDEX_NONE)break;
            const int32 Root=Find(W.Parent,W.Offset[W.Chunk]+Label);if(W.Supported[Root])break;
            int32* Group=W.GroupOfRoot.Find(Root);
            if(!Group){Group=&W.GroupOfRoot.Add(Root,W.Groups.Num());W.Groups.AddDefaulted();}
            W.Groups[*Group].Add({C.Panel,C.X0+I%C.SX,I/(C.SX*C.SZ),C.Z0+(I/C.SX)%C.SZ});
            break;
        }
        case FEvaluation::EPhase::Sort:
        {
            if(W.SortGroup>=W.Groups.Num()){W.Phase=FEvaluation::EPhase::Publish;break;}
            auto& Cells=W.Groups[W.SortGroup];const int32 N=Cells.Num();
            if(W.Width>=N)
            {
                W.GroupHeap.HeapPush(TPair<FERCellId,int32>(Cells[0],W.SortGroup),GroupLess);
                ++W.SortGroup;W.Width=1;W.Base=0;W.Merging=false;break;
            }
            if(W.SortScratch.Num()!=N)W.SortScratch.SetNumUninitialized(N);
            if(W.Base>=N){Swap(Cells,W.SortScratch);W.Width*=2;W.Base=0;W.Merging=false;break;}
            if(!W.Merging)
            {
                W.Left=W.Base;W.LeftEnd=FMath::Min(N,W.Base+W.Width);W.Right=W.LeftEnd;
                W.RightEnd=FMath::Min(N,W.LeftEnd+W.Width);W.Output=W.Base;W.Merging=true;
            }
            if(W.Output<W.RightEnd)
            {
                const bool TakeLeft=W.Left<W.LeftEnd && (W.Right>=W.RightEnd || !(Cells[W.Right]<Cells[W.Left]));
                W.SortScratch[W.Output++]=Cells[TakeLeft ? W.Left++:W.Right++];
            }
            else {W.Base=W.RightEnd;W.Merging=false;}
            break;
        }
        case FEvaluation::EPhase::Publish:
            if(!W.GroupHeap.IsEmpty())
            {TPair<FERCellId,int32> Next;W.GroupHeap.HeapPop(Next,GroupLess,EAllowShrinking::No);W.Result.Add(MoveTemp(W.Groups[Next.Value]));}
            else {Unsupported=MoveTemp(W.Result);Complete=true;}
            break;
        }
        if(Complete)break;
    }
    LastSliceMs=(FPlatformTime::Seconds()-Start)*1000;LastMs+=LastSliceMs;PeakSliceMs=FMath::Max(PeakSliceMs,LastSliceMs);
    if(Complete)Evaluation.Reset();
    return Complete;
}

SIZE_T FERCellSupport::GetAllocatedBytes() const
{
    SIZE_T Bytes = Pairs.GetAllocatedSize() + Labels.GetAllocatedSize() + GroundPanel.GetAllocatedSize();
    for (const auto& L : Labels) Bytes += L.Label.GetAllocatedSize() + L.Sparse.GetAllocatedSize() + L.Anchored.GetAllocatedSize();
    for (const auto& P : Pairs) Bytes += P.Contacts.GetAllocatedSize() + P.Edges.GetAllocatedSize();
    if(Evaluation)
    {
        const auto& W=*Evaluation;
        // Conservative accounting: retained snapshot masks may still be shared
        // with the live world until the next write to their chunk.
        Bytes+=sizeof(FEvaluation)+W.Snapshot.GetAllocatedBytes()+W.Stack.GetAllocatedSize()+W.Offset.GetAllocatedSize()+W.Parent.GetAllocatedSize()+W.Supported.GetAllocatedSize();
        Bytes+=W.UniqueEdges.GetAllocatedSize()+W.GroupOfRoot.GetAllocatedSize()+W.Groups.GetAllocatedSize()+W.Result.GetAllocatedSize()+W.SortScratch.GetAllocatedSize()+W.GroupHeap.GetAllocatedSize();
        for(const auto& G:W.Groups)Bytes+=G.GetAllocatedSize();
        for(const auto& G:W.Result)Bytes+=G.GetAllocatedSize();
    }
    return Bytes;
}
