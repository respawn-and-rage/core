#include "ERShotHistory.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodyInstance.h"

void FERShotHistory::Record(USkeletalMeshComponent* Body)
{
    const double Now=Body->GetWorld()->GetTimeSeconds();
    const FVector Position=Body->GetComponentLocation();
    // Never interpolate across a skin rebuild, teleport, or world-clock reset.
    if(Asset.Get()!=Body->GetSkeletalMeshAsset() || (!Frames.IsEmpty() &&
        (Now<Frames.Last().Time || FVector::DistSquared(Position,Frames.Last().Origin)>FMath::Square(200.))))
        Frames.Reset();
    Asset=Body->GetSkeletalMeshAsset();
    if(!Frames.IsEmpty() && Frames.Last().Time==Now)Frames.Pop(EAllowShrinking::No);
    while(Frames.Num()>1 && (Now-Frames[1].Time>MaxRewind+.1 || Frames.Num()>=128))
        Frames.RemoveAt(0,1,EAllowShrinking::No);
    FERShotPose& Pose=Frames.AddDefaulted_GetRef();Pose.Time=Now;Pose.Origin=Position;
    Pose.ReferencePoint=Body->GetBoneLocation(TEXT("spine_03"));
    Pose.Bodies.Reserve(Body->Bodies.Num());
    for(const auto* Instance:Body->Bodies)
        Pose.Bodies.Add(Instance && Instance->IsValidBodyInstance() ? Instance->GetUnrealWorldTransform():FTransform::Identity);
}
bool FERShotHistory::Sample(double Time,TArray<FTransform>& Out,FVector* ReferencePoint) const
{
    if(Frames.IsEmpty() || Time<Frames[0].Time)return false;
    for(int32 I=1;I<Frames.Num();++I)
    {
        if(Frames[I].Time<Time)continue;
        const auto& A=Frames[I-1];const auto& B=Frames[I];
        if(A.Bodies.Num()!=B.Bodies.Num())return false;
        const float Alpha=float(FMath::Clamp((Time-A.Time)/FMath::Max(.000001,B.Time-A.Time),0.,1.));
        if(ReferencePoint)*ReferencePoint=FMath::Lerp(A.ReferencePoint,B.ReferencePoint,Alpha);
        Out.SetNum(A.Bodies.Num());
        for(int32 J=0;J<Out.Num();++J)Out[J].Blend(A.Bodies[J],B.Bodies[J],Alpha);
        return true;
    }
    if(ReferencePoint)*ReferencePoint=Frames.Last().ReferencePoint;
    Out=Frames.Last().Bodies;return true;
}
bool FERShotHistory::Trace(USkeletalMeshComponent* Body,FHitResult& Hit,const FVector& Start,const FVector& End,
    const FCollisionQueryParams& Query,double Time,float Margin) const
{
    TArray<FTransform> Pose;
    const bool bHistorical=Time>=0 && Time<Body->GetWorld()->GetTimeSeconds() && Sample(Time,Pose) && Pose.Num()==Body->Bodies.Num();
    const double Length=FVector::Distance(Start,End);
    if(Length<UE_SMALL_NUMBER)return false;
    // Exact intersections win over forgiving edges, so tolerance cannot steal a chest hit for the head.
    for(int32 Pass=0;Pass<(Margin>0 ? 2:1);++Pass)
    {
        bool Found=false;double Nearest=Length;
        for(int32 I=0;I<Body->Bodies.Num();++I)
        {
            const auto* Instance=Body->Bodies[I];
            if(!Instance || !Instance->IsValidBodyInstance())continue;
            const FTransform Current=Instance->GetUnrealWorldTransform();
            const FTransform Historical=bHistorical ? Pose[I]:Current;
            const auto ToCurrent=[&](const FVector& P){return Current.TransformPositionNoScale(Historical.InverseTransformPositionNoScale(P));};
            FHitResult Candidate;
            const bool FoundBody=Pass==0 ? Instance->LineTrace(Candidate,ToCurrent(Start),ToCurrent(End),Query.bTraceComplex,Query.bReturnPhysicalMaterial):
                Instance->Sweep(Candidate,ToCurrent(Start),ToCurrent(End),FQuat::Identity,FCollisionShape::MakeSphere(Margin),Query.bTraceComplex);
            if(!FoundBody)continue;
            const auto ToHistorical=[&](const FVector& P){return Historical.TransformPositionNoScale(Current.InverseTransformPositionNoScale(P));};
            const FVector Point=ToHistorical(Candidate.ImpactPoint);
            // Compare along the original ray (sweeps have a surface point off that ray).
            const double Distance=FMath::Clamp(Candidate.Time,0.f,1.f)*Length;
            if(Distance>Nearest)continue;
            Nearest=Distance;Found=true;Hit=Candidate;
            Hit.ImpactPoint=Point;Hit.Location=ToHistorical(Candidate.Location);
            Hit.ImpactNormal=Historical.TransformVectorNoScale(Current.InverseTransformVectorNoScale(Candidate.ImpactNormal));
            Hit.Normal=Historical.TransformVectorNoScale(Current.InverseTransformVectorNoScale(Candidate.Normal));
            Hit.TraceStart=Start;Hit.TraceEnd=End;Hit.Distance=Distance;
        }
        if(Found)return true;
    }
    return false;
}

void FERShotHistory::ProjectToCurrent(USkeletalMeshComponent* Body,FHitResult& Hit,double Time) const
{
    TArray<FTransform> Pose;
    if(Time<0 || Time>=Body->GetWorld()->GetTimeSeconds() || !Sample(Time,Pose) || Pose.Num()!=Body->Bodies.Num())return;
    const int32 BoneIndex=Body->GetBoneIndex(Hit.BoneName);
    for(int32 I=0;I<Body->Bodies.Num();++I)
    {
        const auto* Instance=Body->Bodies[I];
        if(!Instance || !Instance->IsValidBodyInstance() || Instance->InstanceBoneIndex!=BoneIndex)continue;
        const FTransform Current=Instance->GetUnrealWorldTransform();const auto& Historical=Pose[I];
        const auto Project=[&](const FVector& Point){return Current.TransformPositionNoScale(Historical.InverseTransformPositionNoScale(Point));};
        Hit.ImpactPoint=Project(Hit.ImpactPoint);Hit.Location=Project(Hit.Location);
        Hit.TraceStart=Project(Hit.TraceStart);Hit.TraceEnd=Project(Hit.TraceEnd);
        Hit.ImpactNormal=Current.TransformVectorNoScale(Historical.InverseTransformVectorNoScale(Hit.ImpactNormal));
        Hit.Normal=Current.TransformVectorNoScale(Historical.InverseTransformVectorNoScale(Hit.Normal));
        return;
    }
}
