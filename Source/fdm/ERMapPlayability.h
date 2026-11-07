#pragma once
#include "CoreMinimal.h"
#include "ERMapTemplate.h"

struct FERProceduralLayout;
struct FERProceduralConfig;

// Tactical reading of a generated layout. Analysis is read-only: it never consumes geometry
// RNG and never changes the accepted plan. Positions are map-local centimetres; only JSON
// output is in metres. See docs/PROCEDURAL_MAP_PLAYABILITY.md.
enum class EERPlayabilityMode : uint8 { Off, ReportOnly };
enum class EERPlayabilityStatus : uint8 { Pass, Warn, Fail, NotEvaluated };
enum class EERPlayabilityStage : uint8 { Layout, Physical };
enum class EERTacticalNodeKind : uint8 { Spawn, Room, Junction, Waypoint, Upper };
enum class EERTacticalMove : uint8 { Walk, Ramp, Stairs, Ladder };
// Exact: the graph is the only way to move. Routes: validated routes through open space,
// so a missing link may be an adapter gap instead of a map defect.
enum class EERGraphCoverage : uint8 { None, Routes, Exact };

struct FERTacticalNode
{
    FVector Position=FVector::ZeroVector;
    EERTacticalNodeKind Kind=EERTacticalNodeKind::Waypoint;
    FString Source; // Stable metadata origin, such as "room3" or "patrol.4".
    int32 Team=INDEX_NONE; // 0 Alpha, 1 Omega.
    bool bBase=false;
};
struct FERTacticalEdge
{
    int32 A=INDEX_NONE,B=INDEX_NONE;
    float Length=0,Width=0; // Width 0: unknown.
    EERTacticalMove Move=EERTacticalMove::Walk;
    FString Source;
    TArray<FVector> Path; // Pawn-centre samples, swept by the physical stage.
    bool bConfirmed=false,bBlocked=false;
};
struct FERTacticalGraph
{
    TArray<FERTacticalNode> Nodes;
    TArray<FERTacticalEdge> Edges;
    EERGraphCoverage Coverage=EERGraphCoverage::None;
    EERTemplateSymmetry Symmetry=EERTemplateSymmetry::None;
    // True when walls really bound the base, so counted exits mean something.
    bool bExitsBounded=false;
    FString Note;
    int32 AddEdge(int32 A,int32 B,EERTacticalMove Move,const FString& Source,bool Confirmed,float Width=0);
    void SetPath(int32 Edge,TArray<FVector> Path);
};

// Damage states a policy reads. Each needs something that can really break on that archetype.
enum class EERStressScenario : uint8 { None=0,CentralBreach=1,CoverRemoved=2,HighSightline=4,WalkwayLost=8,All=15 };
ENUM_CLASS_FLAGS(EERStressScenario)

struct FERPlayabilityPolicy
{
    int32 MinIndependentPaths=2;
    float MergeRadius=60,LinkRadius=500,SpawnLinkRadius=800,BaseMargin=400;
    float EyeOffset=77.5f; // AERCharacter standing eye above its capsule centre.
    int32 MaxSpawnPairs=100; // Legacy policy field; spawn checks always enumerate every pair.
    int32 PathLimit=8; // Flow stops here: more independent paths change nothing.
    // Bounded sight reading of routes: rays stay under MaxVantages*2*MaxExits+MaxObservers*MaxRouteSamples.
    float VantageHeight=200,RouteStep=400;
    int32 MaxVantages=12,MaxExits=12,MaxObservers=16,MaxRouteSamples=24;
    float CoverReach=2000; // Direct walking approach to an authored hiding site, not navigation distance.
    int32 MaxCoverCandidates=4; // Nearest sites examined per route sample.
    EERStressScenario Stress=EERStressScenario::All;
};

struct FERPlayabilityViolation
{
    EERPlayabilityStatus Severity=EERPlayabilityStatus::Warn;
    FString Message;
    TArray<int32> Nodes,Edges;
    TArray<FVector> Positions;
};
struct FERPlayabilityMetric
{
    FString Code;
    EERPlayabilityStatus Status=EERPlayabilityStatus::NotEvaluated;
    EERPlayabilityStage Stage=EERPlayabilityStage::Layout;
    FString Reason;
    TArray<TPair<FString,double>> Values; // Insertion order is the report order.
    TArray<FERPlayabilityViolation> Violations;
    double Value(const TCHAR* Name,double Default=0) const;
};
// One deterministic damage state, read on the layout. Only real destruction is modelled: panels
// opened here can lose those cells in play, and anything else stays not applicable.
struct FERStressResult
{
    FString Scenario,Reason;
    bool bApplicable=false;
    TArray<TPair<int32,FBox2D>> Openings; // Layout panel index and panel-local X/Z rectangle.
    int32 VisiblePairs=0,VisiblePairsDamaged=0;
    int32 Paths=0,PathsDamaged=0;
    double ExitShare=0,ExitShareDamaged=0;
    double OpenShare=0,OpenShareDamaged=0;
};
struct FERPlayabilityReport
{
    int32 Seed=0,ExpectedTeamSize=0,TemplateRevision=0;
    FString TemplateId;
    EERMapArchetype Archetype=EERMapArchetype::LegacyAssembly;
    EERPlayabilityStage Stage=EERPlayabilityStage::Layout;
    FERTacticalGraph Graph;
    TArray<FERPlayabilityMetric> Metrics;
    TArray<FERStressResult> Stress; // Layout readings: the physical stage keeps them as they are.
    TArray<TPair<FString,double>> TimingsMs;
    FERPlayabilityMetric* Find(const TCHAR* Code);
    const FERPlayabilityMetric* Find(const TCHAR* Code) const;
    EERPlayabilityStatus Overall() const;
    int32 UnevaluatedMetrics() const;
};

namespace ERPlayability
{
    constexpr int32 ReportVersion=4;
    // -ERMapPlayability=ReportOnly on the authority; always Off in Shipping.
    EERPlayabilityMode Mode();
    FERPlayabilityPolicy PolicyFor(EERMapArchetype Archetype,int32 ExpectedTeamSize);

    // Mandatory initial-map rule, independent of optional reporting/selection.
    // Layout proof uses visible opaque known solids; unknown art/terrain never proves safety.
    bool InitialSpawnsOccluded(const FERProceduralLayout& Layout);
    bool InitialSpawnsOccludedInWorld(UWorld* World,const FTransform& MapFrame,const FERProceduralLayout& Layout);

    // Graph queries. Blocked edges do not exist. Node order is the tie-break.
    TArray<int32> Components(const FERTacticalGraph& Graph);
    int32 DisjointPaths(const FERTacticalGraph& Graph,const TArray<int32>& From,const TArray<int32>& To,int32 Limit);
    // Nodes every From-To path crosses, nearest From first.
    TArray<int32> CommonChokes(const FERTacticalGraph& Graph,const TArray<int32>& From,const TArray<int32>& To);
    float PathLength(const FERTacticalGraph& Graph,const TArray<int32>& From,const TArray<int32>& To);
    // Next cheapest way between the sets once the first path's nodes outside them are gone. -1: none.
    float LateralPathLength(const FERTacticalGraph& Graph,const TArray<int32>& From,const TArray<int32>& To);
    // Samples follow the same edge polylines as collision, including bends and reverse travel.
    TArray<FVector> SampleRoute(const FERTacticalGraph& Graph,const TArray<int32>& Path,const FERPlayabilityPolicy& Policy,float& Step);
    // Spatially distributed samples retain both members of a recorded symmetry pair.
    TArray<int32> SelectObservers(const FERTacticalGraph& Graph,const TArray<int32>& Candidates,int32 Limit);

    FERTacticalGraph BuildGraph(const FERProceduralLayout& Layout,EERMapArchetype Archetype,const FERPlayabilityPolicy& Policy);
    // Fills the topology metrics of a report whose graph is already set.
    void EvaluateGraph(FERPlayabilityReport& Report,const FERPlayabilityPolicy& Policy);
    // Layout stage: needs a complete layout, cell panels included.
    FERPlayabilityReport Analyze(const FERProceduralLayout& Layout,const FERProceduralConfig& Config,const FERPlayabilityPolicy& Policy);
    // Physical stage: real collision after the map is generated. Replaces INITIAL_SPAWN_LOS,
    // sweeps a capsule along every walking edge and reevaluates the topology.
    void ConfirmInWorld(FERPlayabilityReport& Report,class UWorld* World,const FTransform& MapFrame,const FERProceduralLayout& Layout,const FERPlayabilityPolicy& Policy);

    // Layout stage again, once per scenario: needs cell panels. Report-only.
    TArray<FERStressResult> Stress(const FERProceduralLayout& Layout,const FERPlayabilityReport& Report,const FERPlayabilityPolicy& Policy);

    // Stable text: same seed, version and policy give the same bytes without timings.
    FString ToJson(const FERPlayabilityReport& Report,bool bTimings);
    FString Summary(const FERPlayabilityReport& Report);
    const TCHAR* ArchetypeName(EERMapArchetype Archetype);
    const TCHAR* StatusName(EERPlayabilityStatus Status);
    bool Save(const FERPlayabilityReport& Report,const FString& Directory);
}
