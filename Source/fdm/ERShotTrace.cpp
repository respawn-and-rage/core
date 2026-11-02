#include "ERShotTrace.h"
#include "ERCharacter.h"
#include "ERPostureMesh.h"
#include "GameFramework/PlayerState.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

double ERShotTrace::BulletTime(const AERCharacter* Shooter)
{
    const double Now=Shooter->GetWorld()->GetTimeSeconds();
    if(!Shooter->HasAuthority() || Shooter->IsLocallyControlled())return Now;
    const auto* State=Shooter->GetPlayerState();
    const double Ping=State ? State->GetPingInMilliseconds():0;
    // Compensate the estimated inbound trip only. A fixed smoothing allowance
    // over-rewinds low-latency clients, whose simulated movement already predicts ahead.
    return Now-FMath::Clamp(Ping*.0005,0.,FERShotHistory::MaxRewind);
}
bool ERShotTrace::Pellet(UWorld* World,FHitResult& Hit,const FVector& Start,const FVector& End,const FCollisionQueryParams& Query,double ShotTime)
{
    FCollisionQueryParams WorldQuery=Query;
    TArray<AERCharacter*,TInlineAllocator<24>> Targets;
    for(TActorIterator<AERCharacter> It(World);It;++It)
    {
        if(!Query.GetIgnoredSourceObjects().Contains(It->GetUniqueID()) && !It->Health->bDead) Targets.Add(*It);
        // Movement capsules are broad locomotion volumes, not bullet anatomy.
        WorldQuery.AddIgnoredActor(*It);
    }
    bool Found=World->LineTraceSingleByChannel(Hit,Start,End,ECC_GameTraceChannel1,WorldQuery);
    FVector Limit=Found ? Hit.ImpactPoint : End;
    double Nearest=FVector::DistSquared(Start,Limit);
    for(auto* Target:Targets)
    {
        FHitResult BodyHit;
        auto* Posture=Cast<UERPostureMesh>(Target->GetMesh());
        const bool bBodyHit=Posture ? Posture->ShotHistory.Trace(Posture,BodyHit,Start,Limit,Query,ShotTime,
            ShotTime>=0 ? FERShotHistory::EdgeTolerance:0.f):Target->GetMesh()->LineTraceComponent(BodyHit,Start,Limit,Query);
        if(bBodyHit &&
            FMath::Square(BodyHit.Distance)<Nearest)
        {
            // A forgiving edge must also be visible at its actual surface point.
            FHitResult SurfaceCover;
            if(ShotTime>=0 && World->LineTraceSingleByChannel(SurfaceCover,Start,BodyHit.ImpactPoint,ECC_GameTraceChannel1,WorldQuery))continue;
            Limit=Start+(Limit-Start)*BodyHit.Time;
            Hit=BodyHit;Found=true;Nearest=FVector::DistSquared(Start,Limit);
        }
    }
    return Found;
}
