#include "ERPostureMesh.h"
#include "ERCharacter.h"
#include "ERMovement.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "ERWeapon.h"
#include "ERWorldWeaponPose.h"
#include "Math/RotationMatrix.h"
#include "ERMatch.h"
#include "Camera/CameraComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void FERPostureRigCache::Initialize(const FReferenceSkeleton& Ref)
{
    const int32 Count=Ref.GetNum();
    BoneIndices.Reset();ParentIndices.SetNum(Count);TraversalOrder.SetNum(Count);SubtreeEnd.SetNum(Count);ReferenceComponentPose.SetNum(Count);
    TArray<TArray<int32>> ChildBones;ChildBones.SetNum(Count);
    for(int32 Bone=0;Bone<Count;++Bone)
    {
        BoneIndices.Add(Ref.GetBoneName(Bone),Bone);ParentIndices[Bone]=Ref.GetParentIndex(Bone);
        const int32 Parent=ParentIndices[Bone];
        if(Parent!=INDEX_NONE)ChildBones[Parent].Add(Bone);
        ReferenceComponentPose[Bone]=Parent==INDEX_NONE ? Ref.GetRefBonePose()[Bone]:Ref.GetRefBonePose()[Bone]*ReferenceComponentPose[Parent];
    }
    // DFS intervals make ancestry queries constant time without an N-squared
    // table or assuming that sibling branches occupy consecutive bone indices.
    int32 NextOrder=0;
    auto Visit=[&](auto&& Self,int32 Bone)->void
    {
        TraversalOrder[Bone]=NextOrder++;
        for(int32 Child:ChildBones[Bone])Self(Self,Child);
        SubtreeEnd[Bone]=NextOrder;
    };
    for(int32 Bone=0;Bone<Count;++Bone)if(ParentIndices[Bone]==INDEX_NONE)Visit(Visit,Bone);
}

void UERPostureMesh::RefreshRigCache()
{
    auto* RigMesh=GetSkeletalMeshAsset();
    if(!RigMesh || (CachedRigMesh.Get()==RigMesh && RigCache.ParentIndices.Num()==RigMesh->GetRefSkeleton().GetNum()))return;
    CachedRigMesh=RigMesh;RigCache.Initialize(RigMesh->GetRefSkeleton());
    CachedKneeOffset=CachedToeOffset=0;CachedPelvisDepth=4;CachedContactsLeft=CachedContactsRight=FVector::ZeroVector;
    const FString MeshPath=RigMesh->GetPathName();
    GConfig->GetFloat(TEXT("RespawnAndRage.KneelingKnee"),*MeshPath,CachedKneeOffset,GGameIni);
    GConfig->GetFloat(TEXT("RespawnAndRage.KneelingToe"),*MeshPath,CachedToeOffset,GGameIni);
    GConfig->GetVector(TEXT("RespawnAndRage.ProneContactsLeft"),*MeshPath,CachedContactsLeft,GGameIni);
    GConfig->GetVector(TEXT("RespawnAndRage.ProneContactsRight"),*MeshPath,CachedContactsRight,GGameIni);
    GConfig->GetFloat(TEXT("RespawnAndRage.PronePelvisDepth"),*MeshPath,CachedPelvisDepth,GGameIni);
}

void ERPosturePose::RotateBranch(const FERPostureRigCache& Ref,TArray<FTransform>& Pose,const TCHAR* RootName,FQuat Rotation)
{
    const int32 Root=Ref.FindBoneIndex(RootName);if(!Pose.IsValidIndex(Root))return;
    const FVector Pivot=Pose[Root].GetLocation();
    for(int32 I=0;I<Pose.Num();++I)if(Ref.InSubtree(I,Root))
    {
        Pose[I].SetLocation(Pivot+Rotation.RotateVector(Pose[I].GetLocation()-Pivot));
        Pose[I].SetRotation(Rotation*Pose[I].GetRotation());
    }
}
void ERPosturePose::FitLimb(const FERPostureRigCache& Ref,TArray<FTransform>& Pose,const TCHAR* UpperName,const TCHAR* LowerName,const TCHAR* HandName,FVector Target,FQuat Rotation,FVector Pole)
{
    const int32 Upper=Ref.FindBoneIndex(UpperName),Lower=Ref.FindBoneIndex(LowerName),Hand=Ref.FindBoneIndex(HandName);
    if(!Pose.IsValidIndex(Upper) || !Pose.IsValidIndex(Lower) || !Pose.IsValidIndex(Hand))return;
    const FTransform OldUpper=Pose[Upper],OldLower=Pose[Lower],OldHand=Pose[Hand];
    const FVector S=Pose[Upper].GetLocation(),E=Pose[Lower].GetLocation(),W=Pose[Hand].GetLocation();
    const float A=FVector::Dist(S,E),B=FVector::Dist(E,W),D=FMath::Clamp(float(FVector::Dist(S,Target)),FMath::Abs(A-B)+.1f,A+B-.1f);
    const FVector Direction=(Target-S).GetSafeNormal();FVector Bend=FVector::VectorPlaneProject(Pole,Direction).GetSafeNormal();
    if(Bend.IsNearlyZero())Bend=FVector::VectorPlaneProject(E-S,Direction).GetSafeNormal();
    const float Along=(A*A-B*B+D*D)/(2*D);const FVector NewE=S+Direction*Along+Bend*FMath::Sqrt(FMath::Max(0.f,A*A-Along*Along)),NewW=S+Direction*D;
    FTransform U=OldUpper,L=OldLower,H=OldHand;
    U.SetRotation(FQuat::FindBetweenNormals((E-S).GetSafeNormal(),(NewE-S).GetSafeNormal())*U.GetRotation());
    L.SetLocation(NewE);L.SetRotation(FQuat::FindBetweenNormals((W-E).GetSafeNormal(),(NewW-NewE).GetSafeNormal())*L.GetRotation());H.SetLocation(NewW);H.SetRotation(Rotation);
    for(int32 I=0;I<Pose.Num();++I)
    {
        const FTransform* Before=Ref.InSubtree(I,Hand) ? &OldHand:Ref.InSubtree(I,Lower) ? &OldLower:Ref.InSubtree(I,Upper) ? &OldUpper:nullptr;
        const FTransform* After=Before==&OldHand ? &H:Before==&OldLower ? &L:&U;
        if(Before)Pose[I]=Pose[I].GetRelativeTransform(*Before)**After;
    }
}
namespace
{
using ERPosturePose::RotateBranch;
using ERPosturePose::FitLimb;
FVector GroundedArmPole(const FVector& Shoulder,const FVector& Elbow,const FVector& Wrist,const FVector& Target,const FVector& Up,const FVector& Side,const FVector& Ground,const AActor* Character,const FTransform& Frame,double FloorZ)
{
    const double A=FVector::Distance(Shoulder,Elbow),B=FVector::Distance(Elbow,Wrist);
    const double D=FMath::Clamp(FVector::Distance(Shoulder,Target),FMath::Abs(A-B)+.1,A+B-.1);
    const FVector Direction=(Target-Shoulder).GetSafeNormal();
    const FVector PlaneUp=FVector::VectorPlaneProject(Up,Direction).GetSafeNormal();
    FVector PlaneSide=FVector::CrossProduct(Direction,PlaneUp).GetSafeNormal();
    if(FVector::DotProduct(PlaneSide,Side)<0)PlaneSide=-PlaneSide;
    const double Along=(A*A-B*B+D*D)/(2*D),Radius=FMath::Sqrt(FMath::Max(0.0,A*A-Along*Along));
    const double Vertical=Radius*FVector::DotProduct(PlaneUp,Up);
    if(Vertical<.01)return Side-Up*.15f;
    const double Height=FMath::Clamp(FVector::DotProduct(Ground-Shoulder-Direction*Along,Up)/Vertical,-1.0,1.0);
    const FVector GroundPole=PlaneUp*Height+PlaneSide*FMath::Sqrt(FMath::Max(0.0,1-Height*Height));
    if(Character)
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ERProneArmRoom),false,Character);
        auto Clear=[&](const FVector& Pole)
        {
            const FVector Joint=Shoulder+Direction*Along+Pole*Radius;
            FHitResult Contact;
            FVector Start=Frame.TransformPosition(Shoulder), End=Frame.TransformPosition(Joint);
            Start.Z=FMath::Max(Start.Z,FloorZ+7.0);
            End.Z=FMath::Max(End.Z,FloorZ+7.0);
            return !Character->GetWorld()->SweepSingleByChannel(Contact,Start,End,
                FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(FVector(16,16,6)),Query);
        };
        if(Clear(GroundPole))return GroundPole;
        // In tight cover the elbow may lift off its support point, but never
        // pass through the wall. The wrist and both segment lengths stay fixed.
        for(float Raise=.5f;Raise<=4.f;Raise+=.5f)
        {
            const FVector Candidate=(GroundPole+PlaneUp*Raise).GetSafeNormal();
            if(Clear(Candidate))return Candidate;
        }
        return PlaneUp;
    }
    return GroundPole;
}
}
void UERPostureMesh::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    const auto* C=Cast<AERCharacter>(GetOwner());const auto* D=C && C->Weapon ? C->Weapon->Data.Get():nullptr;
    const bool Aiming=D && D->bWorldReferencePose && C->Weapon->ScopeLevel>0 && !C->Health->bDead && !C->IsWeaponStowed();
    WorldAimBlend=FMath::FInterpConstantTo(WorldAimBlend,Aiming ? 1.f:0.f,FMath::Min(DeltaTime,.05f),7.f);
    if(D && D->bWorldReferencePose)
    {
        bEnableUpdateRateOptimizations=false;
        VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    }
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
}

void UERPostureMesh::FinalizeBoneTransform()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ERPostureFinalize);
    const auto* C = Cast<AERCharacter>(GetOwner());
    const auto* M = C ? Cast<UERMovement>(C->GetCharacterMovement()) : nullptr;
    const auto* Mesh = GetSkeletalMeshAsset();
    RefreshRigCache();
    const float Blend = M ? M->Locomotion.ProneBlend : 0.f;
    if (Mesh && M && Blend < 1.f && M->Locomotion.HalfHeight < 92.5f)
    {
        const auto& Ref = RigCache;
        auto& Pose = GetEditableComponentSpaceTransforms();
        const float CrouchDepth = 92.5f - FMath::Max(M->Locomotion.HalfHeight, M->GetCrouchedHalfHeight());
        const float Kneel = FMath::Clamp(CrouchDepth / 30.f, 0.f, 1.f) * (1.f - Blend);
        const FTransform& Frame = GetComponentTransform();
        const FVector Up = Frame.InverseTransformVector(FVector::UpVector);
        const FVector Forward = Frame.InverseTransformVector(C->GetActorForwardVector());
        const int32 LeftFoot = Ref.FindBoneIndex(TEXT("foot_l")), RightFoot = Ref.FindBoneIndex(TEXT("foot_r"));
        const int32 RightHip = Ref.FindBoneIndex(TEXT("thigh_r")), RightKnee = Ref.FindBoneIndex(TEXT("calf_r"));
        const int32 RightBall = Ref.FindBoneIndex(TEXT("ball_r"));
        if (Pose.IsValidIndex(LeftFoot) && Pose.IsValidIndex(RightFoot) &&
            Pose.IsValidIndex(RightHip) && Pose.IsValidIndex(RightKnee))
        {
            const FTransform LeftPlant = Pose[LeftFoot], RightPlant = Pose[RightFoot];
            const FVector Hip = Pose[RightHip].GetLocation();
            const double ThighLength = FVector::Distance(Hip, Pose[RightKnee].GetLocation());
            const double ShinLength = FVector::Distance(Pose[RightKnee].GetLocation(), RightPlant.GetLocation());
            const FVector Toe = Pose.IsValidIndex(RightBall) ? Pose[RightBall].GetLocation() :
                RightPlant.GetLocation() + Forward * 13.f - Up * 7.f;
            const FQuat ToePlant(Frame.InverseTransformVectorNoScale(C->GetActorRightVector()), FMath::DegreesToRadians(65.f));
            const FVector AnkleFromToe = ToePlant.RotateVector(RightPlant.GetLocation() - Toe);

            const float KneeOffset=CachedKneeOffset,ToeOffset=CachedToeOffset;

            // Place the knee pad just above the contact plane. Construct the
            // kneeling triangle from the rig's own thigh and shin lengths.
            const FVector Knee = Hip - Up * (FVector::DotProduct(Hip-Toe, Up)/Up.SizeSquared() - 6.f - KneeOffset) - Forward * 12.f;
            const FVector KneelingHip = Knee + Up.GetSafeNormal() *
                FMath::Sqrt(FMath::Max(0.0, FMath::Square(ThighLength)-FMath::Square(12.f*Forward.Size()))) + Forward * 12.f;
            const double AnkleRise = FVector::DotProduct(Toe + AnkleFromToe + Up*ToeOffset - Knee, Up.GetSafeNormal());
            const double ShinRun = FMath::Sqrt(FMath::Max(0.0, FMath::Square(ShinLength)-FMath::Square(AnkleRise)));
            const FVector RearAnkle = Knee - Forward.GetSafeNormal()*ShinRun + Up.GetSafeNormal()*AnkleRise;

            for (FTransform& Bone : Pose)
                Bone.AddToTranslation((KneelingHip-Hip)*Kneel);
            FitLimb(Ref, Pose, TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"),
                LeftPlant.GetLocation()+Forward*(20.f*Kneel), LeftPlant.GetRotation(), Forward);
            FitLimb(Ref, Pose, TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"),
                FMath::Lerp(RightPlant.GetLocation(), RearAnkle, Kneel),
                FQuat::Slerp(RightPlant.GetRotation(), ToePlant*RightPlant.GetRotation(), Kneel),
                FMath::Lerp(Forward.GetSafeNormal(), (Knee-KneelingHip).GetSafeNormal(), Kneel));
        }
    }
    FVector ProneGround=FVector::ZeroVector;
    const FVector ContactsLeft=CachedContactsLeft,ContactsRight=CachedContactsRight;
    double ProneFloorZ=0;
    const float PelvisDepth=CachedPelvisDepth;
    bool GroundedProne=false;
    auto ElbowGround=[&](bool LeftHand)
    {
        return ProneGround+GetComponentTransform().InverseTransformVector(
            FVector::UpVector*(LeftHand ? ContactsLeft.Z:ContactsRight.Z));
    };
    if(C && Blend>0)
    {
        FHitResult Ground;FCollisionQueryParams Query(SCENE_QUERY_STAT(ERProneElbows),false,C);
        GroundedProne=GetWorld()->LineTraceSingleByChannel(Ground,C->GetActorLocation(),C->GetActorLocation()-FVector::UpVector*150,ECC_Pawn,Query);
        if(GroundedProne)
        {
            ProneFloorZ=Ground.ImpactPoint.Z;
            ProneGround=GetComponentTransform().InverseTransformPosition(Ground.ImpactPoint+FVector::UpVector*12.f);
        }
    }
    if (Mesh && Blend > 0)
    {
        const auto& Ref = RigCache;
        auto& Pose = GetEditableComponentSpaceTransforms();
        const int32 Left = Ref.FindBoneIndex(TEXT("upperarm_l")), Right = Ref.FindBoneIndex(TEXT("upperarm_r"));
        const int32 Neck = Ref.FindBoneIndex(TEXT("neck_01"));
        const int32 Pelvis = Ref.FindBoneIndex(TEXT("pelvis"));
        const FVector NeckPivot=Pose.IsValidIndex(Neck) ? Pose[Neck].GetLocation():FVector::ZeroVector;
        const int32 ThighLeft = Ref.FindBoneIndex(TEXT("thigh_l")), ThighRight = Ref.FindBoneIndex(TEXT("thigh_r"));
        auto InSubtree = [&Ref](int32 Bone, int32 Root)
        {
            return Ref.InSubtree(Bone,Root);
        };
        const FVector Shoulder = Pose.IsValidIndex(Left) && Pose.IsValidIndex(Right) ?
            (Pose[Left].GetLocation() + Pose[Right].GetLocation()) * .5f : FVector::ZeroVector;
        // A common shoulder pivot keeps both rifle grips together while the body lies flat.
        const FQuat Arms(FVector::XAxisVector, Blend * PI * .5f);
        const FQuat Head(FVector::XAxisVector, Blend * FMath::DegreesToRadians(70.f));
        const auto& StraightLegs=Ref.ReferenceComponentPose;
        const FVector PelvisShift = Pose.IsValidIndex(Pelvis) ? Pose[Pelvis].GetLocation() - StraightLegs[Pelvis].GetLocation() : FVector::ZeroVector;
        for (int32 Bone = 0; Bone < Pose.Num(); ++Bone)
        {
            const FTransform Original=Pose[Bone];
            if (InSubtree(Bone,ThighLeft) || InSubtree(Bone,ThighRight))
            {
                const int32 Parent = Ref.GetParentIndex(Bone);
                if (Pose.IsValidIndex(Parent) && Pelvis != INDEX_NONE)
                {
                    FTransform Straight=StraightLegs[Bone];Straight.AddToTranslation(PelvisShift);
                    Pose[Bone].Blend(Original,Straight,Blend);
                }
            }
            else if (InSubtree(Bone,Left) || InSubtree(Bone,Right))
            {
                Pose[Bone].SetLocation(Shoulder + Arms.RotateVector(Original.GetLocation() - Shoulder));
                Pose[Bone].SetRotation(Arms * Original.GetRotation());
            }
            else if (InSubtree(Bone,Neck) && Pose.IsValidIndex(Neck))
            {
                const FVector Pivot = NeckPivot;
                Pose[Bone].SetLocation(Pivot + Head.RotateVector(Original.GetLocation() - Pivot));
                Pose[Bone].SetRotation(Head * Original.GetRotation());
            }
        }
        // The standing reference feet point forward. Counterturn the ankles
        // with the body, otherwise the toes point vertically into the floor.
        RotateBranch(Ref,Pose,TEXT("foot_l"),Arms);
        RotateBranch(Ref,Pose,TEXT("foot_r"),Arms);
        // Support the upper torso on the arms instead of laying the standing
        // chest depth through the floor. Keep the head and grips counterturned.
        const FQuat ChestLift(FVector::XAxisVector,Blend*FMath::DegreesToRadians(18.f));
        RotateBranch(Ref,Pose,TEXT("spine_01"),ChestLift);
        RotateBranch(Ref,Pose,TEXT("neck_01"),ChestLift.Inverse());
        RotateBranch(Ref,Pose,TEXT("clavicle_l"),ChestLift.Inverse());
        RotateBranch(Ref,Pose,TEXT("clavicle_r"),ChestLift.Inverse());
        const float ProneUnit=1.f/FMath::Max(.1f,float(GetComponentScale().X));
        const FVector ProneUp=GetComponentTransform().InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
        const FVector ProneForward=GetComponentTransform().InverseTransformVectorNoScale(C ? C->GetActorForwardVector():GetOwner()->GetActorForwardVector()).GetSafeNormal();
        const FVector ProneSide=GetComponentTransform().InverseTransformVectorNoScale(C ? C->GetActorRightVector():GetOwner()->GetActorRightVector()).GetSafeNormal();
        const FVector PelvisDrop=-ProneUp*(PelvisDepth*Blend*ProneUnit);
        for(int32 Bone=0;Bone<Pose.Num();++Bone)
            if(Bone==Pelvis || InSubtree(Bone,ThighLeft) || InSubtree(Bone,ThighRight))
                Pose[Bone].AddToTranslation(PelvisDrop);
        for(bool LeftLeg : {true,false})
        {
            const TCHAR* FootName=LeftLeg ? TEXT("foot_l"):TEXT("foot_r");
            const int32 Foot=Ref.FindBoneIndex(FootName);
            const int32 Calf=Ref.FindBoneIndex(LeftLeg ? TEXT("calf_l"):TEXT("calf_r"));
            if(!Pose.IsValidIndex(Foot) || !Pose.IsValidIndex(Calf))continue;
            const FVector Side=LeftLeg ? -ProneSide:ProneSide;
            const TCHAR* ThighName=LeftLeg ? TEXT("thigh_l"):TEXT("thigh_r");
            const TCHAR* CalfName=LeftLeg ? TEXT("calf_l"):TEXT("calf_r");
            const int32 Thigh=Ref.FindBoneIndex(ThighName);
            if(!Pose.IsValidIndex(Thigh))continue;
            float Spread=1.f;
            if(C && GroundedProne)
            {
                const FVector WorldSide=C->GetActorRightVector()*(LeftLeg ? -1.f:1.f);
                const FVector FloorCentre=GetComponentTransform().TransformPosition(ProneGround)+FVector::UpVector*3.f;
                float Room=70.f;
                FCollisionQueryParams Query(SCENE_QUERY_STAT(ERProneLegRoom),false,C);
                for(float Back : {-10.f,-45.f,-80.f})
                {
                    const FVector Start=FloorCentre+C->GetActorForwardVector()*Back;
                    FHitResult Contact;
                    if(GetWorld()->SweepSingleByChannel(Contact,Start,Start+WorldSide*75.f,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(10.f),Query))
                        Room=FMath::Min(Room,float(Contact.Distance));
                }
                Spread=FMath::Clamp((Room-25.f)/45.f,0.f,1.f);
            }
            const FVector Hip=Pose[Thigh].GetLocation();
            const FVector RestKnee=Pose[Calf].GetLocation();
            const FVector RestAnkle=Pose[Foot].GetLocation();
            const double ThighLength=FVector::Distance(Hip,RestKnee);
            const double ShinLength=FVector::Distance(RestKnee,RestAnkle);
            const int32 Ball=Ref.FindBoneIndex(LeftLeg ? TEXT("ball_l"):TEXT("ball_r"));
            const FVector Toe=Pose.IsValidIndex(Ball) ? Pose[Ball].GetLocation()-RestAnkle : ProneForward*(13.f*ProneUnit);
            const FVector ThighHeading=(-ProneForward+Side*(.43f*Spread)).GetSafeNormal();
            const FVector ShinHeading=(-ProneForward-Side*(.18f*Spread)).GetSafeNormal();
            const FVector Ground=GroundedProne ? ProneGround-ProneUp*(12.f*ProneUnit) : Hip-ProneUp*(24.f*ProneUnit);
            const FVector ContactOffsets=LeftLeg ? ContactsLeft:ContactsRight;
            const double KneeHeight=FMath::Clamp(FVector::DotProduct(Ground-Hip,ProneUp)+(12.f+ContactOffsets.Y)*ProneUnit,-.9*ThighLength,.9*ThighLength);
            const FVector Knee=Hip+ProneUp*KneeHeight+ThighHeading*FMath::Sqrt(FMath::Square(ThighLength)-FMath::Square(KneeHeight));
            const double AnkleHeight=FMath::Clamp(FVector::DotProduct(Ground-Knee,ProneUp)+Toe.Size()+(10.f+ContactOffsets.X)*ProneUnit,-.8*ShinLength,.8*ShinLength);
            const FVector Ankle=Knee+ProneUp*AnkleHeight+ShinHeading*FMath::Sqrt(FMath::Square(ShinLength)-FMath::Square(AnkleHeight));
            const FVector Shin=(Ankle-Knee).GetSafeNormal();
            const FVector DownToe=FVector::VectorPlaneProject(-ProneUp,Shin).GetSafeNormal();
            const FQuat BootFrame=FRotationMatrix::MakeFromXZ(Toe.GetSafeNormal(),ProneUp).ToQuat();
            const FQuat ContactFrame=FRotationMatrix::MakeFromXZ(DownToe,-Shin).ToQuat();
            const FQuat RestRotation=Pose[Foot].GetRotation();
            FitLimb(Ref,Pose,ThighName,CalfName,FootName,FMath::Lerp(RestAnkle,Ankle,Blend),
                FQuat::Slerp(RestRotation,ContactFrame*BootFrame.Inverse()*RestRotation,Blend),
                FMath::Lerp(Side-ProneUp*.1f,(Knee-Hip).GetSafeNormal(),Blend));
        }
        const auto* HeldData=C && C->Weapon ? C->Weapon->Data.Get():nullptr;
        if(HeldData && !HeldData->bWorldReferencePose)
        {
            const float Unit=1.f/FMath::Max(.1f,float(GetComponentScale().X));
            const FVector Up=GetComponentTransform().InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
            const FVector Aim=GetComponentTransform().InverseTransformVectorNoScale(FRotator(0,C->GetBaseAimRotation().Yaw,0).Vector()).GetSafeNormal();
            const FVector Side=GetComponentTransform().InverseTransformVectorNoScale(C->GetActorRightVector()).GetSafeNormal();
            for(bool LeftHand : {false,true})
            {
                const int32 Upper=Ref.FindBoneIndex(LeftHand ? TEXT("upperarm_l"):TEXT("upperarm_r"));
                const int32 Hand=Ref.FindBoneIndex(LeftHand ? TEXT("hand_l"):TEXT("hand_r"));
                const int32 Lower=Ref.FindBoneIndex(LeftHand ? TEXT("lowerarm_l"):TEXT("lowerarm_r"));
                if(!Pose.IsValidIndex(Upper) || !Pose.IsValidIndex(Hand) || !Pose.IsValidIndex(Lower))continue;
                const FVector ShoulderPosition=Pose[Upper].GetLocation();
                FVector Target=ShoulderPosition+Aim*(35.f*Unit);
                FHitResult Cover;
                const FTransform& Frame=GetComponentTransform();
                FCollisionQueryParams Query(SCENE_QUERY_STAT(ERProneHandRoom),false,C);
                if(GetWorld()->SweepSingleByChannel(Cover,Frame.TransformPosition(ShoulderPosition),Frame.TransformPosition(Target),
                    FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(FVector(16,16,6)),Query))
                    Target=FMath::Lerp(ShoulderPosition,Target,FMath::Max(0.f,Cover.Time-.05f));
                const FVector Bend=GroundedProne ? GroundedArmPole(Pose[Upper].GetLocation(),Pose[Lower].GetLocation(),Pose[Hand].GetLocation(),Target,Up,LeftHand ? -Side:Side,ElbowGround(LeftHand),C,GetComponentTransform(),ProneFloorZ):(LeftHand ? -Side:Side)*.65f-Up*.15f;
                FitLimb(Ref,Pose,LeftHand ? TEXT("upperarm_l"):TEXT("upperarm_r"),LeftHand ? TEXT("lowerarm_l"):TEXT("lowerarm_r"),LeftHand ? TEXT("hand_l"):TEXT("hand_r"),
                    FMath::Lerp(Pose[Hand].GetLocation(),Target,Blend),Pose[Hand].GetRotation(),Bend);
            }
        }
    }
    const auto* Data=C ? (C->Weapon ? C->Weapon->Data.Get():nullptr):PreviewWeapon.Get();
    if(Mesh && Data && Data->bWorldReferencePose && ((!C && PreviewWeapon) || (C && !C->Health->bDead && !C->IsWeaponStowed())))
    {
        const auto& Ref=RigCache;auto& Pose=GetEditableComponentSpaceTransforms();
        const auto* PalmR=Mesh->FindSocket(TEXT("ER_Palm_R"));const auto* PalmL=Mesh->FindSocket(TEXT("ER_Palm_L"));
        const int32 ShoulderR=Ref.FindBoneIndex(TEXT("upperarm_r")),ShoulderL=Ref.FindBoneIndex(TEXT("upperarm_l"));
        if(PalmR && PalmL && Pose.IsValidIndex(ShoulderR) && Pose.IsValidIndex(ShoulderL))
        {
            const float BodyScale=GetComponentScale().X,Unit=1.f/FMath::Max(.1f,BodyScale);
            const FVector Right=GetComponentTransform().InverseTransformVectorNoScale(C ? C->GetActorRightVector():GetOwner()->GetActorRightVector()).GetSafeNormal();
            const FVector Up=GetComponentTransform().InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
            const bool Pistol=Data->bOneHanded && Data->Payload==EERPayload::Bullet;
            const FRotator BaseAim=C ? C->GetBaseAimRotation():GetOwner()->GetActorRotation();const float Pitch=FMath::Clamp(FRotator::NormalizeAxis(BaseAim.Pitch),-65.f,65.f);
            const FVector Aim=GetComponentTransform().InverseTransformVectorNoScale(FRotator(Pitch,BaseAim.Yaw,0).Vector()).GetSafeNormal();
            const FQuat GunRotation=FRotationMatrix::MakeFromXZ(Aim,Up).ToQuat();
            const float ShoulderHalfWidth=FVector::Distance(Pose[ShoulderR].GetLocation(),Pose[ShoulderL].GetLocation())*.5f;
            // Turn the chest as well as the shoulder girdle. Rotating only the
            // clavicles left the jacket facing straight into the stock line.
            if(!Pistol)
            {
                const FQuat ChestTurn(Up,FMath::DegreesToRadians(FMath::Lerp(FMath::Lerp(30.f,15.f,WorldAimBlend),FMath::Lerp(6.f,3.f,WorldAimBlend),Blend)));
                RotateBranch(Ref,Pose,TEXT("spine_01"),ChestTurn);
                RotateBranch(Ref,Pose,TEXT("neck_01"),ChestTurn.Inverse());
            }
            const float StandingShoulder=Pistol ? FMath::Lerp(40.f,45.f,WorldAimBlend):FMath::Lerp(25.f,30.f,WorldAimBlend);
            const FQuat ShoulderTurn(Up,FMath::DegreesToRadians(FMath::Lerp(StandingShoulder,Pistol ? 15.f:FMath::Lerp(8.f,12.f,WorldAimBlend),Blend)));
            RotateBranch(Ref,Pose,TEXT("clavicle_r"),ShoulderTurn);
            RotateBranch(Ref,Pose,TEXT("clavicle_l"),ShoulderTurn);
            FTransform Gun(GunRotation,FVector::ZeroVector,FVector(Data->WorldWeaponScale*Unit));
            // Neutral carry follows the upper-arm length, at right-elbow height.
            // The stock passes below the upper arm rather than through the chest.
            const int32 ElbowR=Ref.FindBoneIndex(TEXT("lowerarm_r"));
            const float UpperArm=Pose.IsValidIndex(ElbowR) ? FVector::Distance(Pose[ShoulderR].GetLocation(),Pose[ElbowR].GetLocation()):30.f*Unit;
            const FVector ShoulderCentre=(Pose[ShoulderR].GetLocation()+Pose[ShoulderL].GetLocation())*.5f;
            // Bring the stock 4 cm inward from the previous outer-arm line,
            // into the middle of the opening between jacket and upper arm.
            const float StockSide=ShoulderHalfWidth-1.f*Unit;
            const float ShoulderSide=FVector::DotProduct(Pose[ShoulderR].GetLocation()-ShoulderCentre,Right);
            const FVector Ready=Pose[ShoulderR].GetLocation()+Aim*(FMath::Lerp(10.f,4.f,Blend)*Unit)
                +Right*(StockSide-ShoulderSide)-Up*(UpperArm*.9f*(1.f-Blend));
            const FVector StockXY(Data->WorldStockContact.X,Data->WorldStockContact.Y,0);
            const FVector NeutralOrigin=Ready-Gun.TransformVector(StockXY);
            const FVector AimedPocket=Pose[ShoulderR].GetLocation()+Aim*(7.f*Unit)-Right*(3.f*Unit)+Up*(5.f*Unit);
            const FVector AimedOrigin=AimedPocket-Gun.TransformVector(Data->WorldStockContact);
            Gun.SetLocation(Pistol ? (Pose[ShoulderR].GetLocation()+Pose[ShoulderL].GetLocation())*.5f+Aim*(36*Unit)-Up*(12*Unit*(1.f-Blend)):FMath::Lerp(NeutralOrigin,AimedOrigin,WorldAimBlend));
            const bool GroundedRifle=GroundedProne && !Pistol && Data->Payload==EERPayload::Bullet;
            if(GroundedRifle)
                Gun.AddToTranslation((-Aim*5.f-Up*7.f)*(Blend*Unit));
            if(C)
            {
                const FTransform WorldGun=ERWorldWeaponPose::ClearObstacles(*Data,*C,Gun*GetComponentTransform());
                Gun=WorldGun.GetRelativeTransform(GetComponentTransform());
            }
            auto FitPalm=[&](bool LeftHand,FVector Contact,FQuat PalmRotation)
            {
                const auto* Socket=LeftHand ? PalmL:PalmR;const int32 Hand=Ref.FindBoneIndex(Socket->BoneName);
                if(!Pose.IsValidIndex(Hand))return;
                const FQuat Rotation=Gun.GetRotation()*PalmRotation*Socket->GetSocketLocalTransform().GetRotation().Inverse();
                const FVector Wrist=Gun.TransformPosition(Contact)-Rotation.RotateVector(Socket->RelativeLocation*Pose[Hand].GetScale3D());
                const int32 Upper=Ref.FindBoneIndex(LeftHand ? TEXT("upperarm_l"):TEXT("upperarm_r")),Lower=Ref.FindBoneIndex(LeftHand ? TEXT("lowerarm_l"):TEXT("lowerarm_r"));
                FVector Bend=(LeftHand ? -Right:Right)*.65f-Up*.8f;
                if(GroundedProne && Pose.IsValidIndex(Upper) && Pose.IsValidIndex(Lower))
                    Bend=FMath::Lerp(Bend,GroundedArmPole(Pose[Upper].GetLocation(),Pose[Lower].GetLocation(),Pose[Hand].GetLocation(),Wrist,Up,LeftHand ? -Right:Right,GroundedRifle ? ElbowGround(LeftHand):ProneGround,C,GetComponentTransform(),ProneFloorZ),Blend);
                FitLimb(Ref,Pose,LeftHand ? TEXT("upperarm_l"):TEXT("upperarm_r"),LeftHand ? TEXT("lowerarm_l"):TEXT("lowerarm_r"),LeftHand ? TEXT("hand_l"):TEXT("hand_r"),Wrist,Rotation,Bend);
            };
            FitPalm(false,Data->WorldRightPalmContact,Data->WorldRightPalmRotation.Quaternion());
            // The physical right hand owns the attachment; resolve support from
            // its actual reachable result, rather than an independent gun target.
            const int32 RH=Ref.FindBoneIndex(PalmR->BoneName);
            const float ParentScale=BodyScale*(PalmR->GetSocketLocalTransform()*Pose[RH]).GetScale3D().X;
            Gun=ERWorldWeaponPose::Attachment(*Data,ParentScale)*PalmR->GetSocketLocalTransform()*Pose[RH];Gun.SetScale3D(FVector(Data->WorldWeaponScale*Unit));
            const FVector Contact=ERWorldWeaponPose::SupportContact(*Data,C ? C->GetTeamAppearance():PreviewAppearance);
            const FQuat PalmRotation=Pistol ? FRotationMatrix::MakeFromXZ(FVector::ForwardVector,FVector::RightVector).ToQuat():Data->WorldLeftPalmRotation.Quaternion();
            FitPalm(true,Contact,PalmRotation);
        }
    }
    if(Mesh && C && Data && !Data->bWorldReferencePose && !C->Health->bDead && !C->IsWeaponStowed())
    {
        const auto& Ref=RigCache;auto& Pose=GetEditableComponentSpaceTransforms();
        const auto* Grip=Mesh->FindSocket(TEXT("HandGrip_R"));
        const int32 Hand=Grip ? Ref.FindBoneIndex(Grip->BoneName):INDEX_NONE;
        if(Pose.IsValidIndex(Hand))
        {
            const FTransform Attachment=Data->GripTransform*Grip->GetSocketLocalTransform();
            const FTransform Gun=Attachment*Pose[Hand]*GetComponentTransform();
            const FTransform Clear=ERWorldWeaponPose::ClearObstacles(*Data,*C,Gun);
            if(!Clear.Equals(Gun,.01f))
            {
                const FTransform Wrist=Attachment.Inverse()*Clear.GetRelativeTransform(GetComponentTransform());
                const FVector Up=GetComponentTransform().InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
                const FVector Side=GetComponentTransform().InverseTransformVectorNoScale(C->GetActorRightVector()).GetSafeNormal();
                FVector Bend=Side*.65f-Up*.8f;
                const int32 Upper=Ref.FindBoneIndex(TEXT("upperarm_r")),Lower=Ref.FindBoneIndex(TEXT("lowerarm_r"));
                if(GroundedProne && Pose.IsValidIndex(Upper) && Pose.IsValidIndex(Lower))
                    Bend=FMath::Lerp(Bend,GroundedArmPole(Pose[Upper].GetLocation(),Pose[Lower].GetLocation(),Pose[Hand].GetLocation(),Wrist.GetLocation(),Up,Side,ElbowGround(false),C,GetComponentTransform(),ProneFloorZ),Blend);
                FitLimb(Ref,Pose,TEXT("upperarm_r"),TEXT("lowerarm_r"),TEXT("hand_r"),Wrist.GetLocation(),Wrist.GetRotation(),Bend);
            }
        }
    }
    if (Mesh && Data && Data->bFitSupportHand && !Data->bWorldReferencePose)
    {
        const auto& Ref=RigCache;auto& Pose=GetEditableComponentSpaceTransforms();
        const auto* Right=Mesh->FindSocket(TEXT("HandGrip_R"));
        const auto* Surface=Data->SupportSurfaceContacts.IsEmpty() ? nullptr : Mesh->FindSocket(TEXT("ER_Palm_L"));
        const auto* Left=Surface ? Surface : Mesh->FindSocket(TEXT("HandGrip_L"));
        auto FitArm=[&](const TCHAR* UpperName,const TCHAR* LowerName,const TCHAR* HandName,FVector Target,FQuat Rotation)
        {
            const int32 Upper=Ref.FindBoneIndex(UpperName),Lower=Ref.FindBoneIndex(LowerName),Hand=Ref.FindBoneIndex(HandName);
            if (!Pose.IsValidIndex(Upper) || !Pose.IsValidIndex(Lower) || !Pose.IsValidIndex(Hand)) return;
            const FTransform OldUpper=Pose[Upper],OldLower=Pose[Lower],OldHand=Pose[Hand];
            const FVector Shoulder=Pose[Upper].GetLocation(),Elbow=Pose[Lower].GetLocation(),Wrist=Pose[Hand].GetLocation();
            const float A=FVector::Distance(Shoulder,Elbow),B=FVector::Distance(Elbow,Wrist);
            const FVector Direction=(Target-Shoulder).GetSafeNormal();
            const float D=FMath::Clamp(FVector::Distance(Shoulder,Target),FMath::Abs(A-B)+.1f,A+B-.1f);
            FVector Bend=FVector::VectorPlaneProject(Elbow-Shoulder,Direction).GetSafeNormal();
            if (Bend.IsNearlyZero()) Bend=FVector::VectorPlaneProject(-FVector::ZAxisVector,Direction).GetSafeNormal();
            const float Along=(A*A-B*B+D*D)/(2.f*D);
            const FVector NewElbow=Shoulder+Direction*Along+Bend*FMath::Sqrt(FMath::Max(0.f,A*A-Along*Along));
            const FVector NewWrist=Shoulder+Direction*D;
            FTransform NewUpper=Pose[Upper],NewLower=Pose[Lower],NewHand=Pose[Hand];
            NewUpper.SetRotation(FQuat::FindBetweenNormals((Elbow-Shoulder).GetSafeNormal(),(NewElbow-Shoulder).GetSafeNormal())*NewUpper.GetRotation());
            NewLower.SetRotation(FQuat::FindBetweenNormals((Wrist-Elbow).GetSafeNormal(),(NewWrist-NewElbow).GetSafeNormal())*NewLower.GetRotation());
            NewLower.SetLocation(NewElbow);NewHand.SetLocation(NewWrist);NewHand.SetRotation(Rotation);
            for (int32 Bone=Upper;Bone<Pose.Num();++Bone)
            {
                const FTransform* Before=Ref.InSubtree(Bone,Hand) ? &OldHand:Ref.InSubtree(Bone,Lower) ? &OldLower:Ref.InSubtree(Bone,Upper) ? &OldUpper:nullptr;
                const FTransform* After=Before==&OldHand ? &NewHand:Before==&OldLower ? &NewLower:&NewUpper;
                if(Before)Pose[Bone]=Pose[Bone].GetRelativeTransform(*Before)**After;
            }
        };
        const int32 RightBone=Right ? Ref.FindBoneIndex(Right->BoneName) : INDEX_NONE;
        const int32 Upper=Ref.FindBoneIndex(TEXT("upperarm_l")),Lower=Ref.FindBoneIndex(TEXT("lowerarm_l")),Hand=Ref.FindBoneIndex(TEXT("hand_l"));
        if (Right && Left && Pose.IsValidIndex(RightBone) && Pose.IsValidIndex(Upper) && Pose.IsValidIndex(Lower) && Pose.IsValidIndex(Hand))
        {
            FTransform Weapon=Data->GripTransform * Right->GetSocketLocalTransform() * Pose[RightBone];
            if (Surface)
            {
                // Seat the stock near the shoulder rather than holding the gun at full reach.
                FitArm(TEXT("upperarm_r"),TEXT("lowerarm_r"),TEXT("hand_r"),Pose[RightBone].GetLocation()-Weapon.GetUnitAxis(EAxis::X)*12.f,Pose[RightBone].GetRotation());
                Weapon=Data->GripTransform * Right->GetSocketLocalTransform() * Pose[RightBone];
            }
            const FQuat Rotation=Surface ? Weapon.GetRotation()*Data->SupportPalmRotation.Quaternion()*Left->GetSocketLocalTransform().GetRotation().Inverse() : Pose[Hand].GetRotation();
            const FVector Offset=Rotation.RotateVector(Left->RelativeLocation*Pose[Hand].GetScale3D());
            const FVector Shoulder=Pose[Upper].GetLocation();
            const float Reach=FVector::Distance(Shoulder,Pose[Lower].GetLocation())+FVector::Distance(Pose[Lower].GetLocation(),Pose[Hand].GetLocation())-.1f;
            FVector Target=Weapon.TransformPosition(Data->SupportHandOffset)-Offset;
            if (Surface)
            {
                for (const FVector& Point : Data->SupportSurfaceContacts)
                {
                    Target=Weapon.TransformPosition(Point)-Offset;
                    if (FVector::Distance(Shoulder,Target)<=Reach) break;
                }
            }
            else for (float Slide=.25f;Slide<=10.f && FVector::Distance(Shoulder,Target)>Reach;Slide+=.25f)
                Target=Weapon.TransformPosition(Data->SupportHandOffset-FVector(Slide,0,0))-Offset;
            FitArm(TEXT("upperarm_l"),TEXT("lowerarm_l"),TEXT("hand_l"),Target,Rotation);
        }
    }
    if(Mesh && C && C->Weapon && Data && AERWeapon::IsHandGrenade(Data->Payload) &&
        !C->Health->bDead && C->GrenadeThrowPoseBlend>.001f)
    {
        const auto& Ref=RigCache;auto& Pose=GetEditableComponentSpaceTransforms();
        const int32 Upper=Ref.FindBoneIndex(TEXT("upperarm_r")),Lower=Ref.FindBoneIndex(TEXT("lowerarm_r")),Hand=Ref.FindBoneIndex(TEXT("hand_r"));
        if(Pose.IsValidIndex(Upper) && Pose.IsValidIndex(Lower) && Pose.IsValidIndex(Hand))
        {
            const FTransform OldUpper=Pose[Upper],OldLower=Pose[Lower],OldHand=Pose[Hand];
            const FRotator Yaw(0,C->GetBaseAimRotation().Yaw,0);
            const FVector TargetWorld=C->Camera->GetComponentLocation()+Yaw.RotateVector(FVector(-14,24,-4));
            const FVector Target=GetComponentTransform().InverseTransformPosition(TargetWorld);
            const FVector Shoulder=Pose[Upper].GetLocation(),Elbow=Pose[Lower].GetLocation(),Wrist=Pose[Hand].GetLocation();
            const float A=FVector::Distance(Shoulder,Elbow),B=FVector::Distance(Elbow,Wrist);
            const FVector Direction=(Target-Shoulder).GetSafeNormal();
            const float D=FMath::Clamp(FVector::Distance(Shoulder,Target),FMath::Abs(A-B)+.1f,A+B-.1f);
            FVector Bend=FVector::VectorPlaneProject(GetComponentTransform().InverseTransformVectorNoScale(Yaw.RotateVector(FVector(.2f,1.f,0))),Direction).GetSafeNormal();
            if(Bend.IsNearlyZero()) Bend=FVector::VectorPlaneProject(FVector::ZAxisVector,Direction).GetSafeNormal();
            const float Along=(A*A-B*B+D*D)/(2.f*D);
            const FVector NewElbow=Shoulder+Direction*Along+Bend*FMath::Sqrt(FMath::Max(0.f,A*A-Along*Along));
            const FVector NewWrist=Shoulder+Direction*D;
            const FQuat UpperTurn=FQuat::FindBetweenNormals((Elbow-Shoulder).GetSafeNormal(),(NewElbow-Shoulder).GetSafeNormal());
            const FQuat LowerTurn=FQuat::FindBetweenNormals((Wrist-Elbow).GetSafeNormal(),(NewWrist-NewElbow).GetSafeNormal());
            FTransform NewUpper=Pose[Upper],NewLower=Pose[Lower],NewHand=Pose[Hand];
            NewUpper.SetRotation(UpperTurn*NewUpper.GetRotation());
            NewLower.SetLocation(NewElbow);NewLower.SetRotation(LowerTurn*NewLower.GetRotation());
            NewHand.SetLocation(NewWrist);NewHand.SetRotation(LowerTurn*NewHand.GetRotation());
            for(int32 Bone=Upper;Bone<Pose.Num();++Bone)
            {
                const FTransform* Before=Ref.InSubtree(Bone,Hand) ? &OldHand:Ref.InSubtree(Bone,Lower) ? &OldLower:Ref.InSubtree(Bone,Upper) ? &OldUpper:nullptr;
                const FTransform* After=Before==&OldHand ? &NewHand:Before==&OldLower ? &NewLower:&NewUpper;
                if(Before)
                {
                    const FTransform Original=Pose[Bone];
                    const FTransform Ready=Original.GetRelativeTransform(*Before)**After;
                    Pose[Bone].Blend(Original,Ready,C->GrenadeThrowPoseBlend);
                }
            }
        }
    }
    if(Mesh && C && Data && AERWeapon::IsHandGrenade(Data->Payload) && !C->Health->bDead && C->GrenadeThrowPoseBlend>.001f)
    {
        // Relax the empty support arm instead of leaving the rifle support pose.
        const auto& Ref=RigCache;auto& Pose=GetEditableComponentSpaceTransforms();
        const int32 Upper=Ref.FindBoneIndex(TEXT("upperarm_l")),Lower=Ref.FindBoneIndex(TEXT("lowerarm_l"));
        if(Pose.IsValidIndex(Upper) && Pose.IsValidIndex(Lower))
        {
            const FVector Pivot=Pose[Upper].GetLocation();
            const FVector RestWorld=FMath::Lerp(FVector(0,0,-1),C->GetActorForwardVector(),Blend).GetSafeNormal();
            const FVector Down=GetComponentTransform().InverseTransformVectorNoScale(RestWorld).GetSafeNormal();
            const FQuat Turn=FQuat::FindBetweenNormals((Pose[Lower].GetLocation()-Pivot).GetSafeNormal(),Down);
            for(int32 Bone=Upper;Bone<Pose.Num();++Bone)
            {
                if(!Ref.InSubtree(Bone,Upper))continue;
                const FTransform Original=Pose[Bone];FTransform Rest=Original;
                Rest.SetLocation(Pivot+Turn.RotateVector(Original.GetLocation()-Pivot));Rest.SetRotation(Turn*Original.GetRotation());
                Pose[Bone].Blend(Original,Rest,C->GrenadeThrowPoseBlend);
            }
        }
    }
    // The engine pushes kinematic bodies before this override. Server hit volumes
    // must take the visible procedural pose, or bullets test an arm and head that moved.
    if(C && C->HasAuthority() && !Bodies.IsEmpty())
        UpdateKinematicBonesToAnim(GetEditableComponentSpaceTransforms(),ETeleportType::TeleportPhysics,false,EAllowKinematicDeferral::DisallowDeferral);
    Super::FinalizeBoneTransform();
    if(C && C->HasAuthority() && !C->Health->bDead && !Bodies.IsEmpty())ShotHistory.Record(this);
    else ShotHistory.Reset();
}
