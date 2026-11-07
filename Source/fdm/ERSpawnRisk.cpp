#include "ERSpawnRisk.h"
#include "ERCharacter.h"
#include "ERClaymore.h"
#include "ERServerSettings.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void FERSpawnMemory::Add(TArray<FERSpawnEvent>& Events,FVector Location,double Time)
{
    if(Events.Num()>=32)Events.RemoveAt(0);
    Events.Add({Location,Time});
}
void FERSpawnMemory::Reset()
{
    for(int32 Team=0;Team<2;++Team){Deaths[Team].Reset();Starts[Team].Reset();}
}
bool ERSpawnRisk::Enabled(const UWorld* World){return ERServer::Get(World,"Health.SpawnScoring",1)>.5f;}
FERSpawnRiskTuning ERSpawnRisk::Tuning(const UWorld* World)
{
    FERSpawnRiskTuning T;
    // km/h to cm/s. A floor keeps proximity meaningful with protection disabled.
    const float Sprint=ERServer::Get(World,"Move.Sprint",27)/3.6f*100;
    T.NearDistance=Sprint*FMath::Max(ERServer::Get(World,"Health.Protection",4),1.f);
    return T;
}
void ERSpawnRisk::GatherThreats(UWorld* World,EERTeam Team,TArray<FERSpawnThreat>& Threats,TArray<FERSpawnHazard>& Hazards)
{
    for(TActorIterator<AERCharacter> It(World);It;++It)
    {
        const auto* State=It->GetPlayerState<AERPlayerState>();
        if(!State || State->Team==Team || State->Team==EERTeam::None || !It->Health || It->Health->bDead || !It->bNetworkReady)continue;
        Threats.Add({It->Camera ? It->Camera->GetComponentLocation():It->GetActorLocation()+FVector(0,0,64)});
    }
    for(TActorIterator<AERClaymore> It(World);It;++It)
        if(It->PlacedTeam!=uint8(Team) && (It->State==EERClaymoreState::Arming || It->State==EERClaymoreState::Armed))
            Hazards.Add({It->GetActorLocation(),It->GetActorForwardVector(),It->DamageRadius,It->ConeHalfAngle});
}
int32 ERSpawnRisk::Score(UWorld* World,TArray<FERSpawnCandidate>& Candidates,const TArray<FERSpawnThreat>& Threats,const TArray<FERSpawnHazard>& Hazards,
    const TArray<FERSpawnEvent>& Deaths,const TArray<FERSpawnEvent>& Starts,double Now,const FERSpawnRiskTuning& T)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ERSpawnRiskScore);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERSpawnRisk),false);
    if(World)for(TActorIterator<APawn> Pawn(World);Pawn;++Pawn)Query.AddIgnoredActor(*Pawn);
    struct FProgress
    {
        TArray<int32> Nearest;
        int32 Threat=0,Sample=0;
        float BaseRisk=0;
    };
    TArray<FProgress> Progress;Progress.SetNum(Candidates.Num());
    const FVector Samples[]={FVector(0,0,T.EyeOffset),FVector(0,0,10)};
    auto Recent=[&](const TArray<FERSpawnEvent>& Events,FVector Location,float Radius)
    {
        float Weight=0;
        for(const auto& Event:Events)if(FVector::DistSquared(Event.Location,Location)<=Radius*Radius)
            Weight=FMath::Max(Weight,1-float(Now-Event.Time)/T.MemorySeconds);
        return Weight;
    };
    for(int32 Index=0;Index<Candidates.Num();++Index)
    {
        auto& C=Candidates[Index];auto& P=Progress[Index];
        C.Risk=0;C.Seen=0;C.Unknown=Threats.Num();C.bInHazard=false;
        float Proximity=0;
        for(int32 Threat=0;Threat<Threats.Num();++Threat)
        {
            P.Nearest.Add(Threat);
            // Straight distance deliberately overstates closeness through walls.
            Proximity+=FMath::Square(FMath::Max(0.f,1-float(FVector::Dist(Threats[Threat].Eye,C.Location))/T.NearDistance));
        }
        P.Nearest.Sort([&](int32 A,int32 B)
        {
            const double DA=FVector::DistSquared(Threats[A].Eye,C.Location),DB=FVector::DistSquared(Threats[B].Eye,C.Location);
            return DA==DB ? A<B:DA<DB;
        });
        P.BaseRisk=FMath::Min(Proximity,2.f)*T.ProximityPenalty;
        for(const auto& H:Hazards)
        {
            const FVector Delta=C.Location-H.Location;
            if(Delta.SizeSquared()<=H.Radius*H.Radius && FVector::DotProduct(Delta.GetSafeNormal(),H.Forward)>=FMath::Cos(FMath::DegreesToRadians(H.HalfAngle)))C.bInHazard=true;
        }
        P.BaseRisk+=C.bInHazard ? T.HazardPenalty:0;
        P.BaseRisk+=Recent(Deaths,C.Location,T.SectorRadius)*T.DeathPenalty+Recent(Starts,C.Location,10)*T.RepeatPenalty;
    }
    int32 Traces=0;
    const int32 Budget=FMath::Max(0,T.MaxTraces);
    auto TraceNext=[&](int32 Index)
    {
        auto& C=Candidates[Index];auto& P=Progress[Index];
        if(!World || C.Unknown==0 || Traces>=Budget)return false;
        FHitResult Hit;
        const bool Open=!World->LineTraceSingleByChannel(Hit,Threats[P.Nearest[P.Threat]].Eye,C.Location+Samples[P.Sample],ECC_GameTraceChannel1,Query);
        ++Traces;++P.Sample;
        if(Open || P.Sample==UE_ARRAY_COUNT(Samples))
        {
            C.Seen+=Open ? 1:0;--C.Unknown;++P.Threat;P.Sample=0;
        }
        return true;
    };
    // First survey every start fairly. Reserve at least half for completing the
    // best candidates; a distant visible enemy must not become an implicit pass.
    const int32 SurveyPerCandidate=Budget/(2*FMath::Max(1,Candidates.Num()));
    for(int32 Index=0;Index<Candidates.Num();++Index)
        for(int32 Sample=0;Sample<SurveyPerCandidate && TraceNext(Index);++Sample) {}
    while(World && Traces<Budget)
    {
        int32 Best=INDEX_NONE;float BestLowerRisk=MAX_flt;
        for(int32 Index=0;Index<Candidates.Num();++Index)
        {
            const auto& C=Candidates[Index];if(C.Unknown==0)continue;
            const float LowerRisk=Progress[Index].BaseRisk+C.Seen*T.SightPenalty;
            if(Best==INDEX_NONE || LowerRisk<BestLowerRisk ||
                (LowerRisk==BestLowerRisk && C.Unknown<Candidates[Best].Unknown))
            {Best=Index;BestLowerRisk=LowerRisk;}
        }
        if(Best==INDEX_NONE || !TraceNext(Best))break;
    }
    for(int32 Index=0;Index<Candidates.Num();++Index)
    {
        auto& C=Candidates[Index];
        // Unknown is an upper-bound threat, never a confirmed covered start.
        C.Risk=Progress[Index].BaseRisk+(C.Seen+C.Unknown)*T.SightPenalty;
    }
    return Traces;
}
int32 ERSpawnRisk::Pick(const TArray<FERSpawnCandidate>& Candidates,const FERSpawnRiskTuning& T,TFunctionRef<int32(int32)> Random)
{
    if(Candidates.IsEmpty())return INDEX_NONE;
    float Best=MAX_flt;for(const auto& C:Candidates)Best=FMath::Min(Best,C.Risk);
    int32 LeastUnknown=MAX_int32;
    for(const auto& C:Candidates)if(C.Risk<=Best+T.Comparable)LeastUnknown=FMath::Min(LeastUnknown,C.Unknown);
    TArray<int32> Tied;
    for(int32 I=0;I<Candidates.Num();++I)
        if(Candidates[I].Risk<=Best+T.Comparable && Candidates[I].Unknown==LeastUnknown)Tied.Add(I);
    return Tied[Random(Tied.Num())];
}
