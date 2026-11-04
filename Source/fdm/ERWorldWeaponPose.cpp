#include "ERWorldWeaponPose.h"
#include "ERWeapon.h"
#include "ERTeamAppearance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
FVector ERWorldWeaponPose::SupportContact(const UERWeaponData& Data,const FERTeamAppearance& Appearance)
{
    return Data.bOneHanded ? Data.WorldPistolSupportContact:
        Data.WorldLeftPalmContact+Appearance.WorldSupportPalmOffsetCm/FMath::Max(.01f,Data.WorldWeaponScale);
}
FTransform ERWorldWeaponPose::Attachment(const UERWeaponData& Data,float ParentScale)
{
    const FQuat Rotation=Data.WorldRightPalmRotation.Quaternion().Inverse();
    return FTransform(Rotation,Rotation.RotateVector(-Data.WorldRightPalmContact*Data.WorldWeaponScale)/FMath::Max(.01f,ParentScale),FVector(Data.WorldWeaponScale));
}
FTransform ERWorldWeaponPose::ClearObstacles(const UERWeaponData& Data,const AActor& Character,const FTransform& Desired)
{
    if (!Data.Mesh || !Character.GetWorld()) return Desired;
    const FBoxSphereBounds Bounds = Data.Mesh->GetBounds();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERWeaponClearance),false,&Character);
    Query.AddIgnoredActor(Character.GetOwner());
    auto Clear = [&](const FTransform& Candidate)
    {
        FHitResult Cover;
        if(Character.GetWorld()->LineTraceSingleByChannel(Cover,Character.GetActorLocation(),
            Candidate.TransformPosition(Bounds.Origin),ECC_Pawn,Query))return false;
        return !Character.GetWorld()->OverlapBlockingTestByChannel(Candidate.TransformPosition(Bounds.Origin),
            Candidate.GetRotation(),ECC_Pawn,FCollisionShape::MakeBox(Bounds.BoxExtent * Candidate.GetScale3D().GetAbs() + FVector(2)),Query);
    };
    if (Clear(Desired)) return Desired;
    // Keep the full weapon bounds on this side of cover, including the stock
    // and magazine. Search a bounded tuck, with both palms solved afterwards.
    const FVector Back = -Character.GetActorForwardVector();
    for (int32 Step=1;Step<=12;++Step)
    {
        for (int32 Lift=0;Lift<=Step;++Lift)
        {
            FTransform Candidate=Desired;
            Candidate.AddToTranslation(Back*(Step*3.f)+FVector::UpVector*(Lift*3.f));
            if (Clear(Candidate)) return Candidate;
        }
    }
    return Desired;
}
