#include "ERMapSelection.h"
#include "ERProceduralMap.h"

FString FERMapSelection::Line() const
{
    FString Text=FString::Printf(TEXT("chosen=%d fallback=%d budget_hit=%d total_ms=%.1f"),Chosen,bFallback,bBudgetHit,TotalMs);
    for(const auto& C:Candidates)
    {
        Text+=FString::Printf(TEXT(" [%d %s pref=%d breach=%d"),C.Index,!C.bBuilt ? TEXT("no_layout"):C.Rejected.IsEmpty() ? TEXT("clean"):TEXT("rejected"),C.Preference,C.BreachPairs);
        for(const auto& Reason:C.Rejected)Text+=TEXT(" ")+Reason;
        Text+=TEXT("]");
    }
    return Text;
}

FERMapSelectionProfile ERMapSelection::ProfileFor(EERMapArchetype Archetype,int32 ExpectedTeamSize)
{
    // Initial spawn occlusion is mandatory for every archetype and team size.
    // Optional candidate selection only optimizes the remaining tactical preferences.
    FERMapSelectionProfile P;P.bForbidSpawnSight=true;P.SpawnSight=FFloatInterval(0,0);
    constexpr float MostPaths=8;
    switch(Archetype)
    {
    case EERMapArchetype::Rooms:
        // Sealed shell: exact graph, no unknown solids. Builds cost under 4ms.
        P.Candidates=6;P.bRequireWholeGraph=true;P.MinPaths=2;P.bForbidSpawnSight=true;
        P.Paths=FFloatInterval(3,MostPaths);
        break;
    case EERMapArchetype::CentralBuilding:
        // The common generator already proved opaque cover between all starts.
        P.Candidates=6;
        P.Paths=FFloatInterval(2,MostPaths);P.LateralRatio=FFloatInterval(.99f,1.6f);P.OpenShare=FFloatInterval(.3f,.9f);
        break;
    case EERMapArchetype::MiddleEasternAlleys:
        // House skins are unknown solids: a clear sight reading is proof, a blocked one is not.
        P.SpawnSight=FFloatInterval(0,0);P.Paths=FFloatInterval(3,MostPaths);
        break;
    case EERMapArchetype::Village:
        P.Candidates=6;P.SpawnSight=FFloatInterval(0,0);P.Paths=FFloatInterval(2,MostPaths);P.LateralRatio=FFloatInterval(.99f,1.6f);P.OpenShare=FFloatInterval(.3f,.85f);
        break;
    default:
        // Fire Training: hundreds of unknown solids and the slowest builds. Nothing here can tell
        // candidates apart, so it keeps the plain seed and pays nothing.
        P.Candidates=1;
        break;
    }
    return P;
}

void ERMapSelection::Judge(FERMapCandidate& C,const FERPlayabilityReport& R,const FERMapSelectionProfile& P)
{
    auto Value=[&](const TCHAR* Code,const TCHAR* Name,double Default=0){const auto* M=R.Find(Code);return M ? M->Value(Name,Default):Default;};
    const auto* Whole=R.Find(TEXT("CONNECTED_ZONES"));
    const auto* Sight=R.Find(TEXT("INITIAL_SPAWN_LOS"));
    C.Paths=int32(FMath::Min(Value(TEXT("BASE_EXIT_PATHS"),TEXT("alpha_independent_paths")),Value(TEXT("BASE_EXIT_PATHS"),TEXT("omega_independent_paths"))));
    C.VisiblePairs=int32(Value(TEXT("INITIAL_SPAWN_LOS"),TEXT("visible_pairs")));
    C.UnknownSolids=int32(Value(TEXT("INITIAL_SPAWN_LOS"),TEXT("unknown_solids")));
    C.LateralRatio=Value(TEXT("ROUTE_DIVERSITY"),TEXT("lateral_ratio"),-1);
    C.OpenShare=Value(TEXT("ROUTE_EXPOSURE"),TEXT("open_share"));
    C.Rejected.Reset();C.Preference=0;
    if(P.bRequireWholeGraph && R.Graph.Coverage==EERGraphCoverage::Exact && (!Whole || Whole->Status==EERPlayabilityStatus::Fail))C.Rejected.Add(TEXT("split_graph"));
    if(P.MinPaths>0 && C.Paths<P.MinPaths)C.Rejected.Add(FString::Printf(TEXT("paths_%d"),C.Paths));
    // Layout sight overstates what real collision shows, so this rejects only what known solids leave open.
    if(P.bForbidSpawnSight && (!Sight || C.VisiblePairs>0 || Sight->Value(TEXT("open_shot_pairs"),0)>0))C.Rejected.Add(FString::Printf(TEXT("spawn_sight_%d"),C.VisiblePairs));
    // Sight outweighs the rest together: no line on the layout means no line in real collision.
    constexpr int32 SightWeight=4;
    C.Preference=SightWeight*int32(P.SpawnSight.Contains(C.VisiblePairs))+int32(P.Paths.Contains(C.Paths))+int32(P.LateralRatio.Contains(C.LateralRatio))+int32(P.OpenShare.Contains(C.OpenShare));
}

int32 ERMapSelection::Decide(const TArray<FERMapCandidate>& Candidates,int32 Seed,bool& bFallback)
{
    int32 Best=-1;TArray<int32> Ties;
    for(const auto& C:Candidates)
    {
        if(!C.bBuilt || !C.Rejected.IsEmpty())continue;
        if(C.Preference>Best){Best=C.Preference;Ties.Reset();}
        if(C.Preference==Best)Ties.Add(C.Index);
    }
    bFallback=Ties.IsEmpty();
    if(Ties.Contains(0))return 0;
    if(!Ties.IsEmpty())return Ties[GetTypeHash(Seed)%uint32(Ties.Num())];
    for(const auto& C:Candidates)if(C.bBuilt && !C.Rejected.ContainsByPredicate([](const FString& Why){return Why.StartsWith(TEXT("spawn_sight_"));}))return C.Index;
    return INDEX_NONE;
}

void ERMapSelection::PreferBreachSafe(TArray<FERMapCandidate>& Candidates,TFunctionRef<int32(int32)> BreachPairs)
{
    int32 Best=-1;
    for(const auto& C:Candidates)if(C.bBuilt && C.Rejected.IsEmpty())Best=FMath::Max(Best,C.Preference);
    for(auto& C:Candidates)if(C.bBuilt && C.Rejected.IsEmpty() && C.Preference==Best)
    {
        C.BreachPairs=BreachPairs(C.Index);
        if(C.BreachPairs<=C.VisiblePairs)++C.Preference;
    }
}

FERMapSelection ERMapSelection::Select(const FERProceduralConfig& Config,const FERBlockRoomConfig& Blocks,const FERCellSettings& Cells,const FERMapSelectionProfile& Profile)
{
    FERMapSelection Out;
    const double Start=FPlatformTime::Seconds();
    if(!Config.TemplateAsset || Config.ExpectedTeamSize<=0 || Profile.Candidates<=1)return Out;
    const auto Policy=ERPlayability::PolicyFor(Config.TemplateAsset->Archetype,Config.ExpectedTeamSize);
    const int32 Count=FMath::Clamp(Profile.Candidates,1,MaxCandidates);
    TMap<int32,TPair<FERProceduralLayout,FERPlayabilityReport>> Plans;
    for(int32 Index=0;Index<Count;++Index)
    {
        if(Index>0 && (FPlatformTime::Seconds()-Start)*1000>=Profile.BudgetMs){Out.bBudgetHit=true;break;}
        auto& C=Out.Candidates.AddDefaulted_GetRef();C.Index=Index;
        FERProceduralConfig Try=Config;Try.MapCandidate=Index;
        double Clock=FPlatformTime::Seconds();
        auto& Plan=Plans.Add(Index);C.bBuilt=ERProcedural::Build(Try,Blocks,Plan.Key,Cells);
        C.BuildMs=(FPlatformTime::Seconds()-Clock)*1000;
        if(!C.bBuilt)continue;
        Clock=FPlatformTime::Seconds();
        Plan.Value=ERPlayability::Analyze(Plan.Key,Try,Policy);Judge(C,Plan.Value,Profile);
        C.AnalysisMs=(FPlatformTime::Seconds()-Clock)*1000;
    }
    if(Profile.bPreferBreachSafe && !Out.bBudgetHit)
    {
        auto Breach=Policy;Breach.Stress=EERStressScenario::CentralBreach;
        PreferBreachSafe(Out.Candidates,[&](int32 Index)
        {
            const auto& Plan=Plans[Index];const auto Read=ERPlayability::Stress(Plan.Key,Plan.Value,Breach);
            return Read.IsEmpty() ? 0:Read[0].VisiblePairsDamaged;
        });
    }
    Out.Chosen=Decide(Out.Candidates,Config.Seed,Out.bFallback);
    Out.TotalMs=(FPlatformTime::Seconds()-Start)*1000;
    return Out;
}
