#pragma once
#include "CoreMinimal.h"
#include "ERMapPlayability.h"

struct FERProceduralConfig;
struct FERBlockRoomConfig;
struct FERCellSettings;

// Authority-side choice among a few layouts of one seed. Only the winning index replicates,
// so clients rebuild that plan and never analyse anything. See docs/PROCEDURAL_MAP_PLAYABILITY.md.
struct FERMapSelectionProfile
{
    int32 Candidates=4;
    // Safety valve, set well above the measured cost. Hitting it is logged: it is the only way
    // machine speed can change the outcome for a seed.
    double BudgetMs=4000;
    // Hard rules. A candidate breaking one is never chosen over one that does not.
    bool bRequireWholeGraph=false; // Exact graphs only: a route graph cannot prove a gap.
    int32 MinPaths=0;
    bool bForbidSpawnSight=false; // Known solids only: unknown ones stay undecided.
    bool bPreferBreachSafe=true; // Costs one damaged sight pass per best candidate.
    // Preferred ranges, unset by default. Each one met adds a point, so nothing is maximised.
    FFloatInterval Paths,SpawnSight,LateralRatio,OpenShare;
};
struct FERMapCandidate
{
    int32 Index=0;
    bool bBuilt=false;
    TArray<FString> Rejected; // Empty on a candidate that passes every hard rule.
    int32 Preference=0;
    double BuildMs=0,AnalysisMs=0;
    double LateralRatio=-1,OpenShare=0;
    int32 Paths=0,VisiblePairs=0,UnknownSolids=0;
    int32 BreachPairs=INDEX_NONE; // Spawn pairs with a central breach. Read on the best candidates only.
};
struct FERMapSelection
{
    TArray<FERMapCandidate> Candidates;
    int32 Chosen=0;
    bool bFallback=false,bBudgetHit=false;
    double TotalMs=0;
    FString Line() const;
};

namespace ERMapSelection
{
    constexpr int32 MaxCandidates=16;
    FERMapSelectionProfile ProfileFor(EERMapArchetype Archetype,int32 ExpectedTeamSize);
    // Hard rules and ranges for one analysed candidate.
    void Judge(FERMapCandidate& Candidate,const FERPlayabilityReport& Report,const FERMapSelectionProfile& Profile);
    // Best preference among clean candidates. The plain seed keeps any tie, so a map only moves
    // for a reason; the seed breaks other ties. A fallback must still block initial spawn sight;
    // return INDEX_NONE when no built candidate satisfies that mandatory rule.
    int32 Decide(const TArray<FERMapCandidate>& Candidates,int32 Seed,bool& bFallback);
    // One more point for each best clean candidate that a central breach leaves no worse. Layout
    // sight can overstate but never misses a line, so this only ever passes over a candidate.
    void PreferBreachSafe(TArray<FERMapCandidate>& Candidates,TFunctionRef<int32(int32 Index)> BreachPairs);
    FERMapSelection Select(const FERProceduralConfig& Config,const FERBlockRoomConfig& Blocks,const FERCellSettings& Cells,const FERMapSelectionProfile& Profile);
}
