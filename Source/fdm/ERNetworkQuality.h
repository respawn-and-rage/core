#pragma once
#include "CoreMinimal.h"

namespace ERNetworkProtocol
{
// Match mode voting, skull actors and replicated skull counts.
constexpr uint32 Version=2;
inline FString BuildSignature(const FString& Build){return FString::Printf(TEXT("%s-net%u"),*Build,Version);}
}

// Local diagnostics only. Never used to authorize a shot or extend rewind.
struct FERNetworkQuality
{
    double RoundTripMs=0,JitterMs=0,PreviousMs=0;
    int32 Samples=0;
    void Observe(double Seconds)
    {
        if(!FMath::IsFinite(Seconds) || Seconds<0 || Seconds>4)return;
        const double SampleMs=Seconds*1000;
        if(Samples)
        {
            RoundTripMs=FMath::Lerp(RoundTripMs,SampleMs,.2);
            JitterMs=FMath::Lerp(JitterMs,FMath::Abs(SampleMs-PreviousMs),.2);
        }
        else RoundTripMs=SampleMs;
        PreviousMs=SampleMs;++Samples;
    }
};

struct FERMapSyncDeadline
{
    static constexpr double StallSeconds=60,MaximumSeconds=300;
    double StartedAt=0,LastProgressAt=0;
    uint64 Progress=0;
    void Reset(double Now){StartedAt=LastProgressAt=Now;Progress=0;}
    void Observe(uint64 Serial,double Now)
    {
        if(Serial>Progress){Progress=Serial;LastProgressAt=Now;}
    }
    bool Expired(double Now) const
    {return Now-LastProgressAt>=StallSeconds || Now-StartedAt>=MaximumSeconds;}
};
