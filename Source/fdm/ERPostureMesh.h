#pragma once
#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "ERTeamAppearance.h"
#include "ERShotHistory.h"
#include "ERPostureMesh.generated.h"

struct FReferenceSkeleton;

// Immutable per-mesh data used by every pose pass; rebuilt when the skin changes.
struct FDM_API FERPostureRigCache
{
    TMap<FName,int32> BoneIndices;
    TArray<int32> ParentIndices;
    TArray<int32> TraversalOrder,SubtreeEnd;
    TArray<FTransform> ReferenceComponentPose;
    void Initialize(const FReferenceSkeleton& Ref);
    int32 FindBoneIndex(FName Name) const { const auto* Index=BoneIndices.Find(Name);return Index ? *Index:INDEX_NONE; }
    int32 GetParentIndex(int32 Bone) const { return ParentIndices[Bone]; }
    bool InSubtree(int32 Bone,int32 Root) const
    { return TraversalOrder.IsValidIndex(Root) && TraversalOrder.IsValidIndex(Bone) &&
        TraversalOrder[Bone]>=TraversalOrder[Root] && TraversalOrder[Bone]<SubtreeEnd[Root]; }
};

namespace ERPosturePose
{
    FDM_API void RotateBranch(const FERPostureRigCache& Ref,TArray<FTransform>& Pose,const TCHAR* RootName,FQuat Rotation);
    FDM_API void FitLimb(const FERPostureRigCache& Ref,TArray<FTransform>& Pose,const TCHAR* UpperName,const TCHAR* LowerName,const TCHAR* HandName,FVector Target,FQuat Rotation,FVector Pole);
}

// Procedural prone and weapon support-hand fitting over the team locomotion.
UCLASS()
class FDM_API UERPostureMesh : public USkeletalMeshComponent
{
    GENERATED_BODY()
public:
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    virtual void FinalizeBoneTransform() override;
    // Optional local dressing-room inputs; gameplay characters always use their own state.
    UPROPERTY(Transient) TObjectPtr<class UERWeaponData> PreviewWeapon;
    UPROPERTY(Transient) FERTeamAppearance PreviewAppearance;
    FERShotHistory ShotHistory;
    float GetWorldAimBlend() const {return WorldAimBlend;}
private:
    float WorldAimBlend=0;
    TWeakObjectPtr<class USkeletalMesh> CachedRigMesh;
    FERPostureRigCache RigCache;
    float CachedKneeOffset=0,CachedToeOffset=0,CachedPelvisDepth=4;
    FVector CachedContactsLeft=FVector::ZeroVector,CachedContactsRight=FVector::ZeroVector;
    void RefreshRigCache();
};
