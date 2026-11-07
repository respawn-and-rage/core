#include "ERShotTrace.h"
#include "ERCharacter.h"
#include "ERPostureMesh.h"
#include "ERMovement.h"
#include "GameFramework/PlayerState.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

double ERShotTrace::RewindSeconds(double PingMilliseconds,double ValidatedViewAge)
{
    const double Ping=FMath::IsFinite(PingMilliseconds) ? FMath::Max(0.,PingMilliseconds):0.;
    const double Age=FMath::IsFinite(ValidatedViewAge) ? FMath::Max(0.,ValidatedViewAge):0.;
    return FMath::Clamp(Ping*.0005+Age,0.,FERShotHistory::MaxRewind);
}
double ERShotTrace::BulletTime(const AERCharacter* Shooter)
{
    const double Now=Shooter->GetWorld()->GetTimeSeconds();
    if(!Shooter->HasAuthority() || Shooter->IsLocallyControlled())return Now;
    const auto* State=Shooter->GetPlayerState();
    const double Ping=State ? State->GetPingInMilliseconds():0;
    const auto* Movement=Cast<UERMovement>(Shooter->GetCharacterMovement());
    const double Age=Movement && Movement->LastAcceptedMoveServerTime>=0 ? Now-Movement->LastAcceptedMoveServerTime:0.;
    // No client wall clock or fixed interpolation bonus. Automatic shots may
    // still use the latest validated view while a slower client sends its next move.
    return Now-RewindSeconds(Ping,Age);
}
ERShotTrace::FContext::FContext(UWorld* InWorld,const FCollisionQueryParams& Query,double Time)
    : World(InWorld),ShotTime(Time),WorldQuery(Query)
{
    for(TActorIterator<AERCharacter> It(World);It;++It)
    {
        if(!Query.GetIgnoredSourceObjects().Contains(It->GetUniqueID()) && !It->Health->bDead && It->bNetworkReady)
        {
            auto& Target=Targets.AddDefaulted_GetRef();Target.Character=*It;Target.Posture=Cast<UERPostureMesh>(It->GetMesh());
            Target.Historical=Target.Posture && Time>=0 && Time<World->GetTimeSeconds() &&
                Target.Posture->ShotHistory.Sample(Time,Target.Pose) && Target.Pose.Num()==Target.Posture->Bodies.Num();
        }
        // Movement capsules are broad locomotion volumes, not bullet anatomy.
        WorldQuery.AddIgnoredActor(*It);
    }
}
bool ERShotTrace::Pellet(UWorld* World,FHitResult& Hit,const FVector& Start,const FVector& End,const FCollisionQueryParams& Query,double ShotTime,FContext* Context)
{
    if(!Context || Context->World!=World || Context->ShotTime!=ShotTime)
    {FContext Local(World,Query,ShotTime);return Pellet(World,Hit,Start,End,Query,ShotTime,&Local);}
    bool Found=World->LineTraceSingleByChannel(Hit,Start,End,ECC_GameTraceChannel1,Context->WorldQuery);
    FVector Limit=Found ? Hit.ImpactPoint : End;
    double Nearest=FVector::DistSquared(Start,Limit);
    for(const auto& Target:Context->Targets)
    {
        if(!IsValid(Target.Character) || Target.Character->Health->bDead || !Target.Character->bNetworkReady)continue;
        FHitResult BodyHit;
        auto* Posture=Target.Posture;
        const bool bBodyHit=Posture ? Posture->ShotHistory.TracePose(Posture,BodyHit,Start,Limit,Query,Target.Historical ? &Target.Pose:nullptr,
            ShotTime>=0 ? FERShotHistory::EdgeTolerance:0.f):Target.Character->GetMesh()->LineTraceComponent(BodyHit,Start,Limit,Query);
        if(bBodyHit &&
            FMath::Square(BodyHit.Distance)<Nearest)
        {
            // A forgiving edge must also be visible at its actual surface point.
            FHitResult SurfaceCover;
            if(ShotTime>=0 && World->LineTraceSingleByChannel(SurfaceCover,Start,BodyHit.ImpactPoint,ECC_GameTraceChannel1,Context->WorldQuery))continue;
            Limit=Start+(Limit-Start)*BodyHit.Time;
            Hit=BodyHit;Found=true;Nearest=FVector::DistSquared(Start,Limit);
        }
    }
    return Found;
}
