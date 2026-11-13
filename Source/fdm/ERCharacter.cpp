#include "ERCharacter.h"
#include "ERWounds.h"
#include "ERWorldWeaponPose.h"
#include "ERClaymore.h"
#include "ERGrenadeExplosion.h"
#include "ERLoadout.h"
#include "ERFootsteps.h"
#include "ERPostureMesh.h"
#include "EROutdoorBackdrop.h"
#include "ERCorpse.h"
#include "Engine/DamageEvents.h"
#include "ERMatch.h"
#include "ERWeapon.h"
#include "ERWeaponBallistics.h"
#include "ERMovement.h"
#include "ERPlayerController.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h"
#include "Animation/AnimSequence.h"
#include "ERSmokeCloud.h"
#include "ERShotEffects.h"
#include "ERShotTrace.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Sound/SoundBase.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#if WITH_EDITOR
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "Animation/Skeleton.h"
#endif

bool AERCharacter::CopyTrainingGripSockets(USkeletalMesh* TargetMesh, USkeletalMesh* SourceMesh)
{
#if WITH_EDITOR
    if (!TargetMesh || !SourceMesh || TargetMesh == SourceMesh ||
        !TargetMesh->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Characters/"))) return false;
    TargetMesh->Modify();
    for (const FName Name : {FName(TEXT("HandGrip_R")), FName(TEXT("HandGrip_L"))})
    {
        if (TargetMesh->FindSocket(Name)) continue;
        const USkeletalMeshSocket* Source = SourceMesh->FindSocket(Name);
        if (!Source || TargetMesh->GetRefSkeleton().FindBoneIndex(Source->BoneName) == INDEX_NONE) return false;
        TargetMesh->AddSocket(DuplicateObject<USkeletalMeshSocket>(Source, TargetMesh));
    }
    TargetMesh->PostEditChange();
    return true;
#else
    return false;
#endif
}
FTransform AERCharacter::GetWeaponReferenceBone(USkeletalMesh* WeaponMesh, FName Bone)
{
    if (!WeaponMesh) return FTransform::Identity;
    const FReferenceSkeleton& Ref = WeaponMesh->GetRefSkeleton();
    int32 Index = Ref.FindBoneIndex(Bone);
    if (Index == INDEX_NONE) return FTransform::Identity;
    FTransform Result = Ref.GetRefBonePose()[Index];
    while ((Index = Ref.GetParentIndex(Index)) != INDEX_NONE) Result *= Ref.GetRefBonePose()[Index];
    return Result;
}
bool AERCharacter::ConfigureKnifeGripEditor(USkeletalMesh* PreviewMesh,UAnimSequence* Pose,
    USkeletalMesh* KnifeMesh,FTransform KnifeRelativeToRoot)
{
#if WITH_EDITOR
    // Only isolated editing copies: never touch the runtime skin/skeleton sockets.
    if(!PreviewMesh || !Pose || !KnifeMesh ||
        !PreviewMesh->GetPathName().Contains(TEXT("/KnifeGripEditing/")) ||
        !Pose->GetPathName().Contains(TEXT("/KnifeGripEditing/")))return false;
    PreviewMesh->Modify();
    const FName Name(TEXT("Knife_Edit_Reference"));
    auto* Socket=const_cast<USkeletalMeshSocket*>(PreviewMesh->FindSocket(Name));
    if(!Socket){Socket=NewObject<USkeletalMeshSocket>(PreviewMesh);Socket->SocketName=Name;PreviewMesh->AddSocket(Socket);}
    Socket->BoneName=TEXT("root");Socket->RelativeLocation=KnifeRelativeToRoot.GetTranslation();
    Socket->RelativeRotation=KnifeRelativeToRoot.Rotator();Socket->RelativeScale=KnifeRelativeToRoot.GetScale3D();
    PreviewMesh->RebuildSocketMap();
    FPreviewAssetAttachContainer Attached;Attached.AddAttachedObject(KnifeMesh,Name);
    PreviewMesh->SetPreviewAttachedAssetContainer(Attached);
    Pose->Modify();Pose->SetPreviewMesh(PreviewMesh);PreviewMesh->MarkPackageDirty();Pose->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}
TArray<FVector> AERCharacter::GetPresentationVertices(USkeletalMesh* PresentationMesh, FName Bone)
{
    TArray<FVector> Result;
#if WITH_EDITOR
    if (!PresentationMesh) return Result;
    FMeshDescription* Description=PresentationMesh->GetMeshDescription(0);
    if (!Description) return Result;
    FSkeletalMeshAttributes Attributes(*Description);
    const auto Positions=Attributes.GetVertexPositions();
    const auto Weights=Attributes.GetVertexSkinWeights();
    int32 DescriptionBone=INDEX_NONE;
    for (const FBoneID ID : Attributes.Bones().GetElementIDs())
        if (Attributes.GetBoneNames()[ID]==Bone) DescriptionBone=ID.GetValue();
    const FTransform Frame=Bone.IsNone() ? FTransform::Identity : GetWeaponReferenceBone(PresentationMesh,Bone);
    for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
    {
        bool Include=Bone.IsNone();
        if (!Include) for (const auto Weight : Weights.Get(Vertex))
            if (Weight.GetBoneIndex()==DescriptionBone && Weight.GetWeight()>.65f) Include=true;
        if (Include) Result.Add(Frame.InverseTransformPosition(FVector(Positions[Vertex])));
    }
#endif
    return Result;
}
bool AERCharacter::SetPalmContactSocket(USkeletalMesh* PresentationMesh, FName Name, FName Bone, FTransform Transform)
{
#if WITH_EDITOR
    if (!PresentationMesh || !PresentationMesh->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Characters/")) ||
        (Name!=TEXT("ER_Palm_L") && Name!=TEXT("ER_Palm_R")) || PresentationMesh->GetRefSkeleton().FindBoneIndex(Bone)==INDEX_NONE) return false;
    PresentationMesh->Modify();
    auto* Socket=const_cast<USkeletalMeshSocket*>(PresentationMesh->FindSocket(Name));
    if (!Socket) {Socket=NewObject<USkeletalMeshSocket>(PresentationMesh);Socket->SocketName=Name;PresentationMesh->AddSocket(Socket);}
    Socket->BoneName=Bone;Socket->RelativeLocation=Transform.GetTranslation();Socket->RelativeRotation=Transform.Rotator();Socket->RelativeScale=FVector::OneVector;
    PresentationMesh->RebuildSocketMap();PresentationMesh->MarkPackageDirty();return true;
#else
    return false;
#endif
}
#if WITH_EDITOR
namespace
{
TArray<FVector> PresentationVerticesAtIdle(USkeletalMesh* WeaponMesh,FMeshDescription* Description,UAnimSequence* Animation)
{
        FSkeletalMeshAttributes Attributes(*Description);
        const auto Positions=Attributes.GetVertexPositions();
        TArray<FVector> Deformed;Deformed.SetNum(Description->Vertices().GetArraySize());
        const auto& Ref=WeaponMesh->GetRefSkeleton();
        TArray<FTransform> Bind=Ref.GetRefBonePose(),Current=Bind;
        for (int32 I=0;I<Bind.Num();++I)
        {
            if (Animation && Animation->GetSkeleton())
            {
                const int32 Index=Animation->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Ref.GetBoneName(I));
                if (Index!=INDEX_NONE) Animation->GetBoneTransform(Current[I],FSkeletonPoseBoneIndex(Index),FAnimExtractContext(0.0),false);
            }
            const int32 Parent=Ref.GetParentIndex(I);
            if (Parent!=INDEX_NONE) {Bind[I]*=Bind[Parent];Current[I]*=Current[Parent];}
        }
        TArray<int32> Bones;Bones.Init(INDEX_NONE,Attributes.Bones().GetArraySize());
        for (const FBoneID ID : Attributes.Bones().GetElementIDs()) Bones[ID.GetValue()]=Ref.FindBoneIndex(Attributes.GetBoneNames()[ID]);
        const auto Weights=Attributes.GetVertexSkinWeights();
        for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
        {
            const FVector Position(Positions[Vertex]);FVector Point=FVector::ZeroVector;
            if (Animation) for (const auto Weight : Weights.Get(Vertex))
            {
                const int32 Bone=Bones[Weight.GetBoneIndex()];
                if (Bone!=INDEX_NONE) Point+=Current[Bone].TransformPosition(Bind[Bone].InverseTransformPosition(Position))*Weight.GetWeight();
            }
            Deformed[Vertex.GetValue()]=Animation ? Point : Position;
        }
        return Deformed;
}
}
#endif
FVector AERCharacter::GetWeaponSurfaceContact(USkeletalMesh* WeaponMesh, FVector Start, FVector End, UAnimSequence* Animation)
{
#if WITH_EDITOR
    if (WeaponMesh) if (FMeshDescription* Description=WeaponMesh->GetMeshDescription(0))
    {
        const auto Deformed=PresentationVerticesAtIdle(WeaponMesh,Description,Animation);
        FVector Best(UE_BIG_NUMBER);float Distance=UE_BIG_NUMBER;
        for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
        {
            const auto Vertices=Description->GetTriangleVertices(Triangle);
            FVector Point,Normal;
            if (FMath::SegmentTriangleIntersection(Start,End,Deformed[Vertices[0].GetValue()],Deformed[Vertices[1].GetValue()],Deformed[Vertices[2].GetValue()],Point,Normal) &&
                FVector::DistSquared(Start,Point)<Distance)
            {Best=Point;Distance=FVector::DistSquared(Start,Point);}
        }
        return Best;
    }
#endif
    return FVector(UE_BIG_NUMBER);
}
FVector AERCharacter::GetWeaponMuzzleFromGeometry(USkeletalMesh* WeaponMesh,UAnimSequence* Animation)
{
#if WITH_EDITOR
    if (!WeaponMesh) return FVector::ZeroVector;
    FMeshDescription* Description = WeaponMesh->GetMeshDescription(0);
    if (!Description || Description->Vertices().Num() == 0) return FVector::ZeroVector;
    const auto Positions=PresentationVerticesAtIdle(WeaponMesh,Description,Animation);
    double Front=-DBL_MAX;
    for (const FVertexID Vertex : Description->Vertices().GetElementIDs()) Front=FMath::Max(Front,Positions[Vertex.GetValue()].X);
    FBox TipBounds(ForceInit);
    // Include the evaluated ready pose when measuring visual framing.
    for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
        if (Positions[Vertex.GetValue()].X>=Front-.4) TipBounds+=Positions[Vertex.GetValue()];
    return FVector(Front, TipBounds.GetCenter().Y, TipBounds.GetCenter().Z);
#else
    return FVector::ZeroVector;
#endif
}
bool AERCharacter::AssignWeaponMaterial(USkeletalMesh* WeaponMesh, UMaterialInterface* Material)
{
#if WITH_EDITOR
    if (!WeaponMesh || !Material || !WeaponMesh->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Weapons/"))) return false;
    WeaponMesh->Modify();
    TArray<FSkeletalMaterial> Materials = WeaponMesh->GetMaterials();
    for (auto& Slot : Materials) Slot.MaterialInterface = Material;
    WeaponMesh->SetMaterials(Materials);
    WeaponMesh->PostEditChange();
    WeaponMesh->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}
bool AERCharacter::AssignWeaponMaterials(USkeletalMesh* WeaponMesh, const TArray<UMaterialInterface*>& Interfaces)
{
#if WITH_EDITOR
    if (!WeaponMesh || !WeaponMesh->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Weapons/Sketchfab/")) || Interfaces.Num()!=WeaponMesh->GetMaterials().Num()) return false;
    TArray<FSkeletalMaterial> Materials=WeaponMesh->GetMaterials();
    for (int32 Index=0;Index<Materials.Num();++Index)
    {
        if (!Interfaces[Index]) return false;
        Materials[Index].MaterialInterface=Interfaces[Index];
    }
    WeaponMesh->Modify();WeaponMesh->SetMaterials(Materials);WeaponMesh->PostEditChange();WeaponMesh->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}

bool AERCharacter::SetMercenaryAnimationSkeleton(UAnimSequence* Animation, USkeletalMesh* TargetMesh)
{
#if WITH_EDITOR
    if (!Animation || !TargetMesh || !TargetMesh->GetSkeleton() ||
        !(Animation->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Characters/")) ||
          Animation->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Weapons/Sketchfab/")))) return false;
    Animation->Modify();
    if (Animation->GetSkeleton() != TargetMesh->GetSkeleton() && !Animation->ReplaceSkeleton(TargetMesh->GetSkeleton(), false)) return false;
    Animation->SetPreviewMesh(TargetMesh);
    Animation->PostEditChange();
    return true;
#else
    return false;
#endif
}

UERHealthComponent::UERHealthComponent() { SetIsReplicatedByDefault(true); }
bool UERHealthComponent::IsProtected() const
{
    if(const auto* Character=Cast<AERCharacter>(GetOwner());Character && !Character->bNetworkReady)return true;
    const auto* State = GetWorld()->GetGameState<AERGameState>();
    return State && State->GetServerWorldTimeSeconds() < ProtectionEndServerTime;
}
float UERHealthComponent::ReceiveDamage(float Amount, AController* Killer, float ArmorRatio, const FERMessageDamage& Facts, const FHitResult* BulletHit)
{
    auto* Character = Cast<AERCharacter>(GetOwner());
    auto* Mode = GetWorld()->GetAuthGameMode<AERGameMode>();
    if (!Character || !Character->bNetworkReady || !Character->HasAuthority() || !Mode || !Mode->IsMatchInProgress() || bDead || IsProtected() || Amount <= 0) return 0;
    if(Killer)
    {
        if(Killer==Character->GetController())Amount*=ERServer::Get(GetWorld(),"Damage.Self");
        else
        {
            const auto* VictimState=Character->GetPlayerState<AERPlayerState>();
            const auto* AttackerState=Killer->GetPlayerState<AERPlayerState>();
            if(VictimState && AttackerState && VictimState->Team==AttackerState->Team)Amount*=ERServer::Get(GetWorld(),"Damage.Friendly");
        }
    }
    const float ArmorBefore=Armor;
    Amount = ERWeaponBallistics::AbsorbArmor(Amount, ArmorRatio, Armor);
    const float Before = Health;
    Health = FMath::Clamp(Health - Amount, 0.f, ERServer::Get(GetWorld(),"Health.Max"));
    const float HealthLost=Before-Health, ArmorLost=ArmorBefore-Armor;
    if (BulletHit && (HealthLost>0 || ArmorLost>0)) Character->Wounds->Record(Character->GetMesh(),*BulletHit,HealthLost<=0 && ArmorLost>0);
    if ((HealthLost>0 || ArmorLost>0) && Killer && Killer!=Character->Controller)
        if (auto* Shooter=Cast<AERPlayerController>(Killer)) Shooter->ClientInflictedDamage(HealthLost,ArmorLost);
    UE_LOG(LogTemp, Display, TEXT("ER_DAMAGE target=%s hp=%.0f"), *Character->GetName(), Health);
    GetWorld()->GetSubsystem<UERGameplayMessages>()->Damage(Character, Killer, Facts, Health <= 0);
    if (Health <= 0)
    {
        bDead = true;
        if (Character->Weapon) Character->Weapon->Cancel();
        OnRep_Dead();
        Mode->RecordDeath(Character, Killer, &Facts);
    }
    return Before - Health;
}
void UERHealthComponent::OnRep_Dead() { if (bDead) if (auto* Character = Cast<AERCharacter>(GetOwner())) Character->ApplyDeathVisuals(); }
void UERHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UERHealthComponent, bHelmet); DOREPLIFETIME(UERHealthComponent, Health); DOREPLIFETIME(UERHealthComponent, bDead); DOREPLIFETIME(UERHealthComponent, ProtectionEndServerTime);
    DOREPLIFETIME(UERHealthComponent, Armor);
}
AERCharacter::AERCharacter(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UERMovement>(ACharacter::CharacterMovementComponentName).SetDefaultSubobjectClass<UERPostureMesh>(ACharacter::MeshComponentName))
{
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> GrayBase(TEXT("/Engine/EngineDebugMaterials/LevelColorationLitMaterial.LevelColorationLitMaterial"));
    ProtectionBaseMaterial = GrayBase.Object;
    bReplicates = true;
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(34, 92.5f);
    GetCharacterMovement()->MaxWalkSpeed = 1500.f / 3.6f;
    GetCharacterMovement()->JumpZVelocity = 460;
    GetCharacterMovement()->AirControl = 0.2f;
    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
    GetCharacterMovement()->MaxWalkSpeedCrouched = 150;
    BaseEyeHeight = 77.5f; CrouchedEyeHeight = 50;
    bUseControllerRotationYaw = true;
    Wounds = CreateDefaultSubobject<UERWounds>(TEXT("Wounds"));
    Health = CreateDefaultSubobject<UERHealthComponent>(TEXT("Health"));
    Footsteps = CreateDefaultSubobject<UERFootsteps>(TEXT("Footsteps"));
    ProneHitVolume = CreateDefaultSubobject<UCapsuleComponent>(TEXT("ProneHitVolume"));
    ProneHitVolume->SetupAttachment(GetCapsuleComponent());
    ProneHitVolume->InitCapsuleSize(22,105);
    ProneHitVolume->SetRelativeLocation(FVector(30,0,-9));
    ProneHitVolume->SetRelativeRotation(FRotator(90,0,0));
    ProneHitVolume->SetCollisionObjectType(ECC_Pawn);
    ProneHitVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    ProneHitVolume->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Block);
    ProneHitVolume->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    ProneHitVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ProneHitVolume->SetGenerateOverlapEvents(false);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(GetCapsuleComponent());
    Camera->SetRelativeLocation(FVector(0, 0, 77.5f));
    Camera->bUsePawnControlRotation = true;
    Camera->FieldOfView = 90;
    GetMesh()->SetRelativeLocation(FVector(0, 0, -92.5f));
    GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
    GetMesh()->SetOwnerNoSee(true);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FirstPersonBody = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonBody"));
    FirstPersonBody->SetupAttachment(Camera);
    FirstPersonBody->SetRelativeLocation(FVector(-10, 0, -152));
    FirstPersonBody->SetRelativeRotation(FRotator(0, -90, 0));
    FirstPersonBody->SetOnlyOwnerSee(true);
    FirstPersonBody->SetCastShadow(false);
    FirstPersonBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FirstPersonRifle = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonRifle"));
    FirstPersonRifle->SetupAttachment(FirstPersonBody, TEXT("HandGrip_R"));
    FirstPersonRifle->SetOnlyOwnerSee(true);
    FirstPersonRifle->SetCastShadow(false);
    FirstPersonRifle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WorldRifle = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WorldRifle"));
    WorldRifle->SetupAttachment(GetMesh(), TEXT("HandGrip_R"));
    FirstPersonSight = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonSight"));
    FirstPersonSight->SetupAttachment(FirstPersonRifle); FirstPersonSight->SetOnlyOwnerSee(true);
    WorldSight = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WorldSight"));
    WorldSight->SetupAttachment(WorldRifle); WorldSight->SetOwnerNoSee(true);
    for (auto* Sight : {FirstPersonSight.Get(), WorldSight.Get()})
    { Sight->SetCollisionEnabled(ECollisionEnabled::NoCollision); Sight->SetCastShadow(false); }
    WorldRifle->SetOwnerNoSee(true);
    WorldRifle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FirstPersonMuzzleFlash = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FirstPersonMuzzleFlash"));
    FirstPersonMuzzleFlash->SetupAttachment(FirstPersonRifle, TEXT("Muzzle"));
    FirstPersonMuzzleFlash->SetOnlyOwnerSee(true);
    WorldMuzzleFlash = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WorldMuzzleFlash"));
    WorldMuzzleFlash->SetupAttachment(WorldRifle, TEXT("Muzzle"));
    WorldMuzzleFlash->SetOwnerNoSee(true);
    for (auto* Flash : {FirstPersonMuzzleFlash.Get(), WorldMuzzleFlash.Get()})
    {
        Flash->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Flash->SetCastShadow(false);
        Flash->SetVisibility(false);
    }
    for (const TCHAR* Name : {TEXT("FirstPersonFlashJetA"), TEXT("FirstPersonFlashJetB"),
        TEXT("WorldFlashJetA"), TEXT("WorldFlashJetB")})
    {
        const bool bFirstPerson = MuzzleFlashJets.Num() < 2;
        auto* Jet = CreateDefaultSubobject<UStaticMeshComponent>(FName(Name));
        Jet->SetupAttachment(bFirstPerson ? FirstPersonMuzzleFlash.Get() : WorldMuzzleFlash.Get());
        Jet->SetAbsolute(false, false, true);
        Jet->SetOnlyOwnerSee(bFirstPerson);
        Jet->SetOwnerNoSee(!bFirstPerson);
        Jet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Jet->SetCastShadow(false);
        Jet->SetReceivesDecals(false);
        Jet->SetVisibility(false);
        MuzzleFlashJets.Add(Jet);
    }
    MuzzleLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MuzzleLight"));
    MuzzleLight->SetupAttachment(GetCapsuleComponent());
    MuzzleLight->SetMobility(EComponentMobility::Movable);
    MuzzleLight->SetCastShadows(false);
    MuzzleLight->SetIndirectLightingIntensity(0.f);
    MuzzleLight->SetVolumetricScatteringIntensity(0.f);
    MuzzleLight->SetUseInverseSquaredFalloff(false);
    MuzzleLight->SetLightFalloffExponent(5.f);
    MuzzleLight->SetAttenuationRadius(230.f);
    MuzzleLight->SetIntensity(0.f);
    MuzzleLight->SetVisibility(false);
    TeamMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TeamMarker"));
    TeamMarker->SetupAttachment(GetCapsuleComponent());
    TeamMarker->SetRelativeLocation(FVector(0,0,110));
    TeamMarker->SetRelativeScale3D(FVector(0.18f));
    TeamMarker->SetOwnerNoSee(true);
    TeamMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WeaponClass = AERWeapon::StaticClass();
}
void AERCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
    Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
    Camera->SetRelativeLocation(FVector(0,0,BaseEyeHeight));
}
void AERCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
    Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
    Camera->SetRelativeLocation(FVector(0,0,BaseEyeHeight));
}
void AERCharacter::BeginPlay()
{
    Cast<UERMovement>(GetCharacterMovement())->ApplyServerSettings();
    if(HasAuthority()) SpawnFacingYaw=GetActorRotation().Yaw;
    Super::BeginPlay();
    FirstPersonBodyBaseLocation = FirstPersonBody->GetRelativeLocation();
    // Projected wounds and world bullet marks must never paint held weapons.
    for(UPrimitiveComponent* Part : {static_cast<UPrimitiveComponent*>(FirstPersonRifle.Get()),
        static_cast<UPrimitiveComponent*>(WorldRifle.Get()),static_cast<UPrimitiveComponent*>(FirstPersonSight.Get()),
        static_cast<UPrimitiveComponent*>(WorldSight.Get()),static_cast<UPrimitiveComponent*>(FirstPersonMuzzleFlash.Get()),
        static_cast<UPrimitiveComponent*>(WorldMuzzleFlash.Get())}) Part->SetReceivesDecals(false);
    FirstPersonBodyBaseRotation = FirstPersonBody->GetRelativeRotation();
    UERShotEffects::Prewarm(GetWorld(), TracerSystem, Camera->GetComponentLocation());
    // Compress only the viewmodel's rendered depth, preserving its framing and
    // socket transforms. Keep it inside the capsule clearance near walls.
    // Apply at runtime too: existing Blueprint component defaults are serialized.
    Camera->SetEnableFirstPersonScale(true);
    Camera->SetFirstPersonScale(.2f);
    Camera->SetEnableFirstPersonFieldOfView(true);
    Camera->SetFirstPersonFieldOfView(90.f);
    FirstPersonSight->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    for (UPrimitiveComponent* Component : {static_cast<UPrimitiveComponent*>(FirstPersonBody.Get()),
        static_cast<UPrimitiveComponent*>(FirstPersonRifle.Get()),
        static_cast<UPrimitiveComponent*>(FirstPersonMuzzleFlash.Get())})
        Component->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    if (FApp::CanEverRender() && GetNetMode() != NM_DedicatedServer && EROutdoorBackdrop::IsOutdoorMap(*GetWorld()))
        for (auto* Component : {FirstPersonBody.Get(), FirstPersonRifle.Get(), FirstPersonSight.Get()})
            Component->SetLightingChannels(true, true, false);
    // Existing Blueprint instances may still serialize the old camera attachment.
    // Align the rifle's GripPoint origin to the mannequin's matching grip socket.
    FirstPersonRifle->AttachToComponent(FirstPersonBody, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("HandGrip_R"));
    WorldRifle->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("HandGrip_R"));
    FirstPersonBody->HideBoneByName(TEXT("head"), EPhysBodyOp::PBO_None);
    if (!FirstPersonVisibleMaterialSlots.IsEmpty()) if (const auto* BodyMesh = FirstPersonBody->GetSkeletalMeshAsset())
        for (int32 LOD = 0; LOD < BodyMesh->GetLODNum(); ++LOD)
            for (int32 Slot = 0; Slot < BodyMesh->GetMaterials().Num(); ++Slot)
                FirstPersonBody->ShowMaterialSection(Slot, INDEX_NONE, FirstPersonVisibleMaterialSlots.Contains(Slot), LOD);
    for (auto* Flash : {FirstPersonMuzzleFlash.Get(), WorldMuzzleFlash.Get()})
    {
        Flash->SetStaticMesh(MuzzleFlashMesh);
        Flash->SetMaterial(0, MuzzleFlashMaterial);
        Flash->SetVisibility(false);
    }
    for (const auto& Jet : MuzzleFlashJets)
    {
        Jet->SetStaticMesh(MuzzleFlashMesh);
        Jet->SetMaterial(0, MuzzleFlashMaterial);
        if (Jet->GetAttachParent() == FirstPersonMuzzleFlash)
            Jet->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    }
    if (HasAuthority())
    {
        // Reuse anatomical ragdoll bodies for explicit shotgun queries only.
        // Other weapon/world/pawn collision channels keep their existing behaviour.
        GetMesh()->SetCollisionResponseToAllChannels(ECR_Ignore);
        GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        GetMesh()->SetGenerateOverlapEvents(false);
        GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Health->ProtectionEndServerTime = GetWorld()->GetTimeSeconds() + ERServer::Get(GetWorld(),"Health.Protection");
        AimPunchTuning.BasePitch*=ERServer::Get(GetWorld(),"Damage.Punch");
        AimPunchTuning.PitchPerHP*=ERServer::Get(GetWorld(),"Damage.Punch");
        AimPunchTuning.HalfLife=ERServer::Get(GetWorld(),"Damage.PunchRecovery");
        // Direct map startup can assign the loadout before this pawn's BeginPlay.
        // Keep that weapon and its FPS pose instead of replacing it with the fallback.
        const auto* SpawnMode=GetWorld()->GetAuthGameMode<AERGameMode>();
        if (!Weapon && (!SpawnMode || !SpawnMode->IsSpawningPresetPawn()))
        {
            FActorSpawnParameters Params; Params.Owner = this; Params.Instigator = this;
            Weapon = GetWorld()->SpawnActor<AERWeapon>(WeaponClass, GetActorTransform(), Params);
            if (Weapon) Weapon->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        }
    }
    // Enforce this at runtime: existing Blueprints serialize the old visible marker.
    TeamMarker->SetHiddenInGame(true);
    TeamMarker->SetVisibility(false);
    TeamMarker->SetCastShadow(false);
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("ERViewmodelWallTest")))
    {
        FTimerHandle ProbeTimer;
        GetWorldTimerManager().SetTimer(ProbeTimer, [this]()
        {
            if (!IsLocallyControlled() || !Controller) return;
            FHitResult Wall;
            const FVector Eye = Camera->GetComponentLocation();
            bool bFoundWall = false;
            for (const FVector Direction : {FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector, -FVector::RightVector})
            {
                FHitResult Candidate;
                if (GetWorld()->LineTraceSingleByObjectType(Candidate, Eye, Eye + Direction * 20000.f,
                    FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(ViewmodelWallTest), false, this)) &&
                    FMath::Abs(Candidate.ImpactNormal.Z) < .2f && (!bFoundWall || Candidate.Distance < Wall.Distance))
                { Wall = Candidate; bFoundWall = true; }
            }
            if (!bFoundWall)
            {
                UE_LOG(LogTemp, Warning, TEXT("ER_VIEWMODEL_WALL_TEST no_wall_found"));
                return;
            }
            SetActorLocation(Wall.ImpactPoint + Wall.ImpactNormal * 36.f - FVector(0, 0, Camera->GetRelativeLocation().Z));
            Controller->SetControlRotation((-Wall.ImpactNormal).Rotation());
            FTimerHandle CaptureTimer;
            GetWorldTimerManager().SetTimer(CaptureTimer, [this, Wall]()
            {
                FMinimalViewInfo View;
                CalcCamera(0.f, View);
                const FVector Muzzle = GetVisualMuzzle(true);
                const FVector RenderedMuzzle = View.TransformWorldToFirstPerson(Muzzle, false);
                const float Clearance = FVector::DotProduct(RenderedMuzzle - Wall.ImpactPoint, Wall.ImpactNormal);
                UE_LOG(LogTemp, Display, TEXT("ER_VIEWMODEL_WALL_TEST clearance=%.2f scale=%.2f body=%d rifle=%d flash=%d result=%s"),
                    Clearance, View.FirstPersonScale, int32(FirstPersonBody->FirstPersonPrimitiveType),
                    int32(FirstPersonRifle->FirstPersonPrimitiveType), int32(FirstPersonMuzzleFlash->FirstPersonPrimitiveType),
                    Clearance > 0.f ? TEXT("PASS") : TEXT("FAIL"));
                if (auto* PC = Cast<APlayerController>(Controller)) PC->ConsoleCommand(TEXT("Shot"));
            }, 1.f, false);
        }, 2.f, false);
    }
#endif
}
const FERTeamAppearance& AERCharacter::GetTeamAppearance() const
{
    const auto* State = GetPlayerState<AERPlayerState>();
    const bool Alpha = !State || State->Team != EERTeam::Omega;
    return ERSkins::Select(Alpha ? AlphaSkins : OmegaSkins, Alpha ? AlphaAppearance : OmegaAppearance, State ? State->ActiveSkin : 0);
}

void AERCharacter::FaceRotation(FRotator NewControlRotation, float DeltaTime)
{
    const auto* Movement = Cast<UERMovement>(GetCharacterMovement());
    Super::FaceRotation(Movement ? Movement->ConstrainProneRotation(FRotator(0,NewControlRotation.Yaw,0)) : NewControlRotation, DeltaTime);
}
void AERCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (BlastRinging) UpdateBlastRinging();
    Cast<UERMovement>(GetCharacterMovement())->ApplyPosturePresentation();
    if(HasAuthority())for(auto Item:Inventory)if(Item)Item->UpdateAccuracyRecovery();
    if(HasAuthority())UpdateFireAcknowledgement();
    UpdateWeaponPresentation();
    UpdatePredictedFire();
    DamageViewKick = FMath::VInterpTo(DamageViewKick, FVector::ZeroVector, DeltaTime, 18.f);
    CosmeticDamagePitch = FMath::VInterpTo(CosmeticDamagePitch, FVector::ZeroVector, DeltaTime, 18.f);
    DamageVisuals.RemoveAll([this](const FERDamageVisual& V){return GetWorld()->GetTimeSeconds()-V.LocalTime>.7;});
    DeathViewDrop = FMath::FInterpTo(DeathViewDrop, Health->bDead ? -55.f : 0.f, DeltaTime, 5.f);
#if !UE_BUILD_SHIPPING
    if (!bFPSPoseAudited && IsLocallyControlled() && GetWorld()->GetTimeSeconds() > 2 && FParse::Param(FCommandLine::Get(), TEXT("ERFPSAudit")))
    {
        bFPSPoseAudited = true;
        const FTransform View = Camera->GetComponentTransform();
        for (const FName Name : {FName(TEXT("HandGrip_R")), FName(TEXT("HandGrip_L")), FName(TEXT("upperarm_l")), FName(TEXT("lowerarm_l")), FName(TEXT("hand_l"))})
        {
            const FTransform Pose = FirstPersonBody->GetSocketTransform(Name);
            const FVector P = View.InverseTransformPositionNoScale(Pose.GetLocation());
            UE_LOG(LogTemp, Display, TEXT("ER_FPS_POSE mesh=%s bone=%s camera=%.4f,%.4f,%.4f scale=%s"), *FirstPersonBody->GetSkeletalMeshAsset()->GetName(), *Name.ToString(), P.X, P.Y, P.Z, *Pose.GetScale3D().ToString());
        }
        const FTransform Rifle = FirstPersonRifle->GetComponentTransform().GetRelativeTransform(View);
        UE_LOG(LogTemp, Display, TEXT("ER_FPS_RIFLE camera=%s"), *Rifle.ToString());
    }
#endif
    const auto* Movement = Cast<UERMovement>(GetCharacterMovement());
    const bool bStowed = IsWeaponStowed();
    const bool bLowerForApproach = bStowed && !Movement->IsTraversing() && Movement->ShouldLowerWeaponForLedge();
    const bool bHideStowed = bStowed && !bLowerForApproach;
    LedgeWeaponLowerBlend = FMath::FInterpConstantTo(LedgeWeaponLowerBlend,
        bLowerForApproach ? 1.f : 0.f, DeltaTime, 8.f);
    if (HasAuthority() && (Movement->bHighClimb || bStowed) && Weapon)
    {
        if(Weapon->bCookingGrenade) Weapon->Cancel();
        else Weapon->StopFire();
    }
    const bool bGrenade=AppliedWeaponData && AERWeapon::IsHandGrenade(AppliedWeaponData->Payload);
    const bool bReadyToThrow=bGrenade && Weapon && Weapon->bCookingGrenade && !Health->bDead;
    GrenadeThrowPoseBlend=FMath::FInterpTo(GrenadeThrowPoseBlend,bReadyToThrow?1.f:0.f,DeltaTime,14.f);
    const double LocalNow=GetWorld()->GetTimeSeconds();
    const bool bHideGrenade=(Weapon && AppliedWeaponData && AppliedWeaponData->Payload==EERPayload::Claymore && Weapon->MagazineAmmo<=0) || (bGrenade && ((Weapon && Weapon->IsGrenadeHandHidden()) ||
        (IsLocallyControlled() && ((bLocalGrenadeInputHeld && LocalNow<LocalGrenadeCookDeadline) || LocalNow<LocalGrenadeHideUntil))));
    if(Weapon)Weapon->UpdateScopeMotion();
    const bool bOptic = !bStowed && Weapon && Weapon->HasOpticalScope() && Weapon->ScopeLevel;
    const bool bIronSights = Weapon && Weapon->ScopeLevel && Weapon->Data && Weapon->Data->ScopeMode == 1;
    const bool bRaise=bIronSights && !Health->bDead && !(Movement->bHighClimb || bStowed);
    IronAimBlend=FMath::FInterpConstantTo(IronAimBlend,bRaise?1.f:0.f,FMath::Min(DeltaTime,.05f),5.f);
    const float AimBlend=GetIronAimBlend();
    FirstPersonBody->SetVisibility(!Health->bDead && !bOptic && AimBlend<.01f);
    if (AppliedWeaponData)
    {
        const FVector Base=FirstPersonBodyBaseLocation+AppliedWeaponData->ViewOffset;
        FirstPersonBody->SetRelativeLocation(Base);
        FirstPersonBody->SetRelativeRotation(FirstPersonBodyBaseRotation);
        if (AimBlend>0 && AppliedWeaponData->ScopeMode==1)
        {
            // Shift the complete fitted rig; weapon and both palm contacts stay together.
            FirstPersonBody->SetRelativeRotation((FQuat(FRotator(AppliedWeaponData->IronSightPitch*AimBlend,0,0))*
                FirstPersonBodyBaseRotation.Quaternion()).Rotator());
            FirstPersonBody->UpdateComponentToWorld();
            const FVector Sight=Camera->GetComponentTransform().InverseTransformPosition(GetIronSightLocation());
            // Keep the authored aiming anchor on the optical centre at its weapon-specific eye distance.
            FirstPersonBody->SetRelativeLocation(Base+FVector(AppliedWeaponData->IronSightEyeDistance-Sight.X,-Sight.Y,-Sight.Z-AppliedWeaponData->IronSightDrop)*AimBlend);
        }
        if (LedgeWeaponLowerBlend > 0.f)
        {
            const float Lower = LedgeWeaponLowerBlend;
            FirstPersonBody->AddRelativeLocation(FVector(0,0,-14.f*Lower));
            FirstPersonBody->SetRelativeRotation((FQuat(FRotator(-25.f*Lower,0,0))*
                FirstPersonBody->GetRelativeRotation().Quaternion()).Rotator());
        }
    }
    FirstPersonRifle->SetVisibility(Weapon && !bOptic && !Health->bDead && !(Movement->bHighClimb || bHideStowed) && !bHideGrenade);
    WorldRifle->SetVisibility(Weapon && !bStowed && !Health->bDead && (bReadyToThrow || !(Weapon && Weapon->IsGrenadeHandHidden())));
    FirstPersonSight->SetVisibility(!bOptic && !Health->bDead && !(Movement->bHighClimb || bHideStowed) && AppliedWeaponData && AppliedWeaponData->SightMesh);
    WorldSight->SetVisibility(!Health->bDead && !bStowed && AppliedWeaponData && AppliedWeaponData->SightMesh);
    UpdateWeaponLight(bStowed || Movement->bHighClimb);
    const double FlashRemaining = MuzzleFlashUntil - GetWorld()->GetTimeSeconds();
    const bool bFlash = FlashRemaining > 0 && !Health->bDead && bNetworkReady && !(Movement->bHighClimb || bStowed);
    const bool bViewFlash = bFlash && !bOptic;
    if (FirstPersonMuzzleFlash->GetVisibleFlag() != bViewFlash)
        FirstPersonMuzzleFlash->SetVisibility(bViewFlash, true);
    if (WorldMuzzleFlash->GetVisibleFlag() != bFlash)
        WorldMuzzleFlash->SetVisibility(bFlash, true);
    if (MuzzleLight->IsVisible())
    {
        if (bFlash)
            MuzzleLight->SetIntensity(MuzzleLightPeak * FMath::Square(FMath::Clamp(float(FlashRemaining) / MuzzleFlashDuration, 0.f, 1.f)));
        else
            MuzzleLight->SetVisibility(false);
    }
    CameraKick = Health->bDead || (Movement->bHighClimb || bStowed) ? 0.f : FMath::FInterpTo(CameraKick, 0.f, DeltaTime, Weapon && Weapon->GetScopeRecoilMultiplier()<1.f ? 44.f : 22.f);
    const bool bHideLeft = Movement->bHighClimb || bHideStowed || (AppliedWeaponData && AppliedWeaponData->bOneHanded && !(Weapon && Weapon->bReloading));
    if (bLeftArmHidden != bHideLeft)
    {
        bLeftArmHidden = bHideLeft;
        if (bLeftArmHidden) FirstPersonBody->HideBoneByName(TEXT("upperarm_l"), EPhysBodyOp::PBO_None);
        else FirstPersonBody->UnHideBoneByName(TEXT("upperarm_l"));
    }
    const bool bHideRight=bHideGrenade || bHideStowed;
    if(bRightArmHidden!=bHideRight)
    {
        bRightArmHidden=bHideRight;
        if(bHideRight) FirstPersonBody->HideBoneByName(TEXT("upperarm_r"),EPhysBodyOp::PBO_None);
        else FirstPersonBody->UnHideBoneByName(TEXT("upperarm_r"));
    }
    // Presentation only: preserve the physical capsule and server eye/muzzle.
    if (IsLocallyControlled())
    {
        const float Speed = GetVelocity().Size2D();
        const bool bStepping = Movement->IsMovingOnGround() && !Movement->IsTraversing() && !Health->bDead && Speed > 5;
        FVector Target = FVector::ZeroVector;
        if (bStepping)
        {
            StepPhase = FMath::Fmod(StepPhase + DeltaTime * Speed / 140.f * 2.f * PI, 4.f * PI);
            const float Strength = FMath::Clamp(Speed / (1500.f / 3.6f), 0.f, 1.f) * (bIsCrouched ? .5f : 1.f);
            Target = FVector(0, .25f * FMath::Sin(StepPhase*.5f), .70f * FMath::Sin(StepPhase)) * Strength;
        }
        StepOffset = FMath::VInterpTo(StepOffset, Target, DeltaTime, 14.f);
        if (!bStepping && StepOffset.IsNearlyZero(.001f)) { StepOffset = FVector::ZeroVector; StepPhase = 0; }
    }
    if (const auto* State = GetPlayerState<AERPlayerState>())
    {
        if (State->Team != EERTeam::None && (AppliedBodyTeam != static_cast<uint8>(State->Team) || AppliedSkin != State->ActiveSkin))
        {
            RestoreProtectionMaterials();
            const auto& Appearance = GetTeamAppearance();
            const auto& Materials = State->Team == EERTeam::Alpha ? AlphaBodyMaterials : OmegaBodyMaterials;
            if (Appearance.Mesh)
            {
                Appearance.Apply(GetMesh());
                Wounds->SetBody(GetMesh());
                WorldRifle->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, AppliedWeaponData && AppliedWeaponData->bWorldReferencePose ? TEXT("ER_Palm_R"):TEXT("HandGrip_R"));
                UE_LOG(LogTemp, Display, TEXT("ER_TEAM_APPEARANCE team=%s mesh=%s anim=%s hidden=%d bot=%d"),
                    State->Team == EERTeam::Alpha ? TEXT("Alpha") : TEXT("Omega"), *Appearance.Mesh->GetPathName(),
                    *GetPathNameSafe(Appearance.AnimationClass.Get()), Appearance.HiddenMaterialSlots.Num(), State->IsABot());
            }
            else for (int32 Slot = 0; Slot < Materials.Num(); ++Slot)
                if (Materials[Slot]) GetMesh()->SetMaterial(Slot, Materials[Slot]);
            if (Appearance.FirstPersonMesh)
            {
                // These assets share the fitted FPS bind rig, grips and weapon poses.
                // Preserve the existing component frame and the weapon-specific offset.
                FirstPersonBody->SetSkeletalMeshAsset(Appearance.FirstPersonMesh);
                FirstPersonBody->SetRelativeScale3D(FVector::OneVector);
                FirstPersonBody->SetRelativeLocation(FirstPersonBodyBaseLocation +
                    (AppliedWeaponData ? AppliedWeaponData->ViewOffset : FVector::ZeroVector));
                FirstPersonBody->SetAnimationMode(EAnimationMode::AnimationSingleNode);
                FirstPersonBody->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
                FirstPersonBody->SetForcedLOD(1);
                FirstPersonBody->EmptyOverrideMaterials();
                for (int32 LOD = 0; LOD < Appearance.FirstPersonMesh->GetLODNum(); ++LOD)
                    FirstPersonBody->ShowAllMaterialSections(LOD);
                for (int32 Slot = 0; Slot < Appearance.Materials.Num(); ++Slot)
                    if (Appearance.Materials[Slot]) FirstPersonBody->SetMaterial(Slot, Appearance.Materials[Slot]);
                FirstPersonBody->HideBoneByName(TEXT("head"), EPhysBodyOp::PBO_None);
                if (bLeftArmHidden) FirstPersonBody->HideBoneByName(TEXT("upperarm_l"), EPhysBodyOp::PBO_None);
                if (AppliedWeaponData && AppliedWeaponData->IdleAnimation)
                    FirstPersonBody->PlayAnimation(AppliedWeaponData->IdleAnimation, true);
                UE_LOG(LogTemp, Display, TEXT("ER_FPS_PLAYER_FOREARMS team=%s mesh=%s frame=%s"),
                    State->Team == EERTeam::Alpha ? TEXT("Alpha") : TEXT("Omega"),
                    *Appearance.FirstPersonMesh->GetPathName(), *FirstPersonBody->GetRelativeLocation().ToString());
            }
            else
            {
                const auto& FPSMaterials = State->Team == EERTeam::Alpha ? AlphaFirstPersonMaterials : OmegaFirstPersonMaterials;
                for (int32 Slot = 0; Slot < FPSMaterials.Num(); ++Slot)
                    if (FPSMaterials[Slot]) FirstPersonBody->SetMaterial(Slot, FPSMaterials[Slot]);
            }
            AppliedBodyTeam = static_cast<uint8>(State->Team);
            AppliedSkin = State->ActiveSkin;
            bProtectionMaterialsDirty = true;
            if (!Appearance.Mesh && !Materials.IsEmpty()) UE_LOG(LogTemp, Display, TEXT("ER_TARGET_COLORS team=%s slots=%d"), State->Team == EERTeam::Alpha ? TEXT("Alpha") : TEXT("Omega"), Materials.Num());
        }
    }
    UpdateProtectionPresentation();
}
UMaterialInterface* AERCharacter::GetUnprotectedMaterial(UMeshComponent* SurfaceMesh, int32 Slot) const
{
    for (const auto& State : ProtectionSurfaces)
        if (State.Mesh == SurfaceMesh && State.Originals.IsValidIndex(Slot)) return State.Originals[Slot];
    return SurfaceMesh->GetMaterial(Slot);
}
void AERCharacter::RestoreProtectionMaterials()
{
    for (const auto& State : ProtectionSurfaces)
        if (IsValid(State.Mesh))
            for (int32 Slot=0; Slot<State.Originals.Num(); ++Slot) State.Mesh->SetMaterial(Slot,State.Originals[Slot]);
    ProtectionSurfaces.Reset();
}
void AERCharacter::UpdateProtectionPresentation()
{
    if (GetNetMode() == NM_DedicatedServer) return;
    const bool Protected = !Health->bDead && Health->IsProtected();
    if (!bProtectionMaterialsDirty && Protected == bProtectionVisualsApplied) return;
    bProtectionMaterialsDirty = false;
    bProtectionVisualsApplied = Protected;
    RestoreProtectionMaterials();
    if (!Protected || !ProtectionBaseMaterial) return;
    if (!ProtectionSurface)
    {
        ProtectionSurface=UMaterialInstanceDynamic::Create(ProtectionBaseMaterial,this);
        ProtectionSurface->SetVectorParameterValue(TEXT("Color"),FLinearColor(.32f,.32f,.32f));
    }
    TInlineComponentArray<UMeshComponent*> Meshes(this);
    for (auto* SurfaceMesh : Meshes)
    {
        // Body, first-person arms and skin gear; weapons retain their materials.
        if (SurfaceMesh != GetMesh() && SurfaceMesh != FirstPersonBody && !SurfaceMesh->ComponentHasTag(TEXT("ERSkinGear"))) continue;
        auto& State=ProtectionSurfaces.AddDefaulted_GetRef();
        State.Mesh=SurfaceMesh;
        for (int32 Slot=0; Slot<SurfaceMesh->GetNumMaterials(); ++Slot)
        {
            State.Originals.Add(SurfaceMesh->GetMaterial(Slot));
            SurfaceMesh->SetMaterial(Slot,ProtectionSurface);
        }
    }
}
FRotator AERCharacter::GetRecoilRotation() const
{
    if (!GetWorld() || !Health || Health->bDead) return FRotator::ZeroRotator;
    const auto* GS=GetWorld()->GetGameState<AERGameState>();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    return ERWeaponRecoil::Rotation(ERWeaponRecoil::At(RecoilState,Now));
}
static double AimPunchClock(const AERCharacter* Character)
{
    const auto* World=Character->GetWorld();const auto* GS=World->GetGameState<AERGameState>();
    const auto* PS=Character->GetPlayerState();
    // GameState time samples describe their send time. Approximate the missing
    // one-way transit from measured RTT; this never changes authority or input.
    return (GS?GS->GetAimPunchServerTimeSeconds():World->GetTimeSeconds())+
        (!Character->HasAuthority() && PS?PS->GetPingInMilliseconds()*.0005:0.);
}
FRotator AERCharacter::GetAimPunchRotation() const
{
    if(!GetWorld() || !Controller || !Health || Health->bDead)return FRotator::ZeroRotator;
    const double Now=AimPunchClock(this);
    // Keep recovery moving even while the initial GameState clock is catching
    // up after loading, or a smoothed clock correction briefly moves backwards.
    const double SampleTime=HasAuthority()?Now:(OwnerPunchLastSampleTime=FMath::Max3(Now,OwnerPunchReceiptServerTime+GetWorld()->GetTimeSeconds()-OwnerPunchReceiptTime,OwnerPunchLastSampleTime));
    const auto Angle=ERAimPunch::At(GetAimPunchState(),SampleTime);
    return FRotator(Angle.X,Angle.Y,0);
}
void AERCharacter::ResetAimPunch()
{
    // Preserve the sequence watermark across possession changes, so an old
    // reliable hit event cannot resurrect a previous owner's disturbance.
    const uint32 Sequence=AuthorityPunch.Sequence+1;
    AuthorityPunch=FERAimPunchState();AuthorityPunch.Sequence=Sequence;
    const uint32 OwnerSequence=OwnerPunch.Sequence;
    OwnerPunch=FERAimPunchState();OwnerPunch.Sequence=OwnerSequence;OwnerPunchLastSampleTime=0;
    DamageViewKick=CosmeticDamagePitch=FVector::ZeroVector;DamageVisuals.Reset();
    PunchPelletWeapon.Reset();PunchPelletShot=MAX_uint32;
    if(HasAuthority() && Controller && Controller->IsPlayerController())
        ClientDamageFeedback(0,FVector::ZeroVector,AuthorityPunch,false);
}
void AERCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    bWeaponLightOn = NewController && NewController->IsPlayerController() && EROutdoorBackdrop::IsNight(*GetWorld());
    ResetAimPunch();
}
void AERCharacter::UnPossessed()
{bWeaponLightOn=false;ResetAimPunch();Super::UnPossessed();}
void AERCharacter::OnRep_Controller()
{
    Super::OnRep_Controller();
    if(!Controller){OwnerPunch.Angle=FVector2D::ZeroVector;DamageVisuals.Reset();DamageViewKick=CosmeticDamagePitch=FVector::ZeroVector;}
}
FRotator AERCharacter::GetEffectiveAimRotation() const
{
    // Camera component, attached FPS rig and authoritative eyes use this once.
    // ControlRotation remains the player's input, so mouse compensation is natural.
    const FRotator Recoil=GetRecoilRotation()+GetAimPunchRotation();
    FRotator View=(Super::GetViewRotation()+Recoil).GetNormalized();
    if(!Recoil.IsNearlyZero())View.Pitch=FMath::Clamp(View.Pitch,-89.f,89.f);
    return View;
}
FRotator AERCharacter::GetViewRotation() const { return GetEffectiveAimRotation(); }
void AERCharacter::AddWeaponRecoil(const FVector2D& Impulse)
{
    if (!HasAuthority()) return;
    RecoilState=ERWeaponRecoil::At(RecoilState,GetWorld()->GetTimeSeconds());
    RecoilState.Velocity+=Impulse;
    ForceNetUpdate();
}
void AERCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
    Super::CalcCamera(DeltaTime, OutResult);
    if (!Health->bDead) { bDeathCameraInitialized = false; DeathCameraFOV = 90.f; }
    // The viewmodel now occupies a shallow depth range; retain nearby fingers.
    OutResult.PerspectiveNearClipPlane = 1.f;
    if (!Health->bDead && Weapon && !IsWeaponStowed())
        OutResult.FOV = Weapon->Data && Weapon->Data->ScopeMode==1 ?
            FMath::Lerp(90.f,Weapon->Tuned()->ZoomFOV,GetIronAimBlend()) : Weapon->GetScopeFOV();
    if (IsLocallyControlled() && !Health->bDead)
    {
        const FVector Right = FRotationMatrix(FRotator(0,OutResult.Rotation.Yaw,0)).GetUnitAxis(EAxis::Y);
        OutResult.Location += Right * StepOffset.Y + FVector::UpVector * StepOffset.Z;
        OutResult.Rotation.Pitch += CameraKick;
        const FVector LocalKick = FRotationMatrix(OutResult.Rotation).GetTransposed().TransformVector(DamageViewKick);
        OutResult.Location += DamageViewKick * .8f;
        OutResult.Rotation.Pitch -= FRotationMatrix(OutResult.Rotation).GetTransposed().TransformVector(CosmeticDamagePitch).X * .45f;
        OutResult.Rotation.Roll += LocalKick.Y * .9f;
    }
    if (IsLocallyControlled() && Health->bDead)
    {
        OutResult.Location.Z += DeathViewDrop;
        OutResult.FOV = 90.f;
        if (bHasDeathFocus && !IsInDeathBeat())
        {
            const bool KillerPresent = IsValid(DeathFocus);
            const FVector Focus = KillerPresent ? DeathFocus->GetActorLocation() :
                bDeathCameraInitialized ? DeathCameraLastFocus : DeathFocusPosition;
            DeathCameraLastFocus = Focus;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(ERDeathCamera),false,this);
            const auto SafePosition = [&](const FVector& From, const FVector& To)
            {
                FHitResult Hit;
                const bool Blocked = GetWorld()->SweepSingleByObjectType(Hit,From,To,FQuat::Identity,
                    FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeSphere(12.f),Query);
                return Blocked ? (Hit.bStartPenetrating ? From : Hit.Location + Hit.Normal*3.f) : To;
            };
            if (!bDeathCameraInitialized)
            {
                DeathCameraAway = (GetActorLocation()-Focus).GetSafeNormal2D();
                if (DeathCameraAway.IsNearlyZero()) DeathCameraAway = -GetActorForwardVector();
                const FVector Pivot = GetActorLocation() + FVector(0,0,100);
                DeathCameraLocation = SafePosition(Pivot,Pivot+DeathCameraAway*200.f+FVector(0,0,60));
                DeathCameraFOV = 90.f;
                bDeathCameraInitialized = true;
            }
            // Fit the killer's full capsule into about 65% of the vertical frame.
            // Read the viewport aspect so ultrawide windows keep head and feet visible.
            int32 Width = 0, Height = 0;
            if (const auto* PC = Cast<APlayerController>(Controller)) PC->GetViewportSize(Width,Height);
            const float Aspect = Width > 0 && Height > 0 ? float(Width)/Height : 16.f/9.f;
            OutResult.bConstrainAspectRatio = false;
            OutResult.AspectRatioAxisConstraint = AspectRatio_MaintainXFOV;
            const float HalfHeight = KillerPresent ? DeathFocus->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 92.5f;
            const float Distance = FMath::Max(260.f,HalfHeight*Aspect/(.65f*FMath::Tan(FMath::DegreesToRadians(30.f))));
            const FVector Desired = Focus + DeathCameraAway*Distance + FVector(0,0,25);
            const float Blend = 1.f-FMath::Exp(-6.f*FMath::Clamp(DeltaTime,0.f,.1f));
            // Sweep every part of the approach; the camera never jumps through cover.
            DeathCameraLocation = SafePosition(DeathCameraLocation,FMath::Lerp(DeathCameraLocation,Desired,Blend));
            DeathCameraFOV = FMath::Lerp(DeathCameraFOV,60.f,Blend);
            OutResult.Location = DeathCameraLocation;
            OutResult.FOV = DeathCameraFOV;
            OutResult.Rotation = (Focus - OutResult.Location).Rotation();
        }
    }
    if (IsLocallyControlled() && !IsDeathCameraActive()) OutResult.Rotation += GetLethalPunch();
}
void AERCharacter::StartDeathBeat()
{
    if (DeathBeatStart < 0) DeathBeatStart = GetWorld()->GetTimeSeconds();
}
bool AERCharacter::IsInDeathBeat() const
{
    return DeathBeatStart >= 0 && GetWorld()->GetTimeSeconds() - DeathBeatStart < DeathBeatSeconds;
}
bool AERCharacter::IsDeathCameraActive() const
{
    return Health && Health->bDead && bHasDeathFocus && !IsInDeathBeat();
}
FRotator AERCharacter::GetLethalPunch() const
{
    if (LethalPunchTime < 0) return FRotator::ZeroRotator;
    // 50ms attack, then a .14s half-life: gone well within the beat.
    const double Age = GetWorld()->GetTimeSeconds() - LethalPunchTime;
    const float Envelope = Age < .05 ? float(Age / .05) : float(FMath::Exp(-(Age - .05) * UE_LN2 / .14));
    return Envelope < .01f ? FRotator::ZeroRotator : LethalPunch * Envelope;
}
void AERCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    StopBlastRinging();
    if (HasAuthority())
    {
        for (auto Item : Inventory) if (IsValid(Item)) Item->Destroy();
        if (IsValid(Weapon)) Weapon->Destroy();
    }
    Super::EndPlay(Reason);
}
float AERCharacter::TakeDamage(float Damage, const FDamageEvent& Event, AController* Killer, AActor* Causer)
{
    if(!HasAuthority() || !Health || Health->bDead)return 0;
    const float Before = Health->Health, ArmorBefore = Health->Armor;
    bool bBullet=false;
    FERMessageDamage Facts;
    FVector Direction = Killer && Killer->GetPawn() ? GetActorLocation() - Killer->GetPawn()->GetActorLocation() : FVector::ForwardVector;
    if (Event.IsOfType(FPointDamageEvent::ClassID)) Direction = static_cast<const FPointDamageEvent&>(Event).ShotDirection;
    LastHitDirection = Direction.GetSafeNormal();
    float Ratio = 2.f;
    if (const auto* Gun = Cast<AERWeapon>(Causer); Gun && Gun->Data)
    {
        Ratio = Gun->Tuned()->ArmorRatio;
        Facts.HasSourceLife = true;
        Facts.SourceLife = Cast<APawn>(Gun->GetOwner());
        Facts.Melee = Gun->Data->Payload == EERPayload::Melee;
        Facts.LastBullet = Gun->Data->Payload == EERPayload::Bullet && Gun->MagazineAmmo == 0;
        if (Facts.Melee && Event.IsOfType(FPointDamageEvent::ClassID))
        {
            const auto& Point = static_cast<const FPointDamageEvent&>(Event);
            const auto MeleeZone=ERWeaponBallistics::ZoneForBone(Point.HitInfo.BoneName);
            const float BaseMultiplier=ERWeaponBallistics::MeleeMultiplier(Point.HitInfo.BoneName);
            Damage *= MeleeZone==ERWeaponBallistics::EHitZone::Head?ERServer::Get(GetWorld(),"Knife.Head"):
                BaseMultiplier<1.f?ERServer::Get(GetWorld(),"Knife.Limbs"):1.f;
            if (const AActor* Attacker = Gun->GetOwner())
            {
                const FVector ToAttacker=(Attacker->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
                const bool KnownTorso=ERWeaponBallistics::MeleeRearBonus(Point.HitInfo.BoneName,GetActorForwardVector(),-GetActorForwardVector())>0;
                if(KnownTorso && FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(),ToAttacker)<-FMath::Cos(FMath::DegreesToRadians(ERServer::Get(GetWorld(),"Knife.RearAngle")*.5f)))
                    Damage+=ERServer::Get(GetWorld(),"Knife.RearBonus");
            }
            Facts.Headshot = ERWeaponBallistics::ZoneForBone(Point.HitInfo.BoneName) == ERWeaponBallistics::EHitZone::Head;
        }
        if (Gun->Data->Payload == EERPayload::Bullet && Event.IsOfType(FPointDamageEvent::ClassID))
        {
            bBullet=true;
            const auto& Point = static_cast<const FPointDamageEvent&>(Event);
            Facts.DistanceMeters = FVector::Distance(Gun->LastShotTraceStart, Point.HitInfo.ImpactPoint) / 100.f;
            Facts.Headshot = !Point.HitInfo.BoneName.IsNone() && ERWeaponBallistics::ZoneForBone(Point.HitInfo.BoneName) == ERWeaponBallistics::EHitZone::Head;
            if(Gun->Data->bUseBodyHitZones)
            {
                const auto Zone=ERWeaponBallistics::ZoneForBone(Point.HitInfo.BoneName);
                Damage*=ERWeaponBallistics::ZoneMultiplier(Zone, Gun->Tuned()->HeadshotMultiplier);
                if(Zone!=ERWeaponBallistics::EHitZone::Head)Damage*=ERServer::Get(GetWorld(),Zone==ERWeaponBallistics::EHitZone::Legs?FName("Damage.Legs"):ERWeaponBallistics::MeleeMultiplier(Point.HitInfo.BoneName)<1.f?FName("Damage.Arms"):FName("Damage.Body"));
                if(Zone==ERWeaponBallistics::EHitZone::Legs ||
                    (Zone==ERWeaponBallistics::EHitZone::Head && !Health->bHelmet)) Ratio=2.f;
            }
            Damage = ERWeaponBallistics::AtRange(Damage, Gun->Tuned()->RangeModifier,
                FVector::Distance(Gun->LastShotTraceStart, Point.HitInfo.ImpactPoint));
        }
    }
    else if (const auto* Projectile = Cast<AERProjectile>(Causer); Projectile &&
        (Projectile->Payload == EERPayload::HE || Projectile->Payload == EERPayload::LaunchedHE))
            Ratio = ERWeaponBallistics::GrenadeArmorRatio(Projectile->BlastArmorRatio);
    if(const auto* Mine=Cast<AERClaymore>(Causer))
    { Ratio=Mine->ArmorRatio;Facts.Explosion=true; }
    if (const auto* Projectile = Cast<AERProjectile>(Causer))
    {
        Facts.HasSourceLife = true;
        Facts.SourceLife = Projectile->GetInstigator();
        Facts.Explosion = Projectile->HasDetonated() && (Projectile->Payload == EERPayload::HE ||
            Projectile->Payload == EERPayload::LaunchedHE || Projectile->Payload == EERPayload::Rocket);
        Facts.Grenade = Facts.Explosion && (Projectile->Payload == EERPayload::HE || Projectile->Payload == EERPayload::LaunchedHE);
    }
    // Kill feed icon: catalog key of the weapon, grenade or mine that applied this damage.
    if (const auto* Gun = Cast<AERWeapon>(Causer); Gun && Gun->Data)
    { for (int32 Key = 0; Key < 11; ++Key) if (Gun->Data->GetFName() == ERLoadouts::WeaponAssetName(Key)) { Facts.WeaponKey = Key; break; } }
    else if (const auto* Projectile = Cast<AERProjectile>(Causer))
        Facts.WeaponKey = Projectile->Payload == EERPayload::HE ? 8 : Projectile->Payload == EERPayload::Smoke ? 9 :
            (Projectile->Payload == EERPayload::LaunchedHE || Projectile->Payload == EERPayload::Rocket) ? 7 : -1;
    else if (Cast<AERClaymore>(Causer)) Facts.WeaponKey = 10;
    FHitResult WoundHit;
    if(bBullet)
    {
        WoundHit=static_cast<const FPointDamageEvent&>(Event).HitInfo;
        const auto* SourceGun=Cast<AERWeapon>(Causer);
        const auto* SourceCharacter=SourceGun ? Cast<AERCharacter>(SourceGun->GetOwner()):nullptr;
        // Damage/range use the historical contact; wounds follow that contact on the current bone.
        if(SourceCharacter && SourceGun->Data->bUseBodyHitZones)
            CastChecked<UERPostureMesh>(GetMesh())->ShotHistory.ProjectToCurrent(GetMesh(),WoundHit,ERShotTrace::BulletTime(SourceCharacter));
    }
    Health->ReceiveDamage(Damage, Killer, Ratio, Facts, bBullet ? &WoundHit : nullptr);
    const float Applied=Before-Health->Health;
    if (Facts.Explosion && !Health->bDead && (Applied > 0 || Health->Armor < ArmorBefore)
        && Controller && Controller->IsPlayerController())
    {
        TSubclassOf<AERGrenadeExplosion> BlastEffect;
        if (const auto* Mine = Cast<AERClaymore>(Causer)) BlastEffect = Mine->BlastEffect;
        if (const auto* Projectile = Cast<AERProjectile>(Causer); Projectile && Facts.Grenade)
            BlastEffect = Projectile->GrenadeExplosionClass;
        const float MaxHealth = FMath::Max(1.f, ERServer::Get(GetWorld(), "Health.Max"));
        const float DamageTaken = Applied + ArmorBefore - Health->Armor;
        ClientBlastRinging(10.f * FMath::Clamp(DamageTaken / MaxHealth, 0.f, 1.f),
            Causer->GetActorLocation(), BlastEffect);
    }
    if(Applied>0)
    {
        if(bBullet && !Health->bDead)
        {
            auto* Gun=CastChecked<AERWeapon>(Causer);
            const bool FirstPellet=PunchPelletWeapon.Get()!=Gun || PunchPelletShot!=Gun->AuthorizedShotCount;
            const FVector Right=FRotationMatrix(GetEffectiveAimRotation()).GetUnitAxis(EAxis::Y);
            const float Side=FVector::DotProduct(-LastHitDirection,Right);
            const auto Impulse=ERAimPunch::Impulse(AimPunchTuning,Applied,Facts.Headshot,Health->Armor<ArmorBefore,Side,AuthorityPunch.Sequence+1,FirstPellet);
            ERAimPunch::Add(AuthorityPunch,AimPunchTuning,Impulse,GetWorld()->GetTimeSeconds());
            PunchPelletWeapon=Gun;PunchPelletShot=Gun->AuthorizedShotCount;
        }
        ++AuthorityPunch.Sequence;
#if !UE_BUILD_SHIPPING
        if(bBullet && !Health->bDead && FParse::Param(FCommandLine::Get(),TEXT("ERAimPunchAudit")))
            UE_LOG(LogTemp,Display,TEXT("ER_AIMPUNCH_STATE pawn=%s seq=%u time=%.6f real=%.6f pitch=%.6f half_life=%.6f"),
                *GetName(),AuthorityPunch.Sequence,AuthorityPunch.ServerTime,FPlatformTime::Seconds(),AuthorityPunch.Angle.X,AuthorityPunch.HalfLife);
#endif
        ClientDamageFeedback(Applied,LastHitDirection,AuthorityPunch,bBullet,Health->bDead);
    }
    return Before - Health->Health;
}
void AERCharacter::ClientDamageFeedback_Implementation(float Amount, FVector_NetQuantizeNormal Direction, const FERAimPunchState& Snapshot, bool bBullet, bool bLethal)
{
    if (!IsLocallyControlled() || !Controller || !Controller->IsPlayerController()) return;
    if(!HasAuthority())
    {
        if(!ERAimPunch::Newer(Snapshot.Sequence,OwnerPunch.Sequence))return;
        OwnerPunch=Snapshot;
        OwnerPunchReceiptTime=GetWorld()->GetTimeSeconds();
        OwnerPunchReceiptServerTime=FMath::Max(Snapshot.ServerTime,AimPunchClock(this));
    }
#if !UE_BUILD_SHIPPING
    if(!HasAuthority() && Amount>0 && bBullet && FParse::Param(FCommandLine::Get(),TEXT("ERAimPunchAudit")))
        UE_LOG(LogTemp,Display,TEXT("ER_AIMPUNCH_RECEIVED pawn=%s seq=%u real=%.6f pitch=%.6f ping_ms=%.3f"),*GetName(),Snapshot.Sequence,FPlatformTime::Seconds(),GetAimPunchRotation().Pitch,GetPlayerState()?GetPlayerState()->GetPingInMilliseconds():0.f);
#endif
    if(Amount<=0){DamageVisuals.Reset();DamageViewKick=CosmeticDamagePitch=FVector::ZeroVector;return;}
    if(bLethal)
    {
        // The death may replicate before or after this RPC; either one starts the beat.
        StartDeathBeat();
        const FVector Travel=FVector(Direction);
        const FVector Local=FRotationMatrix(FRotator(0,GetViewRotation().Yaw,0)).GetTransposed().TransformVector(Travel);
        if(!bBullet && Travel.Z<-.9f) LethalPunch=FRotator(-9.f,0,0);
        else
        {
            // The head follows the hit: snapped back from the front, forward from behind,
            // turned and rolled toward the travel side. Blasts and heavy hits punch harder.
            const float Base=(bBullet?5.f:8.f)+FMath::Clamp(Amount/50.f,0.f,1.f)*3.f;
            const float Pitch=Local.X<=0.f?FMath::Max(.5f,-float(Local.X)):-FMath::Max(.5f,float(Local.X));
            LethalPunch=FRotator(Base*Pitch,float(Local.Y)*5.f,float(Local.Y)*10.f);
        }
        LethalPunchTime=GetWorld()->GetTimeSeconds();
    }
    if(!Health->bDead)
    {
        if(DamageVisuals.Num()>=4)DamageVisuals.RemoveAt(0);
        DamageVisuals.Add({-FVector(Direction),GetWorld()->GetTimeSeconds(),FMath::Clamp(.30f+Amount/100.f,.35f,.75f)});
    }
    if(!bBullet)CosmeticDamagePitch=(CosmeticDamagePitch+FVector(Direction)*FMath::Clamp(Amount/25.f,.5f,1.2f)).GetClampedToMaxSize(1.5f);
    DamageViewKick = (DamageViewKick + FVector(Direction) * FMath::Clamp(Amount / 25.f, .5f, 1.2f)).GetClampedToMaxSize(1.5f);
    if (HitSound) UGameplayStatics::PlaySoundAtLocation(this, HitSound,
        Camera->GetComponentLocation() - FVector(Direction) * 140.f, bLethal ? 1.f : .65f, 1.f, 0.f, HitAttenuation);
    UE_LOG(LogTemp, Display, TEXT("ER_DAMAGE_FEEDBACK amount=%.1f direction=%s sound=%d"), Amount, *FVector(Direction).ToString(), HitSound != nullptr);
}
bool AERCharacter::CanJumpInternal_Implementation() const
{return Cast<UERMovement>(GetCharacterMovement())->IsOnLadder() || Super::CanJumpInternal_Implementation();}
float AERCharacter::FallDamage(float DownSpeed,float Gravity)
{
    if(!FMath::IsFinite(DownSpeed) || !FMath::IsFinite(Gravity) || DownSpeed<=0 || Gravity<=0)return 0;
    // Equivalent free-fall height: safe through 3m, lethal at 11m, no armor absorption.
    const float Height=DownSpeed*DownSpeed/(2*Gravity);
    return FMath::Clamp((Height-300.f)/8.f,0.f,100.f);
}
void AERCharacter::Landed(const FHitResult& Hit)
{
    const float DownSpeed=Cast<UERMovement>(GetCharacterMovement())->LandingSpeed();
    const float Gravity=FMath::Abs(GetCharacterMovement()->GetGravityZ());
    const float Height=Gravity>0?DownSpeed*DownSpeed/(2*Gravity):0;
    const float Safe=ERServer::Get(GetWorld(),"Damage.FallSafe")*100,Lethal=ERServer::Get(GetWorld(),"Damage.FallLethal")*100;
    const float Damage=FMath::Clamp((Height-Safe)/FMath::Max(1.f,Lethal-Safe),0.f,1.f)*ERServer::Get(GetWorld(),"Health.Max")*ERServer::Get(GetWorld(),"Damage.Fall");
    if(HasAuthority())for(auto Item:Inventory)if(Item)Item->NotifyAccuracyLanding(DownSpeed);
    Super::Landed(Hit);
    if(HasAuthority() && Damage>0)
    {
        FERMessageDamage Facts; Facts.Fall = true;
        const float Applied=Health->ReceiveDamage(Damage,nullptr,2.f,Facts);
        if(Applied>0){++AuthorityPunch.Sequence;ClientDamageFeedback(Applied,FVector(0,0,-1),AuthorityPunch,false,Health->bDead);}
        UE_LOG(LogTemp,Display,TEXT("ER_FALL_DAMAGE amount=%.2f applied=%.2f hp=%.2f"),Damage,Applied,Health->Health);
    }
}
void AERCharacter::FellOutOfWorld(const UDamageType& Type)
{
    if (HasAuthority() && !Health->bDead)
    {
        Health->ProtectionEndServerTime = 0;
        Health->ReceiveDamage(Health->Health+2*Health->Armor, nullptr);
    }
}
void AERCharacter::ApplyDeathVisuals()
{
    if (bDeathVisualsApplied) return;
    bDeathVisualsApplied = true;
    if (IsLocallyControlled()) StartDeathBeat();
    bPredictingFire=false;
    StopBlastRinging();
    ResetAimPunch();
    if (HasAuthority()) for (auto Item : Inventory) if (Item) Item->Cancel();
    if (HasAuthority() && CorpseClass)
    {
        FTransform Transform(GetActorRotation(), GetActorLocation());
        if (auto* Corpse = GetWorld()->SpawnActorDeferred<AERCorpse>(CorpseClass, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
        {
            if (const auto* State = GetPlayerState<AERPlayerState>()) { Corpse->Team = State->Team; Corpse->SkinIndex = State->ActiveSkin; }
            Corpse->Wounds->CopyFrom(Wounds);
            Corpse->ImpactDirection = LastHitDirection;
            UGameplayStatics::FinishSpawningActor(Corpse, Transform);
        }
    }
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FirstPersonBody->SetVisibility(false); FirstPersonRifle->SetVisibility(false); WorldRifle->SetVisibility(false); TeamMarker->SetVisibility(false);
    FirstPersonSight->SetVisibility(false); WorldSight->SetVisibility(false);
    GetMesh()->SetVisibility(false, true);
    FirstPersonMuzzleFlash->SetVisibility(false, true); WorldMuzzleFlash->SetVisibility(false, true);
    MuzzleLight->SetVisibility(false);
    CameraKick = 0; MuzzleFlashUntil = 0;
}
void AERCharacter::OnRep_NetworkReady()
{
    SetActorEnableCollision(bNetworkReady && !Health->bDead);
    if(!bNetworkReady){bPredictingFire=false;SetFireInputHeld(false);}
}
void AERCharacter::SetFireInputHeld(bool Held)
{
    if(!IsLocallyControlled())return;
    if(Held && (!bNetworkReady || Health->bDead))return;
    auto* Movement=CastChecked<UERMovement>(GetCharacterMovement());
    if(Movement->FireInput.Held==Held)return;
    const int32 SelectedSlot=Inventory.IndexOfByKey(Weapon);
    const FERFireWeaponBinding SelectedWeapon{uint8(SelectedSlot>=0 && SelectedSlot<MAX_uint8 ? SelectedSlot:MAX_uint8),WeaponSelectionRevision};
    Movement->SetFireInput(Held,SelectedWeapon);
    SetGrenadeInputHeld(Held);
    if(HasAuthority()){ApplyFireMove(Movement->FireInput.Sequence,Held,Movement->FireWeapon);return;}
    bPredictingFire=Held;
    if(Held)
    {
        if(PredictedFireWeapon!=Weapon)NextPredictedFireTime=0;
        PredictedFireWeapon=Weapon;PredictedFireSequence=Movement->FireInput.Sequence;PredictedFireOrdinal=0;
        PredictedMagazineLimit=Weapon ? Weapon->MagazineAmmo:0;
        UpdatePredictedFire();
    }
}
void AERCharacter::ApplyFireMove(uint16 Sequence,bool Held,const FERFireWeaponBinding& WeaponBinding)
{
    if(!HasAuthority())return;
    bool Pressed=false;
    if(!ServerFireInput.Receive(Sequence,Held,Pressed))return;
    ++AcceptedFireMoveCount;
    if(Pressed)
    {
        if(Weapon)Weapon->StopFire();
        ServerFireOrdinal=0;
        FireAcknowledgement.Sequence=Sequence;FireAcknowledgement.ShotOrdinal=0;
        FireAcknowledgement.Data=Weapon ? Weapon->Data.Get():nullptr;
        // A reliable switch can overtake a retransmitted movement press. Consume
        // its sequence, but never transfer that press onto the newly selected gun.
        if(WeaponBinding.Matches(Inventory.IndexOfByKey(Weapon),WeaponSelectionRevision))ApplyStartFire();
    }
    if(!Held && Weapon)Weapon->StopFire();
    UpdateFireAcknowledgement();++FireAcknowledgement.Revision;ForceNetUpdate();
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("ERHitLagProbe")))
        UE_LOG(LogTemp,Display,TEXT("ER_FIRE_MOVE pawn=%s sequence=%u pressed=%d held=%d shots=%u move=%.6f"),
            *GetName(),Sequence,Pressed,Held,ServerFireOrdinal,CastChecked<UERMovement>(GetCharacterMovement())->LastAcceptedMoveTimeStamp);
#endif
}
void AERCharacter::UpdateFireAcknowledgement()
{
    const bool Firing=Weapon && Weapon->IsFiring();
    if(FireAcknowledgement.Firing==Firing)return;
    FireAcknowledgement.Firing=Firing;++FireAcknowledgement.Revision;ForceNetUpdate();
}
void AERCharacter::UpdatePredictedFire()
{
    if(!bPredictingFire || HasAuthority())return;
    const auto* State=GetWorld()->GetGameState<AERGameState>();
    const auto* Data=Weapon ? Weapon->Tuned():nullptr;
    if(!IsLocallyControlled() || !bNetworkReady || Health->bDead || !State || !State->IsMatchInProgress() ||
        PredictedFireWeapon!=Weapon || !Data || Data->Payload!=EERPayload::Bullet || Weapon->bReloading ||
        Weapon->MagazineAmmo<=0 || PredictedFireOrdinal>=PredictedMagazineLimit || IsWeaponStowed() ||
        CastChecked<UERMovement>(GetCharacterMovement())->bHighClimb)
    {bPredictingFire=false;return;}
    const double Now=GetWorld()->GetTimeSeconds();
    const double ServerNow=State->GetServerWorldTimeSeconds();
    if(!ERFirePrediction::Ready(Now,NextPredictedFireTime,PredictedFireOrdinal,ServerNow,Weapon->LastSpreadShotServerTime,Data->ShotInterval))return;
    ++PredictedFireOrdinal;
    if(ShotPresentation.Mark(PredictedFireSequence,PredictedFireOrdinal))
    {
        ++PredictedShotFeedbackCount;
        PlayShotPresentation(Weapon->Data);
        if (Data->TracerEveryNShots == 1 && FirstPersonMuzzleFlash->IsVisible())
            AERAttachedTracer::SpawnMuzzleStreak(this, TracerSystem,
                FERShotPresentationWindow::Key(PredictedFireSequence, PredictedFireOrdinal));
    }
    NextPredictedFireTime=Now+FMath::Max(.01f,Data->ShotInterval);
    if(!Data->bAutomatic)bPredictingFire=false;
}
void AERCharacter::RecordPredictedTracer(uint32 ShotKey, const FVector& Muzzle)
{
    if (PredictedTracers.Num() == FERShotPresentationWindow::Capacity) PredictedTracers.RemoveAt(0);
    PredictedTracers.Add({ShotKey, Muzzle});
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("ERTracerAudit")))
        UE_LOG(LogTemp, Display, TEXT("ER_TRACER_PREDICT key=%u frame=%llu flash=%d speed=%.1f muzzle=%s"),
            ShotKey, static_cast<unsigned long long>(GFrameCounter), FirstPersonMuzzleFlash->IsVisible(),
            GetVelocity().Size2D(), *Muzzle.ToString());
#endif
}
void AERCharacter::OnRep_FireAcknowledgement()
{
    if(HasAuthority() || !IsLocallyControlled())return;
    const auto& Ack=FireAcknowledgement;
    // Replicated state eventually repairs a lost multicast. It carries no hit,
    // damage or tracer prediction and coalesces during an automatic burst.
    if(Ack.ShotOrdinal>0 && ShotPresentation.Mark(Ack.Sequence,Ack.ShotOrdinal) &&
        bNetworkReady && !Health->bDead && Weapon && Weapon->Data==Ack.Data)
        PlayShotPresentation(Ack.Data);
    if(Ack.Sequence==PredictedFireSequence && !Ack.Firing)bPredictingFire=false;
}
void AERCharacter::ServerStartFire_Implementation()
{
    const auto* Driver = GetNetDriver();
    if (!Driver || !Driver->IsInTick()) { ApplyStartFire(); return; }
    // A received trigger overtakes the move carrying that frame's view in the same packet.
    // Timers run after this frame's moves, so the shot uses the aim the player fired with.
    bQueuedFireStart = true; bQueuedFireStop = false;
    GetWorldTimerManager().SetTimerForNextTick(this, &AERCharacter::ApplyQueuedFire);
}
void AERCharacter::ApplyStartFire() { if (bNetworkReady && !Health->bDead && Weapon && !Cast<UERMovement>(GetCharacterMovement())->bHighClimb) Weapon->StartFire(); }
void AERCharacter::ApplyQueuedFire()
{
    const bool bStart = bQueuedFireStart, bStop = bQueuedFireStop;
    bQueuedFireStart = bQueuedFireStop = false;
    if (bStart) ApplyStartFire();
    if (bStop && Weapon) Weapon->StopFire();
}
void AERCharacter::SetGrenadeInputHeld(bool bHeld)
{
    if(!IsLocallyControlled()) return;
    if(!bHeld)
    {
        if(bLocalGrenadeInputHeld) LocalGrenadeHideUntil=GetWorld()->GetTimeSeconds()+.35;
        bLocalGrenadeInputHeld=false;return;
    }
    const auto* RoundState=GetWorld()->GetGameState<AERGameState>();
    if(Health->bDead || !RoundState || !RoundState->IsMatchInProgress() || !Weapon || !Weapon->Data || !AERWeapon::IsHandGrenade(Weapon->Data->Payload) ||
        Weapon->MagazineAmmo<=0 || Weapon->bReloading || Weapon->IsGrenadeHandHidden() || Cast<UERMovement>(GetCharacterMovement())->bHighClimb) return;
    bLocalGrenadeInputHeld=true;
    LocalGrenadeCookDeadline=GetWorld()->GetTimeSeconds()+Weapon->Tuned()->FuseDuration;
}
void AERCharacter::ServerStopFire_Implementation()
{
    if (bQueuedFireStart) bQueuedFireStop = true;
    else if (Weapon) Weapon->StopFire();
}
void AERCharacter::ServerReload_Implementation() { if (!Health->bDead && Weapon && !Weapon->bPlacingClaymore && !Cast<UERMovement>(GetCharacterMovement())->bHighClimb) Weapon->Reload(); }
bool AERCharacter::UsesReferenceArsenal() const
{
    // Both catalogs remain available for comparison and existing saved assets.
    return bPresetInventory || bReferenceInventory;
}
void AERCharacter::InitializeLoadout(int32 Primary)
{
    if (!HasAuthority() || !Inventory.IsEmpty()) return;
    // Raw exhibition/test actors can own a fallback; never leave it hidden
    // beside a resolved inventory. Normal GameMode spawns skip its creation.
    if(Weapon) { Weapon->Destroy();Weapon=nullptr; }
    bReferenceInventory=!GetWorld()->URL.HasOption(TEXT("ERLegacyArsenal"));
    bPresetInventory=!GetWorld()->URL.HasOption(TEXT("ERLegacyArsenal")) && !GetWorld()->URL.HasOption(TEXT("ERFullArsenalTest"));
    if(bPresetInventory)
    {
        const auto* PS=GetPlayerState<AERPlayerState>();
        const auto Kit=ERLoadouts::Resolve(PS?PS->LoadoutPreset:FName(),PS?PS->LoadoutVariant:FName(),PS && PS->Team==EERTeam::Omega,this,&ERServer::Rules(GetWorld()));
        for(const auto& Entry:Kit.Items)if(!Entry.Data)
        { UE_LOG(LogTemp,Error,TEXT("ER_LOADOUT_INVALID_CATALOG missing=%d"),Entry.WeaponKey);return; }
        ActivePreset=Kit.Preset;ActiveVariant=Kit.Variant;
        Health->Health=ERServer::Get(GetWorld(),"Health.Start");Health->Armor=ERServer::Get(GetWorld(),"Health.Armor");Health->bHelmet=ERServer::Get(GetWorld(),"Health.Helmet")>0;
        for(const auto& Entry:Kit.Items)
        {
            if(!Entry.Data)
            { UE_LOG(LogTemp,Error,TEXT("ER_LOADOUT_MISSING_ASSET index=%d"),Entry.WeaponKey);continue; }
            auto* Item=GetWorld()->SpawnActorDeferred<AERWeapon>(WeaponClass,GetActorTransform(),this,this,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!Item)continue;
            Item->Data=Entry.Data;
            Item->InitialLoaded=Entry.Loaded;Item->InitialReserve=Entry.Reserve;
            UGameplayStatics::FinishSpawningActor(Item,Item->GetActorTransform());
            Item->AttachToComponent(GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);
            Inventory.Add(Item);
        }
        Weapon=Inventory.IsEmpty()?nullptr:Inventory[0];
        if(Weapon)++WeaponSelectionRevision;
        ForceNetUpdate();return;
    }
    if(Arsenal.Num()<11)return;
    if (Weapon) Weapon->Destroy();
    const bool bReference = UsesReferenceArsenal();
    const TArray<int32> Indices = bReference ? TArray<int32>{0,1,2,3,4,5,6,7,8,9} : TArray<int32>{FMath::Clamp(Primary, 0, 6), 7, 8, 9, 10};
    Health->Armor = bReference ? 100.f : 0.f;
    for (int32 Index : Indices)
    {
        auto* Item = GetWorld()->SpawnActorDeferred<AERWeapon>(WeaponClass, GetActorTransform(), this, this,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Item) continue;
        Item->Data = bReference ? ReferenceArsenal[Index] : Arsenal[Index];
        UGameplayStatics::FinishSpawningActor(Item, Item->GetActorTransform());
        Item->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        Inventory.Add(Item);
    }
    Weapon = Inventory.IsEmpty() ? nullptr : Inventory[bReference ? FMath::Clamp(Primary, 0, 9) : 0];
    if(Weapon)++WeaponSelectionRevision;
    ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("ER_LOADOUT_SPAWN primary=%d slots=%d"), Primary, Inventory.Num());
}
int32 AERCharacter::FindCatalogSlot(int32 Index) const
{
    if(!bPresetInventory)return Index;
    const FName Id=ERLoadouts::WeaponAssetName(Index);
    for(int32 I=0;I<Inventory.Num();++I)
        if(Inventory[I] && Inventory[I]->Data && Inventory[I]->Data->GetFName()==Id)return I;
    return INDEX_NONE;
}
bool AERCharacter::CanHoldWeaponOnLadder() const
{
    const auto* D=Weapon ? Weapon->Data.Get():nullptr;
    return D && ((D->bOneHanded && D->Payload==EERPayload::Bullet) ||
        D->Payload==EERPayload::Melee || AERWeapon::IsHandGrenade(D->Payload));
}
bool AERCharacter::IsWeaponStowed() const
{
    const auto* M=Cast<UERMovement>(GetCharacterMovement());
    return Weapon && M && !CanHoldWeaponOnLadder() &&
        (M->IsOnLadder() || (M->IsMantling() && M->CustomMovementMode==3) ||
            M->IsWeaponAtLadder() || M->ShouldLowerWeaponForLedge());
}
void AERCharacter::ServerSelectWeapon_Implementation(int32 Slot)
{
    auto* Movement=Cast<UERMovement>(GetCharacterMovement());
    if(Health->bDead || (Weapon && (Weapon->bCookingGrenade || Weapon->bPlacingClaymore)) || (Movement->IsMantling() && Movement->CustomMovementMode!=3))return;
    if(!Inventory.IsValidIndex(Slot) || !Inventory[Slot] || Weapon==Inventory[Slot])return;
    if(bPresetInventory && Inventory[Slot]->Data && Inventory[Slot]->Data->Payload!=EERPayload::Bullet && Inventory[Slot]->Data->Payload!=EERPayload::Melee && Inventory[Slot]->MagazineAmmo+Inventory[Slot]->ReserveAmmo<=0)return;
    if(Weapon)Weapon->Cancel();
    Weapon=Inventory[Slot];++WeaponSelectionRevision;
    if(IsWeaponStowed())Weapon->Cancel();
    MuzzleFlashUntil = 0; CameraKick = 0;
    ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("ER_WEAPON_SELECTED slot=%d name=%s ammo=%d reserve=%d"),
        Slot, *Weapon->Data->DisplayName, Weapon->MagazineAmmo, Weapon->ReserveAmmo);
}
FVector AERCharacter::GetIronSightLocation() const
{
    // Weapon-owned aim anchor, measured from preserved source geometry (cm).
    return FirstPersonRifle->GetComponentTransform().TransformPosition(Weapon && Weapon->Data ? Weapon->Data->IronSightOffset : FVector::ZeroVector);
}
FVector AERCharacter::GetVisualMuzzle(bool bFirstPerson) const
{
    if(!Weapon)return Camera->GetComponentLocation();
    const auto* Component = bFirstPerson ? FirstPersonRifle.Get() : WorldRifle.Get();
    return Weapon && Weapon->Data && Weapon->Data->Mesh ? Component->GetComponentTransform().TransformPosition(Weapon->Data->MuzzleOffset) :
        Component->GetSocketLocation(TEXT("Muzzle"));
}
FVector AERCharacter::GetRenderedMuzzle(bool bFirstPerson) const
{
    const FVector Muzzle = GetVisualMuzzle(bFirstPerson);
    if (bFirstPerson)
        if (const auto* PC = Cast<APlayerController>(Controller); PC && PC->PlayerCameraManager)
            return PC->PlayerCameraManager->GetCameraCacheView().TransformWorldToFirstPerson(Muzzle, false);
    return Muzzle;
}
void AERCharacter::ServerToggleWeaponLight_Implementation()
{
    if (!Controller || !Controller->IsPlayerController() || Health->bDead || !bNetworkReady) return;
    bWeaponLightOn = !bWeaponLightOn;
    ForceNetUpdate();
}

void AERCharacter::UpdateWeaponLight(bool bStowed)
{
    const bool bFirearm = AppliedWeaponData && (AppliedWeaponData->Payload == EERPayload::Bullet ||
        AppliedWeaponData->Payload == EERPayload::Rocket || AppliedWeaponData->Payload == EERPayload::LaunchedHE);
    if (!bWeaponLightOn || !bFirearm || bStowed || Health->bDead || !bNetworkReady)
    {
        if (WeaponLight && WeaponLight->IsVisible()) WeaponLight->SetVisibility(false);
        return;
    }
    if (GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender()) return;
    if (!WeaponLight)
    {
        WeaponLight = NewObject<USpotLightComponent>(this, TEXT("WeaponFlashlight"));
        AddInstanceComponent(WeaponLight);
        WeaponLight->SetupAttachment(GetRootComponent());
        WeaponLight->SetMobility(EComponentMobility::Movable);
        WeaponLight->SetIntensityUnits(ELightUnits::Candelas);
        WeaponLight->SetIntensity(160.f);
        WeaponLight->SetAttenuationRadius(2000.f);
        WeaponLight->SetMaxDrawDistance(2600.f);
        WeaponLight->SetMaxDistanceFadeRange(450.f);
        WeaponLight->SetInnerConeAngle(9.f);
        WeaponLight->SetOuterConeAngle(17.f);
        WeaponLight->SetCastShadows(false);
        WeaponLight->SetAffectGlobalIllumination(false);
        WeaponLight->SetAffectTranslucentLighting(false);
        WeaponLight->SetVolumetricScatteringIntensity(0.f);
        WeaponLight->SetSpecularScale(.35f);
        WeaponLight->RegisterComponent();
    }
    const bool bFirstPerson = IsLocallyControlled();
    const FRotator Aim = bFirstPerson ? GetViewRotation() : GetBaseAimRotation();
    const FVector Origin = GetRenderedMuzzle(bFirstPerson) - FRotationMatrix(Aim).GetUnitAxis(EAxis::Z) * 5.;
    WeaponLight->SetWorldLocationAndRotation(Origin, Aim);
    if (!WeaponLight->IsVisible()) WeaponLight->SetVisibility(true);
}
void AERCharacter::UpdateWeaponPresentation()
{
    auto* Data = Weapon ? Weapon->Data.Get() : nullptr;
    if (!Data || !Data->Mesh)
    {
        // Keep the hidden meshes attached: their muzzle sockets still anchor
        // cosmetic children. Visibility below follows the equipped weapon.
        AppliedWeaponData=nullptr;return;
    }
    if(Data->bWorldReferencePose)
    {
        if(WorldRifle->GetAttachSocketName()!=TEXT("ER_Palm_R"))WorldRifle->AttachToComponent(GetMesh(),FAttachmentTransformRules::KeepRelativeTransform,TEXT("ER_Palm_R"));
        WorldRifle->SetAbsolute(false,false,true);
        const float ParentScale=GetMesh()->GetSocketTransform(TEXT("ER_Palm_R")).GetScale3D().X;
        WorldRifle->SetRelativeTransform(ERWorldWeaponPose::Attachment(*Data,ParentScale));
    }
    else if(WorldRifle->GetAttachSocketName()!=TEXT("HandGrip_R"))
    {
        WorldRifle->SetAbsolute(false,false,false);WorldRifle->AttachToComponent(GetMesh(),FAttachmentTransformRules::KeepRelativeTransform,TEXT("HandGrip_R"));WorldRifle->SetRelativeTransform(Data->GripTransform);
    }
    // Authored FPS translations are baked into the owned Animation-mode rig.
    // SetDisableRetargeting is an editor debugging flag: it forces raw curve
    // sampling and asserts in cooked games, where raw animation data is absent.
    if (Data == AppliedWeaponData)
    {
        if (!Weapon->bReloading)
        {
            for (auto* Component : {FirstPersonRifle.Get(), WorldRifle.Get()})
                if (Data->WeaponIdleAnimation && !Component->IsPlaying()) Component->PlayAnimation(Data->WeaponIdleAnimation, true);
            if (Data->IdleAnimation && !FirstPersonBody->IsPlaying()) FirstPersonBody->PlayAnimation(Data->IdleAnimation, true);
        }
        if (bAppliedReloading != Weapon->bReloading)
        {
            bAppliedReloading = Weapon->bReloading;
            for (auto* Component : {FirstPersonRifle.Get(), WorldRifle.Get()})
            {
                UAnimSequence* Clip = bAppliedReloading ? Data->WeaponReloadAnimation.Get() : Data->WeaponIdleAnimation.Get();
                if (Clip)
                {
                    Component->PlayAnimation(Clip, !bAppliedReloading);
                    Component->SetPlayRate(bAppliedReloading ? Clip->GetPlayLength() / FMath::Max(.01f, Data->ReloadDuration) : 1.f);
                }
            }
            UAnimSequence* Arms = bAppliedReloading ? Data->ReloadAnimation.Get() : Data->IdleAnimation.Get();
            if (Arms)
            {
                FirstPersonBody->PlayAnimation(Arms, !bAppliedReloading);
                FirstPersonBody->SetPlayRate(bAppliedReloading ? Arms->GetPlayLength() / FMath::Max(.01f, Data->ReloadDuration) : 1.f);
            }
        }
        return;
    }
    AppliedWeaponData = Data;
    bAppliedReloading = false;
    FirstPersonRifle->AttachToComponent(FirstPersonBody,FAttachmentTransformRules::KeepRelativeTransform,
        Data->bFirstPersonPalmGrip ? TEXT("ER_Palm_R") : TEXT("HandGrip_R"));
    IronAimBlend=0;
    FirstPersonBody->SetRelativeLocation(FirstPersonBodyBaseLocation + Data->ViewOffset);
    for (auto* Sight : {FirstPersonSight.Get(), WorldSight.Get()})
    { Sight->SetSkeletalMeshAsset(Data->SightMesh); Sight->SetRelativeTransform(Data->SightTransform); }
    for (auto* Component : {FirstPersonRifle.Get(), WorldRifle.Get()})
    {
        Component->SetSkeletalMeshAsset(Data->Mesh);
        Component->EmptyOverrideMaterials();
        if(Component==WorldRifle && Data->bWorldReferencePose)Component->SetRelativeTransform(ERWorldWeaponPose::Attachment(*Data,GetMesh()->GetSocketTransform(TEXT("ER_Palm_R")).GetScale3D().X));
        else Component->SetRelativeTransform(Component==FirstPersonRifle && Data->bFirstPersonPalmGrip ? Data->FirstPersonGripTransform : Data->GripTransform);
        Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        Component->SetAnimation(nullptr);
        if (Data->WeaponIdleAnimation) Component->PlayAnimation(Data->WeaponIdleAnimation, true);
    }
    if (Data->IdleAnimation) FirstPersonBody->PlayAnimation(Data->IdleAnimation, true);
    if (Data->bFitSupportHand)
    {
        // A stationary/culled mesh can reuse its evaluated idle. Equipping a
        // different support target must invalidate that presentation immediately.
        GetMesh()->TickAnimation(0.f, false);
        GetMesh()->RefreshBoneTransforms();
    }

    MuzzleFlashUntil = 0;
    UE_LOG(LogTemp, Display, TEXT("ER_WEAPON_MODEL name=%s mesh=%s"), *Data->DisplayName, *Data->Mesh->GetPathName());
}
void AERCharacter::MulticastShot(FVector_NetQuantize Start,FVector_NetQuantize End,bool bTracer,UERWeaponData* FiredData,const TArray<FVector_NetQuantize>& PelletEnds)
{
    if(!HasAuthority())return;
    FireAcknowledgement.Sequence=ServerFireInput.Sequence;
    FireAcknowledgement.ShotOrdinal=++ServerFireOrdinal;
    FireAcknowledgement.Data=FiredData;
    FireAcknowledgement.ShotServerTime=GetWorld()->GetTimeSeconds();
    FireAcknowledgement.Firing=Weapon && Weapon->IsFiring();
    ++FireAcknowledgement.Revision;ForceNetUpdate();
    MulticastConfirmedShot(Start,End,bTracer,FiredData,PelletEnds,ServerFireInput.Sequence,ServerFireOrdinal);
}
void AERCharacter::MulticastConfirmedShot_Implementation(FVector_NetQuantize Start,FVector_NetQuantize End,bool bTracer,UERWeaponData* FiredData,const TArray<FVector_NetQuantize>& PelletEnds,uint16 Sequence,uint16 Ordinal)
{
    if (FParse::Param(FCommandLine::Get(),TEXT("ERShotAudit")))
        UE_LOG(LogTemp,Display,TEXT("ER_PELLET_TRACERS pawn=%s weapon=%s count=%d"),*GetName(),*GetNameSafe(FiredData),bTracer ? (PelletEnds.IsEmpty()?1:FMath::Min(PelletEnds.Num(),16)) : 0);
    const auto* Data=FiredData ? FiredData:(Weapon ? Weapon->Data.Get():nullptr);
    // Only confirmed endpoints may open smoke. Cosmetics never mutate recon.
    if(Data && Data->Payload==EERPayload::Bullet)
        for(TActorIterator<AERSmokeCloud> It(GetWorld());It;++It)It->AddShot(Start,End);
    if(GetNetMode()==NM_DedicatedServer || Health->bDead || !bNetworkReady || IsWeaponStowed() || Cast<UERMovement>(GetCharacterMovement())->bHighClimb)return;
    const bool bFirstPerson=IsLocallyControlled() && Controller && Controller->IsPlayerController();
    if(bTracer && Data && Data->Payload==EERPayload::Bullet)
    {
        const uint32 ShotKey = FERShotPresentationWindow::Key(Sequence, Ordinal);
        const int32 PredictedIndex = bFirstPerson ? PredictedTracers.IndexOfByPredicate(
            [ShotKey](const FPredictedTracer& Entry) { return Entry.ShotKey == ShotKey; }) : INDEX_NONE;
        const FVector* Muzzle = PredictedIndex != INDEX_NONE ? &PredictedTracers[PredictedIndex].Muzzle : nullptr;
        const auto SpawnTracer = [this, bFirstPerson, Muzzle](const FVector& Impact)
        {
            AERAttachedTracer::Spawn(this, TracerSystem, Impact, bFirstPerson, .016f, Muzzle);
        };
        if (PelletEnds.IsEmpty()) SpawnTracer(End);
        else for (int32 I = 0; I < FMath::Min(PelletEnds.Num(), 16); ++I) SpawnTracer(PelletEnds[I]);
        if (Muzzle)
        {
#if !UE_BUILD_SHIPPING
            if (FParse::Param(FCommandLine::Get(), TEXT("ERTracerAudit")))
                UE_LOG(LogTemp, Display, TEXT("ER_TRACER_CONFIRM key=%u frame=%llu muzzle_shift=%.1f muzzle=%s end=%s"),
                    ShotKey, static_cast<unsigned long long>(GFrameCounter),
                    FVector::Dist(*Muzzle, GetRenderedMuzzle(true)), *Muzzle->ToString(), *End.ToString());
#endif
            PredictedTracers.RemoveAt(PredictedIndex);
        }
    }
    if(ShotPresentation.Mark(Sequence,Ordinal))PlayShotPresentation(FiredData);
}
void AERCharacter::PlayShotPresentation(UERWeaponData* FiredData)
{
    if(GetNetMode()==NM_DedicatedServer || Health->bDead || !bNetworkReady || IsWeaponStowed() || Cast<UERMovement>(GetCharacterMovement())->bHighClimb)return;
    ++ShotFeedbackCount;
    const auto* Data=Weapon ? Weapon->Data.Get():nullptr;
    const bool bFirstPerson=IsLocallyControlled() && Controller && Controller->IsPlayerController();
    if (Data && Data->WeaponFireAnimation)
        for (auto* Component : {FirstPersonRifle.Get(), WorldRifle.Get()})
        {
            Component->PlayAnimation(Data->WeaponFireAnimation, false);
            Component->SetPlayRate(FMath::Max(1.f, Data->WeaponFireAnimation->GetPlayLength() / FMath::Max(.01f, Data->ShotInterval)));
        }
    if (Data && Data->Payload == EERPayload::Bullet && ShotFeedbackCount % 2 == 1)
    {
        const FVector Forward = GetBaseAimRotation().Vector();
        const FVector Right = FRotationMatrix(GetBaseAimRotation()).GetUnitAxis(EAxis::Y);
        FVector Port = GetVisualMuzzle(bFirstPerson) - Forward * (Data->bOneHanded ? 8.f : 24.f) + Right * 4.f;
        if (bFirstPerson)
            if (auto* PC = Cast<APlayerController>(Controller); PC && PC->PlayerCameraManager)
                Port = PC->PlayerCameraManager->GetCameraCacheView().TransformWorldToFirstPerson(Port, false);
        AERSpentCasing::Eject(GetWorld(), CasingClass, Port, Right, Forward, bFirstPerson ? .2f : 1.f);
    }
    const auto* Animation = Data && Data->Mesh ? Data->FireAnimation.Get() : FirstPersonFireAnimation.Get();
    // New weapon palettes preserve their recorded identity (no legacy per-gun pitch).
    // The accepted weapon identity travels with the existing event: its actor
    // pointer/data replication may arrive later on a joining peer or after a
    // quick switch. Variant selection remains entirely local.
    const auto* AudioData=FiredData ? FiredData : Data;
    const auto* Palette=AudioData ? AudioData->ShotAudio.Get() : nullptr;
    const float Pitch = AudioData ? AudioData->SoundPitch : 1.f;
    int32 AudioVariant=INDEX_NONE;
    const bool bAudioGunshot = !AudioData || AudioData->Payload == EERPayload::Bullet || AudioData->Payload == EERPayload::Rocket || AudioData->Payload == EERPayload::LaunchedHE;
    // Keep the original visual/recoil decision independent of the audio snapshot.
    const bool bGunshot = !Data || Data->Payload == EERPayload::Bullet || Data->Payload == EERPayload::Rocket || Data->Payload == EERPayload::LaunchedHE;
    if (bFirstPerson)
    {
        // Animate the body and its socket-attached rifle together. Both hands keep their grip.
        if (Animation)
        {
            FirstPersonBody->PlayAnimation(const_cast<UAnimSequence*>(Animation), false);
            FirstPersonBody->SetPlayRate(Data && Data->WeaponFireAnimation ? FMath::Max(1.f,Animation->GetPlayLength()/FMath::Max(.01f,Data->ShotInterval)) : 1.f);
            if (auto* Instance = FirstPersonBody->GetSingleNodeInstance()) Instance->SetPosition(0.f, false);
        }
        if (bGunshot && !(Data && Data->bUseWorksheetRecoil))
        {
            const float Recoil=Weapon?Weapon->GetScopeRecoilMultiplier():1.f;
            CameraKick = FMath::Min(CameraKick + .35f*Recoil, .65f*Recoil);
        }
        if (bAudioGunshot)
        {
            if (Palette) AudioVariant=ERAudio::Play(this,Palette,ShotShuffles.FindOrAdd(Palette),GetVisualMuzzle(true),true,.35f,nullptr);
            else if (FireSound) UGameplayStatics::PlaySound2D(this, FireSound, .35f, Pitch);
        }
    }
    else if (bAudioGunshot)
    {
        if (Palette) AudioVariant=ERAudio::Play(this,Palette,ShotShuffles.FindOrAdd(Palette),GetVisualMuzzle(false),false,.6f,FireAttenuation);
        else if (FireSound) UGameplayStatics::PlaySoundAtLocation(this, FireSound, GetVisualMuzzle(false), .6f, Pitch, 0.f, FireAttenuation);
    }
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(),TEXT("ERAudioAudit")) && bAudioGunshot)
        UE_LOG(LogTemp,Display,TEXT("ER_SHOT_AUDIO pawn=%s local=%d palette=%s variant=%d event=%u pellets=%d"),
            *GetName(),bFirstPerson,*GetNameSafe(Palette),AudioVariant,ShotFeedbackCount,AudioData?AudioData->Pellets:1);
#endif
    // Keep the flash variation independent of gameplay and spread RNG.
    const bool bBulletFlash = Data && Data->Payload == EERPayload::Bullet;
    const bool bShowFlash = !(Data && Data->bOneHanded && !bBulletFlash);
    const bool bOptic = Weapon && Weapon->HasOpticalScope() && Weapon->ScopeLevel;
    FRandomStream FlashRandom(int32(ShotFeedbackCount & MAX_int32));
    const float Roll = FlashRandom.FRandRange(-PI, PI);
    const float Length = FlashRandom.FRandRange(.75f, 1.22f);
    const FVector FlashScale = bBulletFlash ? FVector(.035, .092 * Length, .030) : FVector(.012, .025, .012);
    for (auto* Flash : {FirstPersonMuzzleFlash.Get(), WorldMuzzleFlash.Get()})
    {
        Flash->SetWorldRotation(Flash->GetAttachParent()->GetComponentQuat() *
            (Data && Data->Mesh ? FRotator(0,-90,0).Quaternion() : FQuat::Identity));
        const bool bViewFlash = Flash == FirstPersonMuzzleFlash;
        const FVector Tip = GetVisualMuzzle(bViewFlash);
        const FQuat BarrelRotation = Flash->GetComponentQuat();
        Flash->SetWorldLocation(Tip + BarrelRotation.GetAxisY() * (bBulletFlash ? 5. * Length : 4.));
        Flash->SetWorldScale3D(FlashScale);
        Flash->SetVisibility(bShowFlash && (!bViewFlash || !bOptic), true);
        for (int32 Index = 0; Index < 2; ++Index)
        {
            auto* Jet = MuzzleFlashJets[(bViewFlash ? 0 : 2) + Index].Get();
            Jet->SetHiddenInGame(!bBulletFlash);
            if (!bBulletFlash) continue;
            const FQuat JetRotation = BarrelRotation * FQuat(FVector::YAxisVector, Roll + Index * PI) *
                FQuat(FVector::ZAxisVector, Index == 0 ? .43 : .70);
            const double JetLength = (Index == 0 ? 8. : 5.) * Length;
            Jet->SetWorldLocationAndRotation(Tip + JetRotation.GetAxisY() * JetLength * .48, JetRotation);
            Jet->SetWorldScale3D(FVector(.014, JetLength * .01, .012));
        }
    }
    MuzzleFlashDuration = bShowFlash ? (bBulletFlash ? FlashRandom.FRandRange(.039f, .051f) : .018f) : 0.f;
    MuzzleFlashUntil = GetWorld()->GetTimeSeconds() + MuzzleFlashDuration;
    if (bBulletFlash && FApp::CanEverRender())
    {
        const bool bViewLight = bFirstPerson && !bOptic;
        MuzzleLightPeak = FlashRandom.FRandRange(3.6f, 4.6f) * (Data->bOneHanded ? .72f : 1.f);
        MuzzleLight->SetWorldLocation(GetVisualMuzzle(bViewLight));
        MuzzleLight->SetLightColor(FLinearColor(1.f, FlashRandom.FRandRange(.46f, .60f), .24f));
        MuzzleLight->SetIntensity(MuzzleLightPeak);
        MuzzleLight->SetVisibility(true);
    }
    else MuzzleLight->SetVisibility(false);
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("ERShotAudit")))
    {
        UE_LOG(LogTemp, Display, TEXT("ER_SHOT_FEEDBACK pawn=%s local=%d count=%u animation=%d sound=%d flash=%d"),
            *GetName(), bFirstPerson, ShotFeedbackCount, FirstPersonFireAnimation != nullptr, FireSound != nullptr,
            MuzzleFlashMesh != nullptr && MuzzleFlashMaterial != nullptr);
    }
#endif
}
void AERCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AERCharacter, Weapon);
    DOREPLIFETIME_CONDITION(AERCharacter,WeaponSelectionRevision,COND_OwnerOnly);
    DOREPLIFETIME(AERCharacter, bReferenceInventory);
    DOREPLIFETIME(AERCharacter, bPresetInventory); DOREPLIFETIME(AERCharacter, ActivePreset); DOREPLIFETIME(AERCharacter, ActiveVariant);
    DOREPLIFETIME(AERCharacter, Inventory);
    DOREPLIFETIME(AERCharacter,bNetworkReady);
    DOREPLIFETIME(AERCharacter,bWeaponLightOn);
    DOREPLIFETIME_CONDITION(AERCharacter,FireAcknowledgement,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AERCharacter, SpawnFacingYaw, COND_InitialOnly);
    DOREPLIFETIME_CONDITION(AERCharacter, RecoilState, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AERCharacter, DeathFocus, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AERCharacter, DeathFocusPosition, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AERCharacter, bHasDeathFocus, COND_OwnerOnly);
}
