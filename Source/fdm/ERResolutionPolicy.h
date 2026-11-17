#pragma once
#include "CoreMinimal.h"

// Timing is expressed in seconds so adaptation rates are independent of frame rate.
struct FERResolutionPolicy
{
    float Fraction=1, FilteredMs=0, PendingSeconds=0, StableSeconds=0, HoldSeconds=4;
    float SecondsSinceChange=0;
    int32 Direction=0, PendingDirection=0;
    void Reset() { *this=FERResolutionPolicy(); }
    void Update(float Delta,float GPUMs,float Min=.4f,float Max=1.f)
    {
        if(!FMath::IsFinite(Delta) || !FMath::IsFinite(GPUMs) || Delta<=0 || GPUMs<=0 || GPUMs>250)return;
        Delta=FMath::Min(Delta,.1f);
        Fraction=FMath::Clamp(Fraction,Min,Max);
        FilteredMs=FilteredMs>0 ? FMath::Lerp(FilteredMs,GPUMs,1-FMath::Exp(-Delta/ .6f)):GPUMs;
        if(HoldSeconds>0){HoldSeconds=FMath::Max(0.f,HoldSeconds-Delta);return;}
        if(!Direction)
        {
            // 59 FPS is within tolerance. Require a sustained <56 or >66 FPS.
            const int32 Want=FilteredMs>1000.f/56 && Fraction>Min+.001f ? -1:
                (FilteredMs<1000.f/66 && Fraction<Max-.001f ? 1:0);
            PendingSeconds=Want && Want==PendingDirection ? PendingSeconds+Delta:0;
            PendingDirection=Want;
            if(PendingSeconds<3)return;
            Direction=Want;PendingSeconds=0;StableSeconds=0;
        }
        const float Target=FMath::Clamp(Fraction*FMath::Sqrt((1000.f/60)/FilteredMs),Min,Max);
        // Give the temporal upscaler time at each scale while keeping the same slew rate.
        constexpr float MinChangeInterval=.130f;
        SecondsSinceChange+=Delta;
        if(SecondsSinceChange>=MinChangeInterval)
        {
            if((Direction<0 && Target<Fraction) || (Direction>0 && Target>Fraction))
            {
                const float StepSeconds=FMath::Min(SecondsSinceChange,MinChangeInterval+Delta);
                Fraction=FMath::FInterpConstantTo(Fraction,Target,StepSeconds,Direction<0 ? .08f:.05f);
                SecondsSinceChange=0;
            }
        }
        const bool AtLimit=Direction<0 ? Fraction<=Min+.001f:Fraction>=Max-.001f;
        const bool Settled=Direction<0 ? FilteredMs<=1000.f/58:FilteredMs>=1000.f/63;
        StableSeconds=Settled || AtLimit ? StableSeconds+Delta:0;
        if(StableSeconds>=2){Direction=PendingDirection=0;PendingSeconds=StableSeconds=SecondsSinceChange=0;HoldSeconds=4;}
    }
};

// Installs one engine governor, preserving the viewport/output cap and HUD.
void ERInstallResolutionGovernor();
