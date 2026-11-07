#pragma once
#include "CoreMinimal.h"
#include "ERMatch.h"

// Server-side respawn choice among starts that already passed every placement filter.
// Runs per respawn, never per tick. See docs/PROCEDURAL_MAP_PLAYABILITY.md.
struct FERSpawnThreat { FVector Eye=FVector::ZeroVector; };
struct FERSpawnHazard { FVector Location=FVector::ZeroVector,Forward=FVector::ForwardVector;float Radius=0,HalfAngle=0; };
struct FERSpawnRiskTuning
{
    float NearDistance=3000; // What a sprinter covers during spawn protection.
    float MemorySeconds=15,SectorRadius=600;
    float SightPenalty=1000,HazardPenalty=500,ProximityPenalty=200,DeathPenalty=150,RepeatPenalty=100;
    float Comparable=40; // Scores this close to the best are the same choice.
    float EyeOffset=64;
    int32 MaxCandidates=12,MaxTraces=96;
};
struct FERSpawnEvent { FVector Location=FVector::ZeroVector;double Time=0; };
// Recent outcomes by team. Bounded, and forgotten when the arena changes.
struct FERSpawnMemory
{
    TArray<FERSpawnEvent> Deaths[2],Starts[2];
    void Add(TArray<FERSpawnEvent>& Events,FVector Location,double Time);
    void Reset();
};
struct FERSpawnCandidate
{
    FVector Location=FVector::ZeroVector;
    float Risk=0;
    int32 Seen=0; // Enemies with an open shot on this standing capsule.
    int32 Unknown=0; // No open sample found, but not every sample was tested.
    bool bInHazard=false;
};
namespace ERSpawnRisk
{
    FDM_API bool Enabled(const UWorld* World);
    FDM_API FERSpawnRiskTuning Tuning(const UWorld* World);
    // Living enemies, nearest last-known state from the server, and their live devices.
    FDM_API void GatherThreats(UWorld* World,EERTeam Team,TArray<FERSpawnThreat>& Threats,TArray<FERSpawnHazard>& Hazards);
    // Current collision, with part of the shared budget reserved for completing
    // promising candidates. Unchecked enemies contribute conservative sight risk.
    FDM_API int32 Score(UWorld* World,TArray<FERSpawnCandidate>& Candidates,const TArray<FERSpawnThreat>& Threats,const TArray<FERSpawnHazard>& Hazards,
        const TArray<FERSpawnEvent>& Deaths,const TArray<FERSpawnEvent>& Starts,double Now,const FERSpawnRiskTuning& Tuning);
    // Random among comparable best scores, so spawns stay unpredictable. INDEX_NONE when empty.
    FDM_API int32 Pick(const TArray<FERSpawnCandidate>& Candidates,const FERSpawnRiskTuning& Tuning,TFunctionRef<int32(int32)> Random);
}
