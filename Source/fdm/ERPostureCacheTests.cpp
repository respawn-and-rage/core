#include "ERPostureMesh.h"
#include "ReferenceSkeleton.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERPostureCacheTest,"EclipseRecoil.Characters.PostureCache",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERPostureCacheTest::RunTest(const FString&)
{
    FReferenceSkeleton Skeleton;
    {
        FReferenceSkeletonModifier Edit(Skeleton,nullptr);
        auto Add=[&](const TCHAR* Name,int32 Parent,FVector Position)
        {Edit.Add(FMeshBoneInfo(FName(Name),FString(Name),Parent),FTransform(Position));};
        Add(TEXT("root"),INDEX_NONE,FVector(4,3,2));
        Add(TEXT("upper"),0,FVector(0,0,50));
        Add(TEXT("lower"),1,FVector(30,0,0));
        Add(TEXT("hand"),2,FVector(25,0,0));
        Add(TEXT("finger"),3,FVector(5,1,0));
        Add(TEXT("upper_twist"),1,FVector(10,1,0));
        Add(TEXT("lower_twist"),2,FVector(10,-1,0));
        Add(TEXT("other"),0,FVector(-20,0,10));
    }
    FERPostureRigCache Cache;Cache.Initialize(Skeleton);
    TestEqual(TEXT("name lookup"),Cache.FindBoneIndex(TEXT("hand")),3);
    TestEqual(TEXT("missing name"),Cache.FindBoneIndex(TEXT("missing")),int32(INDEX_NONE));
    for(int32 Bone=0;Bone<Skeleton.GetNum();++Bone)
        for(int32 Root=0;Root<Skeleton.GetNum();++Root)
        {
            bool Descendant=false;
            for(int32 Ancestor=Bone;Ancestor!=INDEX_NONE;Ancestor=Skeleton.GetParentIndex(Ancestor))
                if(Ancestor==Root){Descendant=true;break;}
            TestEqual(TEXT("cached ancestry matches the original skeleton"),Cache.InSubtree(Bone,Root),Descendant);
        }
    TestFalse(TEXT("missing branch is empty"),Cache.InSubtree(2,INDEX_NONE));
    TestTrue(TEXT("reference pose is composed from local parent transforms"),Cache.ReferenceComponentPose[4].GetLocation().Equals(FVector(64,4,52),.00001));
    FRandomStream Random(1729);
    for(int32 Trial=0;Trial<64;++Trial)
    {
        auto Pose=Cache.ReferenceComponentPose;
        const auto Initial=Pose;
        const FQuat Turn(Random.VRand(),Random.FRandRange(-1.2f,1.2f));
        ERPosturePose::RotateBranch(Cache,Pose,TEXT("upper"),Turn);
        for(int32 Bone=0;Bone<Pose.Num();++Bone)
        {
            FTransform Expected=Initial[Bone];
            if(Cache.InSubtree(Bone,1))
            {
                Expected.SetLocation(Initial[1].GetLocation()+Turn.RotateVector(Initial[Bone].GetLocation()-Initial[1].GetLocation()));
                Expected.SetRotation(Turn*Initial[Bone].GetRotation());
            }
            TestTrue(TEXT("branch rotation preserves its pivot and all unrelated bones"),Pose[Bone].Equals(Expected,.00001));
        }
        const auto Before=Pose;
        const FVector Target=Pose[1].GetLocation()+Random.VRand()*Random.FRandRange(10.f,50.f);
        const FQuat Palm(Random.VRand(),Random.FRandRange(-2.f,2.f));
        ERPosturePose::FitLimb(Cache,Pose,TEXT("upper"),TEXT("lower"),TEXT("hand"),Target,Palm,FVector::UpVector);
        TestTrue(TEXT("IK keeps the shoulder fixed"),Pose[1].GetLocation().Equals(Before[1].GetLocation(),.00001));
        TestTrue(TEXT("IK keeps upper segment length"),FMath::IsNearlyEqual(FVector::Distance(Pose[1].GetLocation(),Pose[2].GetLocation()),30.,.001));
        TestTrue(TEXT("IK keeps lower segment length"),FMath::IsNearlyEqual(FVector::Distance(Pose[2].GetLocation(),Pose[3].GetLocation()),25.,.001));
        TestTrue(TEXT("reachable palm reaches its target"),Pose[3].GetLocation().Equals(Target,.001));
        TestTrue(TEXT("palm receives the requested orientation"),Pose[3].GetRotation().Equals(Palm,.00001));
        for(const FIntPoint Pair:{FIntPoint(4,3),FIntPoint(5,1),FIntPoint(6,2)})
            TestTrue(TEXT("fingers and twist bones retain their anchor-relative transform"),
                Pose[Pair.X].GetRelativeTransform(Pose[Pair.Y]).Equals(Before[Pair.X].GetRelativeTransform(Before[Pair.Y]),.0001));
        TestTrue(TEXT("IK leaves another branch untouched"),Pose[7].Equals(Before[7],.00001));
        const auto BeforeMissing=Pose;
        ERPosturePose::RotateBranch(Cache,Pose,TEXT("missing"),Turn);
        ERPosturePose::FitLimb(Cache,Pose,TEXT("missing"),TEXT("lower"),TEXT("hand"),Target,Palm,FVector::UpVector);
        for(int32 Bone=0;Bone<Pose.Num();++Bone)TestTrue(TEXT("missing joints preserve the pose"),Pose[Bone].Equals(BeforeMissing[Bone],.00001));
    }
    FReferenceSkeleton Replacement;
    {
        FReferenceSkeletonModifier Edit(Replacement,nullptr);
        Edit.Add(FMeshBoneInfo(FName(TEXT("replacement")),TEXT("replacement"),INDEX_NONE),FTransform::Identity);
    }
    Cache.Initialize(Replacement);
    TestEqual(TEXT("rebuild removes old bone indices"),Cache.FindBoneIndex(TEXT("hand")),int32(INDEX_NONE));
    TestEqual(TEXT("rebuild replaces branch intervals"),Cache.SubtreeEnd.Num(),1);
    TestEqual(TEXT("rebuild replaces reference transforms"),Cache.ReferenceComponentPose.Num(),1);
    return !HasAnyErrors();
}
#endif
