#pragma once
#include "CoreMinimal.h"
class USkeletalMeshComponent;
// Server-owned pose history. Queries transform rays, never live physics bodies.
struct FERShotPose
{
    double Time=0;
    FVector ReferencePoint=FVector::ZeroVector;
    FVector Origin=FVector::ZeroVector;
    TArray<FTransform> Bodies;
};
struct FERShotHistory
{
    static constexpr double MaxRewind=.2;
    static constexpr float EdgeTolerance=2.f;
    TArray<FERShotPose> Frames;
    TWeakObjectPtr<UObject> Asset;
    void Record(USkeletalMeshComponent* Body);
    bool Sample(double Time,TArray<FTransform>& Out,FVector* ReferencePoint=nullptr) const;
    void ProjectToCurrent(USkeletalMeshComponent* Body,FHitResult& Hit,double Time) const;
    bool Trace(USkeletalMeshComponent* Body,FHitResult& Hit,const FVector& Start,const FVector& End,
        const FCollisionQueryParams& Query,double Time,float Margin) const;
};
