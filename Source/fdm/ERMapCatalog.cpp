#include "ERMapCatalog.h"
#include "ERServerSettings.h"
#include "ERLobbyRules.h"

namespace ERMaps
{
TConstArrayView<FERMapCatalogEntry> All()
{
    static const FERMapCatalogEntry Entries[]={
        {TEXT("central_building"),TEXT("Central Building"),TEXT("/Game/EclipseRecoil/Maps/L_CentralBuilding"),true,true},
        {TEXT("depot"),TEXT("Depot · Reference"),TEXT("/Game/EclipseRecoil/Maps/L_Depot"),false,false},
        {TEXT("middle_eastern_alleys"),TEXT("Alleys"),TEXT("/Game/EclipseRecoil/Maps/L_MiddleEasternAlleys"),true,true},
        {TEXT("fire_training"),TEXT("Fire Training"),TEXT("/Game/EclipseRecoil/Maps/L_FireTraining"),true,true},
        {TEXT("rooms"),TEXT("Rooms"),TEXT("/Game/EclipseRecoil/Maps/L_Rooms"),true,false},
        {TEXT("village"),TEXT("Village"),TEXT("/Game/EclipseRecoil/Maps/L_Village"),true,true}};
    return Entries;
}
const FERMapCatalogEntry& At(int32 Index){const auto Entries=All();return Entries[FMath::Clamp(Index,0,Entries.Num()-1)];}
TArray<FString> Labels(){TArray<FString> Result;for(const auto& E:All())Result.Add(E.Label);return Result;}
bool Enabled(int32 Index,const FERServerRules* Rules)
{ return All().IsValidIndex(Index) && At(Index).InRotation && (!Rules || Rules->Get(FName(*(FString(TEXT("Map."))+At(Index).Id)))>0); }
TArray<FName> Rotation(const FERServerRules* Rules){TArray<FName> Result;for(int32 I=0;I<All().Num();++I)if(Enabled(I,Rules))Result.Add(FName(At(I).Package));return Result;}
void RecordRoundMap(FName CurrentPackage,TArray<FName>& Played,const FERServerRules* Rules)
{
    const auto Pool=Rotation(Rules);
    Played.RemoveAll([&](FName Map){return !Pool.Contains(Map);});
    if(!Pool.Contains(CurrentPackage) || (!Played.IsEmpty() && Played.Last()==CurrentPackage))return;
    // The next actual arrival begins a new cycle after the pool was exhausted.
    if(!Pool.ContainsByPredicate([&](FName Map){return !Played.Contains(Map);}))Played.Reset();
    Played.AddUnique(CurrentPackage);
}
TArray<FName> NextRoundMaps(FName CurrentPackage,TConstArrayView<FName> Played,const FERServerRules* Rules)
{
    // A fixed reference map stays out of the pool: its rounds restart in place.
    for(const auto& Entry:All())if(CurrentPackage==FName(Entry.Package) && !Entry.InRotation)return {};
    TArray<FName> Result=Rotation(Rules);
    Result.RemoveAll([&](FName Map){return Played.Contains(Map);});
    // Refill only once every production template has been played. Also avoid
    // an immediate repeat across the boundary between two complete cycles.
    if(Result.IsEmpty())Result=Rotation(Rules);
    Result.Remove(CurrentPackage);
    return Result;
}
TArray<int32> LobbyLeaders(TConstArrayView<int32> Votes,const FERServerRules* Rules)
{
    TArray<int32> Eligible;
    for (int32 I=0; I<All().Num(); ++I) if (Enabled(I,Rules)) Eligible.Add(I);
    TArray<int32> MappedVotes;
    for (int32 Vote : Votes) MappedVotes.Add(Eligible.IndexOfByKey(Vote));
    TArray<int32> Result;
    for (int32 Leader : ERLobby::LeadingMaps(MappedVotes,Eligible.Num())) Result.Add(Eligible[Leader]);
    return Result;
}

}
