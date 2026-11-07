#include "ERShotHistory.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodyInstance.h"

FERShotPose& FERShotHistory::Append(double Time,const FVector& Origin)
{
    if(Count>0 && (Time<Frame(Count-1).Time || FVector::DistSquared(Origin,Frame(Count-1).Origin)>FMath::Square(200.)))Reset();
    if(Count>0 && Frame(Count-1).Time==Time)--Count;
    while(Count>1 && (Time-Frame(1).Time>MaxRewind+.1 || Count>=Capacity))
    {First=(First+1)%Capacity;--Count;}
    const int32 Slot=(First+Count)%Capacity;
    if(Slot>=Frames.Num())Frames.SetNum(Slot+1);
    ++Count;
    auto& Pose=Frames[Slot];Pose.Time=Time;Pose.Origin=Origin;Pose.ReferencePoint=FVector::ZeroVector;Pose.Bodies.Reset();
    return Pose;
}
void FERShotHistory::Record(USkeletalMeshComponent* Body)
{
    // Never interpolate across a skin rebuild, teleport, or world-clock reset.
    if(Asset.Get()!=Body->GetSkeletalMeshAsset())Reset();
    auto& Pose=Append(Body->GetWorld()->GetTimeSeconds(),Body->GetComponentLocation());
    Asset=Body->GetSkeletalMeshAsset();
    Pose.ReferencePoint=Body->GetBoneLocation(TEXT("spine_03"));
    Pose.Bodies.Reserve(Body->Bodies.Num());
    for(const auto* Instance:Body->Bodies)
        Pose.Bodies.Add(Instance && Instance->IsValidBodyInstance() ? Instance->GetUnrealWorldTransform():FTransform::Identity);
}
bool FERShotHistory::Sample(double Time,TArray<FTransform>& Out,FVector* ReferencePoint) const
{
    if(Count==0 || Time<Frame(0).Time)return false;
    for(int32 I=1;I<Count;++I)
    {
        if(Frame(I).Time<Time)continue;
        const auto& A=Frame(I-1);const auto& B=Frame(I);
        if(A.Bodies.Num()!=B.Bodies.Num())return false;
        const float Alpha=float(FMath::Clamp((Time-A.Time)/FMath::Max(.000001,B.Time-A.Time),0.,1.));
        if(ReferencePoint)*ReferencePoint=FMath::Lerp(A.ReferencePoint,B.ReferencePoint,Alpha);
        Out.SetNum(A.Bodies.Num());
        for(int32 J=0;J<Out.Num();++J)Out[J].Blend(A.Bodies[J],B.Bodies[J],Alpha);
        return true;
    }
    if(ReferencePoint)*ReferencePoint=Frame(Count-1).ReferencePoint;
    Out=Frame(Count-1).Bodies;return true;
}
bool FERShotHistory::Trace(USkeletalMeshComponent* Body,FHitResult& Hit,const FVector& Start,const FVector& End,
    const FCollisionQueryParams& Query,double Time,float Margin) const
{
    TArray<FTransform> Pose;
    const bool bHistorical=Time>=0 && Time<Body->GetWorld()->GetTimeSeconds() && Sample(Time,Pose) && Pose.Num()==Body->Bodies.Num();
    return TracePose(Body,Hit,Start,End,Query,bHistorical ? &Pose:nullptr,Margin);
}
bool FERShotHistory::TracePose(USkeletalMeshComponent* Body,FHitResult& Hit,const FVector& Start,const FVector& End,
    const FCollisionQueryParams& Query,const TArray<FTransform>* Pose,float Margin) const
{
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
            const FTransform Historical=Pose && Pose->Num()==Body->Bodies.Num() ? (*Pose)[I]:Current;
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
