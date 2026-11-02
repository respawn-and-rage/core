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

namespace
{
bool Descendant(const FReferenceSkeleton& Ref,int32 Bone,int32 Root)
{for(int32 I=Bone;I!=INDEX_NONE;I=Ref.GetParentIndex(I))if(I==Root)return true;return false;}
void RotateBranch(const FReferenceSkeleton& Ref,TArray<FTransform>& Pose,const TCHAR* RootName,FQuat Rotation)
{
    const int32 Root=Ref.FindBoneIndex(RootName);if(!Pose.IsValidIndex(Root))return;
    const TArray<FTransform> Old=Pose;const FVector Pivot=Old[Root].GetLocation();
    for(int32 I=0;I<Pose.Num();++I)if(Descendant(Ref,I,Root))
    {
        Pose[I].SetLocation(Pivot+Rotation.RotateVector(Old[I].GetLocation()-Pivot));
        Pose[I].SetRotation(Rotation*Old[I].GetRotation());
    }
}
void FitLimb(const FReferenceSkeleton& Ref,TArray<FTransform>& Pose,const TCHAR* UpperName,const TCHAR* LowerName,const TCHAR* HandName,FVector Target,FQuat Rotation,FVector Pole)
{
    const int32 Upper=Ref.FindBoneIndex(UpperName),Lower=Ref.FindBoneIndex(LowerName),Hand=Ref.FindBoneIndex(HandName);
    if(!Pose.IsValidIndex(Upper) || !Pose.IsValidIndex(Lower) || !Pose.IsValidIndex(Hand))return;
    const TArray<FTransform> Old=Pose;
    const FVector S=Pose[Upper].GetLocation(),E=Pose[Lower].GetLocation(),W=Pose[Hand].GetLocation();
    const float A=FVector::Dist(S,E),B=FVector::Dist(E,W),D=FMath::Clamp(float(FVector::Dist(S,Target)),FMath::Abs(A-B)+.1f,A+B-.1f);
    const FVector Direction=(Target-S).GetSafeNormal();FVector Bend=FVector::VectorPlaneProject(Pole,Direction).GetSafeNormal();
    if(Bend.IsNearlyZero())Bend=FVector::VectorPlaneProject(E-S,Direction).GetSafeNormal();
    const float Along=(A*A-B*B+D*D)/(2*D);const FVector NewE=S+Direction*Along+Bend*FMath::Sqrt(FMath::Max(0.f,A*A-Along*Along)),NewW=S+Direction*D;
    FTransform U=Old[Upper],L=Old[Lower],H=Old[Hand];
    U.SetRotation(FQuat::FindBetweenNormals((E-S).GetSafeNormal(),(NewE-S).GetSafeNormal())*U.GetRotation());
    L.SetLocation(NewE);L.SetRotation(FQuat::FindBetweenNormals((W-E).GetSafeNormal(),(NewW-NewE).GetSafeNormal())*L.GetRotation());H.SetLocation(NewW);H.SetRotation(Rotation);
    for(int32 I=0;I<Pose.Num();++I)
    {
        int32 Parent=I;while(Parent!=INDEX_NONE && Parent!=Upper && Parent!=Lower && Parent!=Hand)Parent=Ref.GetParentIndex(Parent);
        const FTransform* New=Parent==Hand ? &H:Parent==Lower ? &L:Parent==Upper ? &U:nullptr;
        if(New)Pose[I]=Old[I].GetRelativeTransform(Old[Parent])**New;
    }
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
    const auto* C = Cast<AERCharacter>(GetOwner());
    const auto* M = C ? Cast<UERMovement>(C->GetCharacterMovement()) : nullptr;
    const auto* Mesh = GetSkeletalMeshAsset();
    const float Blend = M ? M->Locomotion.ProneBlend : 0.f;
    if (Mesh && Blend > 0)
    {
        const auto& Ref = Mesh->GetRefSkeleton();
        auto& Pose = GetEditableComponentSpaceTransforms();
        const TArray<FTransform> Original = Pose;
        const int32 Left = Ref.FindBoneIndex(TEXT("upperarm_l")), Right = Ref.FindBoneIndex(TEXT("upperarm_r"));
        const int32 Neck = Ref.FindBoneIndex(TEXT("neck_01"));
        const int32 Pelvis = Ref.FindBoneIndex(TEXT("pelvis"));
        const int32 ThighLeft = Ref.FindBoneIndex(TEXT("thigh_l")), ThighRight = Ref.FindBoneIndex(TEXT("thigh_r"));
        auto InSubtree = [&Ref](int32 Bone, int32 Root)
        {
            if (Root == INDEX_NONE) return false;
            for (int32 I = Bone; I != INDEX_NONE; I = Ref.GetParentIndex(I)) if (I == Root) return true;
            return false;
        };
        const FVector Shoulder = Pose.IsValidIndex(Left) && Pose.IsValidIndex(Right) ?
            (Pose[Left].GetLocation() + Pose[Right].GetLocation()) * .5f : FVector::ZeroVector;
        // A common shoulder pivot keeps both rifle grips together while the body lies flat.
        const FQuat Arms(FVector::XAxisVector, Blend * PI * .5f);
        const FQuat Head(FVector::XAxisVector, Blend * FMath::DegreesToRadians(70.f));
        TArray<FTransform> StraightLegs;
        StraightLegs.SetNum(Pose.Num());
        for (int32 Bone = 0; Bone < Pose.Num(); ++Bone)
        {
            const int32 Parent = Ref.GetParentIndex(Bone);
            StraightLegs[Bone] = Parent == INDEX_NONE ? Ref.GetRefBonePose()[Bone] : Ref.GetRefBonePose()[Bone] * StraightLegs[Parent];
        }
        const FVector PelvisShift = Pose.IsValidIndex(Pelvis) ? Original[Pelvis].GetLocation() - StraightLegs[Pelvis].GetLocation() : FVector::ZeroVector;
        for (int32 Bone = 0; Bone < Pose.Num(); ++Bone)
        {
            if (InSubtree(Bone,ThighLeft) || InSubtree(Bone,ThighRight))
            {
                const int32 Parent = Ref.GetParentIndex(Bone);
                if (Pose.IsValidIndex(Parent) && Pelvis != INDEX_NONE)
                {
                    StraightLegs[Bone].AddToTranslation(PelvisShift);
                    Pose[Bone].Blend(Original[Bone], StraightLegs[Bone], Blend);
                }
            }
            else if (InSubtree(Bone,Left) || InSubtree(Bone,Right))
            {
                Pose[Bone].SetLocation(Shoulder + Arms.RotateVector(Original[Bone].GetLocation() - Shoulder));
                Pose[Bone].SetRotation(Arms * Original[Bone].GetRotation());
            }
            else if (InSubtree(Bone,Neck) && Pose.IsValidIndex(Neck))
            {
                const FVector Pivot = Original[Neck].GetLocation();
                Pose[Bone].SetLocation(Pivot + Head.RotateVector(Original[Bone].GetLocation() - Pivot));
                Pose[Bone].SetRotation(Head * Original[Bone].GetRotation());
            }
        }
    }
    const auto* Data=C ? (C->Weapon ? C->Weapon->Data.Get():nullptr):PreviewWeapon.Get();
    if(Mesh && Data && Data->bWorldReferencePose && ((!C && PreviewWeapon) || (C && !C->Health->bDead && !C->IsWeaponStowed())))
    {
        const auto& Ref=Mesh->GetRefSkeleton();auto& Pose=GetEditableComponentSpaceTransforms();
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
                const FQuat ChestTurn(Up,FMath::DegreesToRadians(FMath::Lerp(30.f,15.f,WorldAimBlend)));
                RotateBranch(Ref,Pose,TEXT("spine_01"),ChestTurn);
                RotateBranch(Ref,Pose,TEXT("neck_01"),ChestTurn.Inverse());
            }
            const FQuat ShoulderTurn(Up,FMath::DegreesToRadians(Pistol ? FMath::Lerp(40.f,45.f,WorldAimBlend):FMath::Lerp(25.f,30.f,WorldAimBlend)));
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
            const FVector Ready=Pose[ShoulderR].GetLocation()+Aim*(10.f*Unit)
                +Right*(StockSide-ShoulderSide)-Up*(UpperArm*.9f);
            const FVector StockXY(Data->WorldStockContact.X,Data->WorldStockContact.Y,0);
            const FVector NeutralOrigin=Ready-Gun.TransformVector(StockXY);
            const FVector AimedPocket=Pose[ShoulderR].GetLocation()+Aim*(7.f*Unit)-Right*(3.f*Unit)+Up*(5.f*Unit);
            const FVector AimedOrigin=AimedPocket-Gun.TransformVector(Data->WorldStockContact);
            Gun.SetLocation(Pistol ? (Pose[ShoulderR].GetLocation()+Pose[ShoulderL].GetLocation())*.5f+Aim*(36*Unit)-Up*(12*Unit):FMath::Lerp(NeutralOrigin,AimedOrigin,WorldAimBlend));
            auto FitPalm=[&](bool LeftHand,FVector Contact,FQuat PalmRotation)
            {
                const auto* Socket=LeftHand ? PalmL:PalmR;const int32 Hand=Ref.FindBoneIndex(Socket->BoneName);
                if(!Pose.IsValidIndex(Hand))return;
                const FQuat Rotation=GunRotation*PalmRotation*Socket->GetSocketLocalTransform().GetRotation().Inverse();
                const FVector Wrist=Gun.TransformPosition(Contact)-Rotation.RotateVector(Socket->RelativeLocation*Pose[Hand].GetScale3D());
                FitLimb(Ref,Pose,LeftHand ? TEXT("upperarm_l"):TEXT("upperarm_r"),LeftHand ? TEXT("lowerarm_l"):TEXT("lowerarm_r"),LeftHand ? TEXT("hand_l"):TEXT("hand_r"),Wrist,Rotation,(LeftHand ? -Right:Right)*.65f-Up*.8f);
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
    if (Mesh && Data && Data->bFitSupportHand && !Data->bWorldReferencePose)
    {
        const auto& Ref=Mesh->GetRefSkeleton();auto& Pose=GetEditableComponentSpaceTransforms();
        const auto* Right=Mesh->FindSocket(TEXT("HandGrip_R"));
        const auto* Surface=Data->SupportSurfaceContacts.IsEmpty() ? nullptr : Mesh->FindSocket(TEXT("ER_Palm_L"));
        const auto* Left=Surface ? Surface : Mesh->FindSocket(TEXT("HandGrip_L"));
        auto FitArm=[&](const TCHAR* UpperName,const TCHAR* LowerName,const TCHAR* HandName,FVector Target,FQuat Rotation)
        {
            const int32 Upper=Ref.FindBoneIndex(UpperName),Lower=Ref.FindBoneIndex(LowerName),Hand=Ref.FindBoneIndex(HandName);
            if (!Pose.IsValidIndex(Upper) || !Pose.IsValidIndex(Lower) || !Pose.IsValidIndex(Hand)) return;
            const TArray<FTransform> Original=Pose;
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
                int32 Ancestor=Bone;
                while (Ancestor!=INDEX_NONE && Ancestor!=Upper && Ancestor!=Lower && Ancestor!=Hand) Ancestor=Ref.GetParentIndex(Ancestor);
                const FTransform* Replacement=Ancestor==Hand ? &NewHand : Ancestor==Lower ? &NewLower : Ancestor==Upper ? &NewUpper : nullptr;
                if (Replacement) Pose[Bone]=Original[Bone].GetRelativeTransform(Original[Ancestor]) * *Replacement;
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
        const auto& Ref=Mesh->GetRefSkeleton();auto& Pose=GetEditableComponentSpaceTransforms();
        const int32 Upper=Ref.FindBoneIndex(TEXT("upperarm_r")),Lower=Ref.FindBoneIndex(TEXT("lowerarm_r")),Hand=Ref.FindBoneIndex(TEXT("hand_r"));
        if(Pose.IsValidIndex(Upper) && Pose.IsValidIndex(Lower) && Pose.IsValidIndex(Hand))
        {
            const TArray<FTransform> Original=Pose;
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
                int32 Ancestor=Bone;
                while(Ancestor!=INDEX_NONE && Ancestor!=Upper && Ancestor!=Lower && Ancestor!=Hand) Ancestor=Ref.GetParentIndex(Ancestor);
                const FTransform* Replacement=Ancestor==Hand?&NewHand:Ancestor==Lower?&NewLower:Ancestor==Upper?&NewUpper:nullptr;
                if(Replacement)
                {
                    const FTransform Ready=Original[Bone].GetRelativeTransform(Original[Ancestor]) * *Replacement;
                    Pose[Bone].Blend(Original[Bone],Ready,C->GrenadeThrowPoseBlend);
                }
            }
        }
    }
    if(Mesh && C && Data && AERWeapon::IsHandGrenade(Data->Payload) && !C->Health->bDead && C->GrenadeThrowPoseBlend>.001f)
    {
        // Relax the empty support arm instead of leaving the rifle support pose.
        const auto& Ref=Mesh->GetRefSkeleton();auto& Pose=GetEditableComponentSpaceTransforms();
        const int32 Upper=Ref.FindBoneIndex(TEXT("upperarm_l")),Lower=Ref.FindBoneIndex(TEXT("lowerarm_l"));
        if(Pose.IsValidIndex(Upper) && Pose.IsValidIndex(Lower))
        {
            const FVector Pivot=Pose[Upper].GetLocation();
            const FVector RestWorld=FMath::Lerp(FVector(0,0,-1),C->GetActorForwardVector(),Blend).GetSafeNormal();
            const FVector Down=GetComponentTransform().InverseTransformVectorNoScale(RestWorld).GetSafeNormal();
            const FQuat Turn=FQuat::FindBetweenNormals((Pose[Lower].GetLocation()-Pivot).GetSafeNormal(),Down);
            for(int32 Bone=Upper;Bone<Pose.Num();++Bone)
            {
                int32 Ancestor=Bone;while(Ancestor!=INDEX_NONE && Ancestor!=Upper) Ancestor=Ref.GetParentIndex(Ancestor);
                if(Ancestor!=Upper) continue;
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
    else ShotHistory.Frames.Reset();
}
