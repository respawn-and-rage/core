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
    TWeakObjectPtr<UObject> Asset;
    static constexpr int32 Capacity=128;
    int32 Num() const {return Count;}
    void Reset() {First=Count=0;Asset.Reset();}
    // Also used by deterministic fixtures; keeps each slot's body allocation.
    FERShotPose& Append(double Time,const FVector& Origin=FVector::ZeroVector);
    void Record(USkeletalMeshComponent* Body);
    bool Sample(double Time,TArray<FTransform>& Out,FVector* ReferencePoint=nullptr) const;
    void ProjectToCurrent(USkeletalMeshComponent* Body,FHitResult& Hit,double Time) const;
    bool Trace(USkeletalMeshComponent* Body,FHitResult& Hit,const FVector& Start,const FVector& End,
        const FCollisionQueryParams& Query,double Time,float Margin) const;
    bool TracePose(USkeletalMeshComponent* Body,FHitResult& Hit,const FVector& Start,const FVector& End,
        const FCollisionQueryParams& Query,const TArray<FTransform>* Pose,float Margin) const;
private:
    TArray<FERShotPose> Frames;
    int32 First=0,Count=0;
    const FERShotPose& Frame(int32 Index) const {return Frames[(First+Index)%Capacity];}
};
