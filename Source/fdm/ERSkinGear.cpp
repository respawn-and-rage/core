#include "ERCorpse.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#if WITH_EDITOR
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "SkeletalMeshAttributes.h"
#endif

FERAppearanceAttachment AERCorpse::MakeFieldAttachment(UStaticMesh* Gear, UMaterialInterface* Material, USkeletalMesh* Character, FName Bone, FVector Offset, FVector Scale)
{
    FERAppearanceAttachment Part;
    if (!Character) return Part;
    const auto& Ref = Character->GetRefSkeleton();
    int32 Index = Ref.FindBoneIndex(Bone);
    if (Index == INDEX_NONE) return Part;
    FTransform Reference = Ref.GetRefBonePose()[Index];
    while ((Index = Ref.GetParentIndex(Index)) != INDEX_NONE) Reference *= Ref.GetRefBonePose()[Index];
    const FTransform Desired(FQuat::Identity, Reference.GetLocation() + Offset, Scale);
    Part.Mesh = Gear; Part.Material = Material; Part.Bone = Bone;
    Part.Transform = Desired.GetRelativeTransform(Reference);
    return Part;
}

bool AERCorpse::BuildFieldGear(UStaticMesh* Mesh, bool bHelmet, bool bOutwardFacing)
{
#if WITH_EDITOR
    if (!Mesh || !Mesh->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Characters/Teams/"))) return false;
    FMeshDescription Description;
    FStaticMeshAttributes Attributes(Description); Attributes.Register();
    Attributes.GetVertexInstanceUVs().SetNumChannels(1);
    const auto Group = Description.CreatePolygonGroup();
    Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Fabric");
    auto Triangle = [&](FVector A, FVector B, FVector C)
    {
        TArray<FVertexInstanceID> Instances;
        const FVector Normal = FVector::CrossProduct(B-A, C-A).GetSafeNormal();
        const FVector Tangent = (B-A).GetSafeNormal();
        // Unreal's front face uses clockwise winding. Keep the outward normals
        // but reverse the generated polygon for newly fitted closed silhouettes.
        // Opt in so existing saved/shared gear remains unchanged.
        if (bOutwardFacing) Swap(B, C);
        int32 UVIndex = 0;
        for (const FVector Point : {A,B,C})
        {
            auto Vertex = Description.CreateVertex();
            Attributes.GetVertexPositions()[Vertex] = FVector3f(Point);
            auto Instance = Description.CreateVertexInstance(Vertex);
            Attributes.GetVertexInstanceNormals()[Instance] = FVector3f(Normal);
            Attributes.GetVertexInstanceTangents()[Instance] = FVector3f(Tangent);
            Attributes.GetVertexInstanceBinormalSigns()[Instance] = 1;
            Attributes.GetVertexInstanceUVs().Set(Instance, 0, FVector2f(UVIndex == 1 ? 1 : 0, UVIndex == 2 ? 1 : 0));
            Attributes.GetVertexInstanceColors()[Instance] = FVector4f(1,1,1,1);
            Instances.Add(Instance); ++UVIndex;
        }
        Description.CreatePolygon(Group, Instances);
    };
    // Rounded textile pack and pockets, or a simple helmet shell with a rolled rim.
    auto Surface = [&](FVector Center, FVector Radius, bool Hemisphere)
    {
        constexpr int32 Sides = 32, Rings = 12;
        const float MinAngle = Hemisphere ? -.12f : -PI*.5f;
        auto Point = [&](int32 R, int32 S)
        {
            const float Latitude = FMath::Lerp(MinAngle, PI*.5f-.001f, float(R)/Rings);
            const float Angle = 2*PI*float(S)/Sides;
            // Superellipsoid makes bags box-shaped with softened corners.
            auto Shape = [&](float X) { return Hemisphere ? X : FMath::Sign(X)*FMath::Pow(FMath::Abs(X), .28f); };
            return Center + Radius*FVector(Shape(FMath::Cos(Latitude))*Shape(FMath::Cos(Angle)),
                Shape(FMath::Cos(Latitude))*Shape(FMath::Sin(Angle)), Shape(FMath::Sin(Latitude)));
        };
        for (int32 R=0; R<Rings; ++R) for (int32 S=0; S<Sides; ++S)
        {
            FVector A=Point(R,S), B=Point(R,S+1), C=Point(R+1,S+1), D=Point(R+1,S);
            Triangle(A,B,C); Triangle(A,C,D);
        }
        if (Hemisphere)
        {
            for (int32 S=0; S<Sides; ++S)
            {
                FVector A=Point(0,S), B=Point(0,S+1);
                FVector C=Center+(B-Center)*FVector(.94f,.94f,1), D=Center+(A-Center)*FVector(.94f,.94f,1);
                Triangle(A,D,C); Triangle(A,C,B);
            }
        }
    };
    if (bHelmet) Surface(FVector::ZeroVector, FVector(10.3f,12.3f,12), true);
    else
    {
        Surface(FVector::ZeroVector, FVector(14,7,20), false);
        Surface(FVector(0,-8,-5), FVector(10,3,9), false);
        for (int32 Side : {-1,1}) Surface(FVector(Side*14,0,-7), FVector(3,6,10), false);
        for (int32 Side : {-1,1}) Surface(FVector(Side*9,6,0), FVector(2,1,18), false);
    }
    Mesh->Modify();
    Mesh->GetStaticMaterials().Reset();
    Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Fabric")));
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bBuildSimpleCollision = false;
    Params.bFastBuild = false;
    if (!Mesh->BuildFromMeshDescriptions({&Description}, Params)) return false;
    Mesh->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}


bool AERCorpse::BuildDrongoMask(UStaticMesh* TargetMesh,USkeletalMesh* SourceMesh)
{
#if WITH_EDITOR
    if(!TargetMesh || !SourceMesh || !TargetMesh->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Characters/Teams/Gear/")))return false;
    const FMeshDescription* Source=SourceMesh->GetMeshDescription(0);if(!Source)return false;
    const auto& Ref=SourceMesh->GetRefSkeleton();const int32 HeadBone=Ref.FindBoneIndex(TEXT("head"));int32 Bone=HeadBone;if(Bone==INDEX_NONE)return false;
    FTransform Head=Ref.GetRefBonePose()[Bone];while((Bone=Ref.GetParentIndex(Bone))!=INDEX_NONE)Head*=Ref.GetRefBonePose()[Bone];
    FSkeletalMeshConstAttributes In(*Source);
    TArray<int32> BoneMap;BoneMap.Init(INDEX_NONE,In.Bones().GetArraySize());
    for(const FBoneID ID:In.Bones().GetElementIDs())BoneMap[ID.GetValue()]=Ref.FindBoneIndex(In.GetBoneNames()[ID]);
    const auto Weights=In.GetVertexSkinWeights();
    auto HeadWeight=[&](FVertexID Vertex)
    {
        float Total=0;
        for(const auto Weight:Weights.Get(Vertex))
        {
            int32 Index=BoneMap.IsValidIndex(Weight.GetBoneIndex())?BoneMap[Weight.GetBoneIndex()]:INDEX_NONE;
            // The mask's two upper hoses are articulated children of head.
            // Exclude their entire bone chains rather than baking the long
            // reference-pose tubes into the rigid Breacher/Marksman accessory.
            bool bHeadHose = false;
            while(Index!=INDEX_NONE && Index!=HeadBone)
            {
                bHeadHose |= Ref.GetBoneName(Index).ToString().StartsWith(TEXT("headHose"), ESearchCase::IgnoreCase);
                Index=Ref.GetParentIndex(Index);
            }
            if(Index==HeadBone && !bHeadHose)Total+=Weight.GetWeight();
        }
        return Total;
    };
    FMeshDescription Out;FStaticMeshAttributes Attr(Out);Attr.Register();
    const int32 UVs=In.GetVertexInstanceUVs().GetNumChannels();Attr.GetVertexInstanceUVs().SetNumChannels(UVs);
    TargetMesh->GetStaticMaterials().Reset();
    TMap<FPolygonGroupID,FPolygonGroupID> Groups;int32 Triangles=0;
    for(const FTriangleID Triangle:Source->Triangles().GetElementIDs())
    {
        const FPolygonGroupID Group=Source->GetTrianglePolygonGroup(Triangle);
        const FName Slot=In.GetPolygonGroupMaterialSlotNames()[Group];const FString Name=Slot.ToString().ToLower();
        if(!Name.Contains(TEXT("variant")) && !Name.Contains(TEXT("bones")))continue;
        const auto Corners=Source->GetTriangleVertexInstances(Triangle);bool InHead=true;
        for(const auto Corner:Corners)
        {
            const FVector P(In.GetVertexPositions()[Source->GetVertexInstanceVertex(Corner)]);
            const FVector Offset=P-Head.GetLocation();
            InHead &= Offset.Z>-22 && Offset.Z<30 && FMath::Abs(Offset.X)<26 && FMath::Abs(Offset.Y)<35 && HeadWeight(Source->GetVertexInstanceVertex(Corner))>.8f;
        }
        if(!InHead)continue;
        if(!Groups.Contains(Group))
        {
            const auto NewGroup=Out.CreatePolygonGroup();Groups.Add(Group,NewGroup);Attr.GetPolygonGroupMaterialSlotNames()[NewGroup]=Slot;
            const int32 Material=SourceMesh->GetMaterials().IndexOfByPredicate([&](const FSkeletalMaterial& M){return M.MaterialSlotName==Slot || M.ImportedMaterialSlotName==Slot;});
            TargetMesh->GetStaticMaterials().Add(FStaticMaterial(Material==INDEX_NONE?nullptr:SourceMesh->GetMaterials()[Material].MaterialInterface.Get(),Slot));
        }
        TArray<FVertexInstanceID> NewCorners;
        for(const auto Corner:Corners)
        {
            const auto Vertex=Out.CreateVertex();Attr.GetVertexPositions()[Vertex]=FVector3f(Head.InverseTransformPosition(FVector(In.GetVertexPositions()[Source->GetVertexInstanceVertex(Corner)])));
            const auto Instance=Out.CreateVertexInstance(Vertex);NewCorners.Add(Instance);
            Attr.GetVertexInstanceNormals()[Instance]=FVector3f(Head.InverseTransformVectorNoScale(FVector(In.GetVertexInstanceNormals()[Corner])));
            Attr.GetVertexInstanceTangents()[Instance]=FVector3f(Head.InverseTransformVectorNoScale(FVector(In.GetVertexInstanceTangents()[Corner])));
            Attr.GetVertexInstanceBinormalSigns()[Instance]=In.GetVertexInstanceBinormalSigns()[Corner];
            Attr.GetVertexInstanceColors()[Instance]=In.GetVertexInstanceColors()[Corner];
            for(int32 UV=0;UV<UVs;++UV)Attr.GetVertexInstanceUVs().Set(Instance,UV,In.GetVertexInstanceUVs().Get(Corner,UV));
        }
        Out.CreatePolygon(Groups[Group],NewCorners);++Triangles;
    }
    if(Triangles<50)return false;
    UStaticMesh::FBuildMeshDescriptionsParams Params;Params.bBuildSimpleCollision=false;Params.bFastBuild=false;
    if(!TargetMesh->BuildFromMeshDescriptions({&Out},Params))return false;
    TargetMesh->MarkPackageDirty();UE_LOG(LogTemp,Display,TEXT("ER_DRONGO_MASK_BUILD triangles=%d materials=%d"),Triangles,Groups.Num());return true;
#else
    return false;
#endif
}
