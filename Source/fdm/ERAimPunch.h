#pragma once
#include "CoreMinimal.h"
#include "ERAimPunch.generated.h"

USTRUCT(BlueprintType)
struct FERAimPunchTuning
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float BasePitch = .40f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float PitchPerHP = .012f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float HeadMultiplier = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float ArmorMultiplier = .6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float YawFraction = .35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float PitchCap = 2.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float YawCap = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.01")) float HalfLife = .20f;
};

// Absolute post-hit snapshot: a late owner event ages the whole accumulated
// state, rather than replaying stale impulses or applying pellets twice.
USTRUCT()
struct FERAimPunchState
{
    GENERATED_BODY()
    UPROPERTY() FVector2D Angle = FVector2D::ZeroVector;
    UPROPERTY() double ServerTime = 0;
    UPROPERTY() float HalfLife = .20f;
    UPROPERTY() uint32 Sequence = 0;
};

namespace ERAimPunch
{
inline bool Newer(uint32 Incoming,uint32 Current) { return int32(Incoming-Current)>0; }
inline FVector2D At(const FERAimPunchState& State,double Now)
{
    const auto Angle=State.Angle*FMath::Pow(.5,FMath::Max(0.,Now-State.ServerTime)/FMath::Max(.01f,State.HalfLife));
    return Angle.SizeSquared()<1.e-10 ? FVector2D::ZeroVector : Angle;
}
inline FVector2D Impulse(const FERAimPunchTuning& T,float HP,bool Head,bool Armored,float Side,uint32 Event,bool FirstPellet)
{
    if(!FMath::IsFinite(HP) || HP<=0)return FVector2D::ZeroVector;
    const float Pitch=((FirstPellet?T.BasePitch:0.f)+HP*T.PitchPerHP)*(Head?T.HeadMultiplier:1.f)*(Armored?T.ArmorMultiplier:1.f);
    // Incoming travel is negated by the caller: Side describes the attacker's
    // position relative to effective facing. Stable server event fallback.
    const float Sign=FMath::Abs(Side)>.05f ? FMath::Sign(Side) : ((GetTypeHash(Event)&1)?1.f:-1.f);
    return FVector2D(Pitch,Pitch*T.YawFraction*Sign);
}
inline void Add(FERAimPunchState& State,const FERAimPunchTuning& T,const FVector2D& Impulse,double Now)
{
    const auto Sum=At(State,Now)+Impulse;
    State.Angle=FVector2D(FMath::Clamp(Sum.X,-T.PitchCap,T.PitchCap),FMath::Clamp(Sum.Y,-T.YawCap,T.YawCap));
    State.ServerTime=Now;State.HalfLife=T.HalfLife;
}
}

struct FERDamageVisual
{
    FVector ToAttacker = FVector::ZeroVector;
    double LocalTime = -100;
    float Strength = 0;
    bool bShowAfterDeath = false;
};
