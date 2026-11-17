#include "ERPlayerController.h"
#include "ERMatchStats.h"
#include "HAL/PlatformTime.h"

namespace
{
constexpr int32 ChunkSize = 4096;
constexpr int32 MaxGraphSize = 192 * 1024;

bool ValidDay(const FString& Day)
{
    if (Day.IsEmpty()) return true;
    if (Day.Len() != 10) return false;
    for (int32 Index = 0; Index < Day.Len(); ++Index)
    {
        if (Index == 4 || Index == 7)
        {
            if (Day[Index] != '-') return false;
        }
        else if (Day[Index] < '0' || Day[Index] > '9') return false;
    }
    return true;
}
}

void AERPlayerController::RequestMatchGraph(const FString& Day, const FString& Match, TFunction<void(const FString&)> Complete)
{
    MatchGraphComplete = MoveTemp(Complete);
    MatchGraphJson.Reset();
    ServerRequestMatchGraph(++MatchGraphRequest, Day, Match);
}

void AERPlayerController::ServerRequestMatchGraph_Implementation(uint32 Request, const FString& Day, const FString& Match)
{
    FGuid MatchId;
    if (!ValidDay(Day) || (!Match.IsEmpty() && !FGuid::ParseExact(Match, EGuidFormats::DigitsWithHyphens, MatchId)))
    {
        ClientMatchGraphChunk(Request, TEXT("{\"error\":\"Invalid match selection.\"}"), true);
        return;
    }
    const double Now = FPlatformTime::Seconds();
    if (bMatchGraphBusy || Now < NextMatchGraphRead)
    {
        ClientMatchGraphChunk(Request, TEXT("{\"error\":\"The archive is busy. Try again in a moment.\"}"), true);
        return;
    }
    bMatchGraphBusy = true;
    NextMatchGraphRead = Now + .25;
    FERMatchStats::ReadGraph(Day, Match.IsEmpty() ? FString() : MatchId.ToString(EGuidFormats::DigitsWithHyphensLower),
        [Weak = TWeakObjectPtr<AERPlayerController>(this), Request](FString Json)
        {
            auto* PC = Weak.Get();
            if (!PC) return;
            PC->bMatchGraphBusy = false;
            if (Json.IsEmpty() || Json.Len() > MaxGraphSize)
                Json = TEXT("{\"error\":\"This match graph is unavailable or too large to display.\"}");
            for (int32 Offset = 0; Offset < Json.Len(); Offset += ChunkSize)
                PC->ClientMatchGraphChunk(Request, Json.Mid(Offset, ChunkSize), Offset + ChunkSize >= Json.Len());
        });
}

void AERPlayerController::ClientMatchGraphChunk_Implementation(uint32 Request, const FString& Chunk, bool bFinal)
{
    if (Request != MatchGraphRequest || !MatchGraphComplete) return;
    if (Chunk.Len() > ChunkSize || MatchGraphJson.Len() + Chunk.Len() > MaxGraphSize)
    {
        MatchGraphJson = TEXT("{\"error\":\"The match graph response was too large.\"}");
        bFinal = true;
    }
    else MatchGraphJson += Chunk;
    if (bFinal)
    {
        TFunction<void(const FString&)> Complete = MoveTemp(MatchGraphComplete);
        FString Json = MoveTemp(MatchGraphJson);
        Complete(Json);
    }
}
